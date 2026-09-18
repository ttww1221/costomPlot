/**
 * @file    UserService.h
 * @brief   用户账号服务 —— 数据访问层（S8）
 *
 * 口令安全设计（这是登录功能的核心，也是论文可写的安全分析点）：
 *  1. 绝不明文存储，也不做可逆加密；
 *  2. 每用户独立随机盐（16 字节，QRandomGenerator::system 提供密码学随机源），
 *     使相同口令在不同账号下得到不同摘要，抵御彩虹表与批量撞库；
 *  3. 迭代哈希（默认 10000 轮 SHA-256，即 PBKDF 的简化形式），
 *     把单次猜测的成本放大约一万倍，显著拖慢离线暴力破解；
 *     轮数随账号一起入库，日后可平滑提升强度而不影响老账号；
 *  4. 校验用定长时间比较（逐字节异或累积），避免因提前返回而泄露
 *     "前若干字节匹配"的时间侧信道；
 *  5. 认证失败统一返回"用户名或密码错误"，不区分是用户名不存在还是口令不对，
 *     防止通过响应差异枚举合法用户名。
 */

#ifndef USERSERVICE_H
#define USERSERVICE_H

#include <QList>
#include <QObject>

#include "Database.h"
#include "UserInfo.h"

/**
 * @brief 口令哈希工具（纯函数，可独立测试）
 */
namespace PasswordHasher {

// 生成随机盐
QByteArray randomSalt(int bytes = 16);

// 加盐迭代哈希：SHA256(salt||password) 后再迭代 iterations-1 轮 SHA256(h||salt)
QByteArray hash(const QString &password, const QByteArray &salt, int iterations);

// 定长时间比对
bool verify(const QString &password, const QByteArray &salt, int iterations, const QByteArray &expected);

}

class UserService : public QObject
{
    Q_OBJECT

public:
    explicit UserService(Database *db, QObject *parent = nullptr);

    static constexpr int kDefaultIterations = 10000;
    static constexpr int kMinPasswordLength = 6;

    // 库为空时创建默认管理员 admin/admin123，返回是否发生了创建
    bool ensureDefaultAdmin();

    /**
     * @brief 认证
     * @return 成功返回有效 UserInfo（并刷新 last_login），失败返回无效对象
     */
    UserInfo authenticate(const QString &username, const QString &password, QString *error = nullptr);

    bool createUser(const QString &username, const QString &password, UserRole role,
                    const QString &displayName = QString(), QString *error = nullptr);
    bool changePassword(int userId, const QString &newPassword, QString *error = nullptr);
    bool setEnabled(int userId, bool enabled, QString *error = nullptr);
    /**
     * @brief 删除用户
     * @param currentUserId 当前登录者，禁止删除自己
     */
    bool removeUser(int userId, int currentUserId, QString *error = nullptr);
    bool setRole(int userId, UserRole role, QString *error = nullptr);

    QList<UserInfo> listUsers() const;
    UserInfo findById(int userId) const;
    UserInfo findByName(const QString &username) const;
    int userCount() const;
    int enabledAdminCount() const;

    QString lastError() const { return m_lastError; }

    // 用户名/口令合法性校验（供界面即时提示复用）
    static bool isValidUserName(const QString &name, QString *error = nullptr);
    static bool isValidPassword(const QString &pwd, QString *error = nullptr);

private:
    bool exec(QSqlQuery &q, const QString &what);
    UserInfo readRow(const QSqlQuery &q) const;
    void touchLastLogin(int userId);

    Database *m_db;
    mutable QString m_lastError;
};

#endif // USERSERVICE_H
