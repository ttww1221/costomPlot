/**
 * @file    UserService.cpp
 * @brief   用户账号服务实现
 */

#include "UserService.h"
#include "DbTime.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

// ---------------------------------------------------------------------------
// PasswordHasher
// ---------------------------------------------------------------------------

namespace PasswordHasher {

QByteArray randomSalt(int bytes)
{
    if (bytes <= 0)
        bytes = 16;
    QByteArray salt(bytes, Qt::Uninitialized);
    QRandomGenerator *rng = QRandomGenerator::system();   // 系统密码学随机源
    for (int i = 0; i < bytes; ++i)
        salt[i] = static_cast<char>(rng->bounded(256));
    return salt;
}

QByteArray hash(const QString &password, const QByteArray &salt, int iterations)
{
    if (iterations < 1)
        iterations = 1;

    // 首轮：SHA256(salt || password)
    QByteArray h = QCryptographicHash::hash(salt + password.toUtf8(), QCryptographicHash::Sha256);
    // 后续轮次：SHA256(h || salt)，每轮都把盐重新混入，避免退化为单纯重复哈希
    for (int i = 1; i < iterations; ++i) {
        QCryptographicHash c(QCryptographicHash::Sha256);
        c.addData(h);
        c.addData(salt);
        h = c.result();
    }
    return h;
}

bool verify(const QString &password, const QByteArray &salt, int iterations, const QByteArray &expected)
{
    const QByteArray actual = hash(password, salt, iterations);

    // 定长时间比较：不论第几个字节失配都走完全程，不泄露匹配长度
    if (actual.size() != expected.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < actual.size(); ++i)
        diff |= static_cast<unsigned char>(actual.at(i)) ^ static_cast<unsigned char>(expected.at(i));
    return diff == 0;
}

}

// ---------------------------------------------------------------------------
// UserService
// ---------------------------------------------------------------------------

UserService::UserService(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

bool UserService::isValidUserName(const QString &name, QString *error)
{
    // 3~20 位字母/数字/下划线，且不以数字开头，避免与纯数字 ID 混淆
    static const QRegularExpression kRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{2,19}$"));
    if (!kRe.match(name).hasMatch()) {
        if (error)
            *error = QStringLiteral("用户名须为 3~20 位字母、数字或下划线，且以字母或下划线开头");
        return false;
    }
    return true;
}

bool UserService::isValidPassword(const QString &pwd, QString *error)
{
    if (pwd.length() < kMinPasswordLength) {
        if (error)
            *error = QStringLiteral("密码长度不得少于 %1 位").arg(kMinPasswordLength);
        return false;
    }
    return true;
}

bool UserService::ensureDefaultAdmin()
{
    if (userCount() > 0)
        return false;

    QString err;
    // 首次运行植入默认管理员，界面上会提示尽快修改口令
    return createUser(QStringLiteral("admin"), QStringLiteral("admin123"), UserRole::Admin,
                      QStringLiteral("系统管理员"), &err);
}

UserInfo UserService::authenticate(const QString &username, const QString &password, QString *error)
{
    // 统一话术：不区分"用户不存在"与"口令错误"，防止用户名枚举
    const QString generic = QStringLiteral("用户名或密码错误");
    UserInfo invalid;

    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("SELECT * FROM users WHERE lower(username) = lower(:u)"));
    q.bindValue(QStringLiteral(":u"), username.trimmed());
    if (!q.exec() || !q.next()) {
        m_lastError = q.lastError().text();
        if (error)
            *error = generic;
        return invalid;
    }

    const int id = q.value(QStringLiteral("id")).toInt();
    const int iterations = q.value(QStringLiteral("iterations")).toInt();
    const QByteArray salt = QByteArray::fromHex(q.value(QStringLiteral("salt")).toString().toLatin1());
    const QByteArray expected = QByteArray::fromHex(q.value(QStringLiteral("password_hash")).toString().toLatin1());

    if (iterations < 1 || salt.isEmpty() || expected.isEmpty()
        || !PasswordHasher::verify(password, salt, iterations, expected)) {
        if (error)
            *error = generic;
        return invalid;
    }

    UserInfo u = readRow(q);
    if (!u.enabled) {
        if (error)
            *error = QStringLiteral("账号已被禁用，请联系管理员");
        return invalid;
    }

    touchLastLogin(id);
    u.lastLogin = QDateTime::currentDateTime();
    return u;
}

