/**
 * @file    Database.cpp
 * @brief   SQLite 数据库连接与模式管理实现
 */

#include "Database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
constexpr int kSchemaVersion = 1;

// ---- 建表语句 -------------------------------------------------------------
// users       用户账号：口令以"加盐迭代哈希"存储，绝不明文落库
// audit_log   操作审计：谁、什么时候、做了什么、成功与否（登录/控制/记录等）
// alarm_event 报警事件：越限与恢复都入库，供事后追溯与报警统计
// schema_info 模式版本，升级时在 migrate 中按版本号逐段执行 ALTER
const char *const kCreateUsers =
    "CREATE TABLE IF NOT EXISTS users ("
    "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  username      TEXT    NOT NULL UNIQUE,"
    "  password_hash TEXT    NOT NULL,"
    "  salt          TEXT    NOT NULL,"
    "  iterations    INTEGER NOT NULL DEFAULT 10000,"
    "  role          INTEGER NOT NULL DEFAULT 2,"   // 0=管理员 1=操作员 2=访客
    "  display_name  TEXT,"
    "  enabled       INTEGER NOT NULL DEFAULT 1,"
    "  created_at    TEXT    NOT NULL,"
    "  last_login    TEXT"
    ")";

const char *const kCreateAudit =
    "CREATE TABLE IF NOT EXISTS audit_log ("
    "  id       INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  ts       TEXT    NOT NULL,"
    "  user_id  INTEGER,"
    "  username TEXT,"
    "  action   TEXT    NOT NULL,"
    "  detail   TEXT,"
    "  success  INTEGER NOT NULL DEFAULT 1"
    ")";

const char *const kCreateAlarm =
    "CREATE TABLE IF NOT EXISTS alarm_event ("
    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  ts        TEXT    NOT NULL,"
    "  channel   TEXT    NOT NULL,"   // TEMP / HUM
    "  level     INTEGER NOT NULL,"   // 0提示 1警告 2严重
    "  kind      INTEGER NOT NULL,"   // 0越上限 1越下限 2恢复
    "  value     REAL    NOT NULL,"
    "  threshold REAL    NOT NULL,"
    "  message   TEXT,"
    "  acked     INTEGER NOT NULL DEFAULT 0"
    ")";

const char *const kCreateSchemaInfo =
    "CREATE TABLE IF NOT EXISTS schema_info ("
    "  key   TEXT PRIMARY KEY,"
    "  value TEXT"
    ")";

// ---- 索引：按时间倒序查询是审计与报警面板的主要访问模式 ----
const char *const kIdxAuditTs = "CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_log(ts DESC)";
const char *const kIdxAlarmTs = "CREATE INDEX IF NOT EXISTS idx_alarm_ts ON alarm_event(ts DESC)";
const char *const kIdxAlarmAck = "CREATE INDEX IF NOT EXISTS idx_alarm_acked ON alarm_event(acked)";
}

Database::Database(QObject *parent)
    : QObject(parent)
    // 连接名带对象地址，保证同进程内多个 Database 实例互不干扰
    , m_connName(QStringLiteral("db_%1").arg(reinterpret_cast<quintptr>(this)))
{
}

Database::~Database()
{
    close();
}

QString Database::defaultPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/app.db"));
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

QString Database::filePath() const
{
    return m_db.databaseName();
}

QString Database::lastError() const
{
    return m_lastError;
}

int Database::schemaVersion() const
{
    return m_schemaVersion;
}

bool Database::open(const QString &path)
{
    if (m_db.isOpen())
        return true;

    const QString file = path.isEmpty() ? defaultPath() : path;

    // 内存库无需建目录；文件库要保证父目录存在，否则 SQLite 打开失败
    if (file != QLatin1String(":memory:")) {
        QDir dir = QFileInfo(file).absoluteDir();
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            m_lastError = QStringLiteral("无法创建数据库目录: %1").arg(dir.absolutePath());
            return false;
        }
    }

    if (QSqlDatabase::contains(m_connName))
        QSqlDatabase::removeDatabase(m_connName);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    m_db.setDatabaseName(file);

    if (!m_db.open()) {
        m_lastError = QStringLiteral("打开数据库失败: %1").arg(m_db.lastError().text());
        return false;
    }

    // PRAGMA 必须在连接建立后、业务查询前执行
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

    if (!createSchema())
        return false;

    // 读取已有版本号；首次建库时写入当前版本
    QSqlQuery vq(m_db);
    if (vq.exec(QStringLiteral("SELECT value FROM schema_info WHERE key='version'")) && vq.next())
        m_schemaVersion = vq.value(0).toInt();
    else
        setSchemaVersion(kSchemaVersion);

    return true;
}

void Database::close()
{
    if (!m_db.isOpen())
        return;
    m_db.close();
    // 必须先释放本地句柄再 removeDatabase，否则 Qt 会告警"连接仍被使用"
    const QString name = m_connName;
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool Database::createSchema()
{
    for (const char *sql : {kCreateUsers, kCreateAudit, kCreateAlarm, kCreateSchemaInfo,
                            kIdxAuditTs, kIdxAlarmTs, kIdxAlarmAck}) {
        if (!exec(QString::fromLatin1(sql)))
            return false;
    }
    return true;
}

bool Database::exec(const QString &sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        m_lastError = QStringLiteral("执行 SQL 失败: %1\n%2").arg(sql, q.lastError().text());
        return false;
    }
    return true;
}

bool Database::setSchemaVersion(int version)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO schema_info(key, value) VALUES('version', :v)"));
    q.bindValue(QStringLiteral(":v"), QString::number(version));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    m_schemaVersion = version;
    return true;
}
