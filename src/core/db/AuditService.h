/**
 * @file    AuditService.h
 * @brief   操作审计服务 —— 数据访问层（S8）
 *
 * 记录"谁、在什么时刻、做了什么、结果如何"，是工业上位机的合规性要求，
 * 也是论文中"系统安全性设计"一节的实证材料：任何一次风扇启停、
 * 目标温度修改、账号增删都留下不可抵赖的痕迹。
 *
 * 当前操作者由 Session.sigUserChanged 注入（slotCurrentUser），
 * 因此调用方写 audit->log("FAN_COMMAND", "开") 即可，不必每次传用户，
 * 避免了业务代码里到处穿透 Session 的耦合。
 */

#ifndef AUDITSERVICE_H
#define AUDITSERVICE_H

#include <QDateTime>
#include <QList>
#include <QObject>

#include "Database.h"

/**
 * @brief 一条审计记录
 */
struct AuditEntry
{
    qint64 id = -1;
    QDateTime ts;
    int userId = -1;
    QString username;
    QString action;
    QString detail;
    bool success = true;
};

Q_DECLARE_METATYPE(AuditEntry)

// 动作码常量：集中定义避免字符串拼写在各处漂移
namespace AuditAction {
constexpr const char *kLogin = "LOGIN";
constexpr const char *kLoginFailed = "LOGIN_FAILED";
constexpr const char *kLogout = "LOGOUT";
constexpr const char *kPortOpen = "PORT_OPEN";
constexpr const char *kPortClose = "PORT_CLOSE";
constexpr const char *kFanCommand = "FAN_COMMAND";
constexpr const char *kMotorCommand = "MOTOR_COMMAND";
constexpr const char *kModeChange = "MODE_CHANGE";
constexpr const char *kTargetChange = "TARGET_CHANGE";
constexpr const char *kRecordStart = "RECORD_START";
constexpr const char *kRecordStop = "RECORD_STOP";
constexpr const char *kReplayLoad = "REPLAY_LOAD";
constexpr const char *kAlarm = "ALARM";
constexpr const char *kAlarmAck = "ALARM_ACK";
constexpr const char *kAlarmThreshold = "ALARM_THRESHOLD";
constexpr const char *kUserCreate = "USER_CREATE";
constexpr const char *kUserDelete = "USER_DELETE";
constexpr const char *kUserPassword = "USER_PASSWORD";
constexpr const char *kStatReset = "STAT_RESET";
}

class AuditService : public QObject
{
    Q_OBJECT

public:
    explicit AuditService(Database *db, QObject *parent = nullptr);

    // 查询最近的记录；actionFilter 为空表示不过滤
    QList<AuditEntry> query(int limit = 200, const QString &actionFilter = QString()) const;
    int count() const;
    QString lastError() const { return m_lastError; }

public slots:
    // 由 Session.sigUserChanged 驱动，记住当前操作者
    void slotCurrentUser(int userId, const QString &username);
    // 写入一条审计记录
    bool log(const QString &action, const QString &detail = QString(), bool success = true);
    // 清空（仅管理员可调用，权限判定在界面层完成）
    bool clear();

signals:
    void sigEntryAdded(const AuditEntry &entry);

private:
    Database *m_db;
    int m_userId = -1;
    QString m_username;
    mutable QString m_lastError;
};

#endif // AUDITSERVICE_H