bool UserService::createUser(const QString &username, const QString &password, UserRole role,
                             const QString &displayName, QString *error)
{
    const QString name = username.trimmed();
    if (!isValidUserName(name, error))
        return false;
    if (!isValidPassword(password, error))
        return false;

    // 用户名唯一性（忽略大小写）
    if (findByName(name).isValid()) {
        if (error)
            *error = QStringLiteral("用户名 %1 已存在").arg(name);
        return false;
    }

    const QByteArray salt = PasswordHasher::randomSalt();
    const QByteArray digest = PasswordHasher::hash(password, salt, kDefaultIterations);

    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral(
        "INSERT INTO users(username, password_hash, salt, iterations, role, display_name, enabled, created_at)"
        " VALUES(:u, :h, :s, :i, :r, :d, 1, :c)"));
    q.bindValue(QStringLiteral(":u"), name);
    q.bindValue(QStringLiteral(":h"), QString::fromLatin1(digest.toHex()));
    q.bindValue(QStringLiteral(":s"), QString::fromLatin1(salt.toHex()));
    q.bindValue(QStringLiteral(":i"), kDefaultIterations);
    q.bindValue(QStringLiteral(":r"), static_cast<int>(role));
    q.bindValue(QStringLiteral(":d"), displayName.isEmpty() ? name : displayName);
    q.bindValue(QStringLiteral(":c"), DbTime::now());
    return exec(q, QStringLiteral("创建用户"));
}

bool UserService::changePassword(int userId, const QString &newPassword, QString *error)
{
    if (!isValidPassword(newPassword, error))
        return false;

    const QByteArray salt = PasswordHasher::randomSalt();   // 改口令必须换盐
    const QByteArray digest = PasswordHasher::hash(newPassword, salt, kDefaultIterations);

    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("UPDATE users SET password_hash=:h, salt=:s, iterations=:i WHERE id=:id"));
    q.bindValue(QStringLiteral(":h"), QString::fromLatin1(digest.toHex()));
    q.bindValue(QStringLiteral(":s"), QString::fromLatin1(salt.toHex()));
    q.bindValue(QStringLiteral(":i"), kDefaultIterations);
    q.bindValue(QStringLiteral(":id"), userId);

    if (!exec(q, QStringLiteral("修改密码")))
        return false;
    if (q.numRowsAffected() == 0) {
        m_lastError = QStringLiteral("用户不存在: id=%1").arg(userId);
        if (error)
            *error = m_lastError;
        return false;
    }
    return true;
}

bool UserService::setEnabled(int userId, bool enabled, QString *error)
{
    // 禁用最后一个可用管理员会导致系统彻底失控，必须拒绝
    if (!enabled) {
        const UserInfo u = findById(userId);
        if (u.role == UserRole::Admin && u.enabled && enabledAdminCount() <= 1) {
            const QString msg = QStringLiteral("不能禁用唯一的管理员账号");
            m_lastError = msg;
            if (error)
                *error = msg;
            return false;
        }
    }

    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("UPDATE users SET enabled=:e WHERE id=:id"));
    q.bindValue(QStringLiteral(":e"), enabled ? 1 : 0);
    q.bindValue(QStringLiteral(":id"), userId);
    return exec(q, QStringLiteral("设置账号状态"));
}

bool UserService::setRole(int userId, UserRole role, QString *error)
{
    const UserInfo u = findById(userId);
    if (!u.isValid()) {
        m_lastError = QStringLiteral("用户不存在: id=%1").arg(userId);
        if (error)
            *error = m_lastError;
        return false;
    }

    // 把唯一的管理员降级会让系统失去管理入口，必须拒绝
    if (u.role == UserRole::Admin && role != UserRole::Admin && u.enabled && enabledAdminCount() <= 1) {
        m_lastError = QStringLiteral("不能降级唯一的管理员账号");
        if (error)
            *error = m_lastError;
        return false;
    }

    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("UPDATE users SET role=:r WHERE id=:id"));
    q.bindValue(QStringLiteral(":r"), static_cast<int>(role));
    q.bindValue(QStringLiteral(":id"), userId);
    return exec(q, QStringLiteral("修改角色"));
}

bool UserService::removeUser(int userId, int currentUserId, QString *error)
{
    if (userId == currentUserId) {
        const QString msg = QStringLiteral("不能删除当前登录的账号");
        m_lastError = msg;
        if (error)
            *error = msg;
        return false;
    }

    const UserInfo u = findById(userId);
    if (!u.isValid()) {
        const QString msg = QStringLiteral("用户不存在");
        m_lastError = msg;
        if (error)
            *error = msg;
        return false;
    }
    if (u.role == UserRole::Admin && enabledAdminCount() <= 1) {
        const QString msg = QStringLiteral("不能删除唯一的管理员账号");
        m_lastError = msg;
        if (error)
            *error = msg;
        return false;
    }

    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("DELETE FROM users WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), userId);
    return exec(q, QStringLiteral("删除用户"));
}

QList<UserInfo> UserService::listUsers() const
{
    QList<UserInfo> out;
    QSqlQuery q(m_db->handle());
    if (!q.exec(QStringLiteral("SELECT * FROM users ORDER BY role ASC, username ASC"))) {
        m_lastError = q.lastError().text();
        return out;
    }
    while (q.next())
        out.append(readRow(q));
    return out;
}

UserInfo UserService::findById(int userId) const
{
    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("SELECT * FROM users WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), userId);
    if (q.exec() && q.next())
        return readRow(q);
    return UserInfo();
}

UserInfo UserService::findByName(const QString &username) const
{
    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("SELECT * FROM users WHERE lower(username) = lower(:u)"));
    q.bindValue(QStringLiteral(":u"), username.trimmed());
    if (q.exec() && q.next())
        return readRow(q);
    return UserInfo();
}

int UserService::userCount() const
{
    QSqlQuery q(m_db->handle());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM users")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int UserService::enabledAdminCount() const
{
    QSqlQuery q(m_db->handle());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM users WHERE role=0 AND enabled=1")) && q.next())
        return q.value(0).toInt();
    return 0;
}

void UserService::touchLastLogin(int userId)
{
    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("UPDATE users SET last_login=:t WHERE id=:id"));
    q.bindValue(QStringLiteral(":t"), DbTime::now());
    q.bindValue(QStringLiteral(":id"), userId);
    q.exec();   // 登录时间更新失败不影响认证结果，不做错误上抛
}

UserInfo UserService::readRow(const QSqlQuery &q) const
{
    UserInfo u;
    u.id = q.value(QStringLiteral("id")).toInt();
    u.username = q.value(QStringLiteral("username")).toString();
    u.displayName = q.value(QStringLiteral("display_name")).toString();
    u.role = UserText::roleFromInt(q.value(QStringLiteral("role")).toInt());
    u.enabled = q.value(QStringLiteral("enabled")).toInt() != 0;
    u.createdAt = DbTime::fromDb(q.value(QStringLiteral("created_at")).toString());
    u.lastLogin = DbTime::fromDb(q.value(QStringLiteral("last_login")).toString());
    return u;
}

bool UserService::exec(QSqlQuery &q, const QString &what)
{
    if (q.exec())
        return true;
    m_lastError = QStringLiteral("%1失败: %2").arg(what, q.lastError().text());
    return false;
}
