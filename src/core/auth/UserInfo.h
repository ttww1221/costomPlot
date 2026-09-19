/**
 * @file    UserInfo.h
 * @brief   用户模型与权限定义 —— 领域模型层（S8）
 *
 * 把"角色"和"权限判定"集中在这里，UI 与业务层都只问 Permission，
 * 不散落 if (role == Admin) 之类的判断，日后加角色只改一处。
 *
 * 三级角色（数据库中按 int 存储）：
 *   Admin    0  管理员：全部权限 + 用户管理 + 清空审计/报警记录
 *   Operator 1  操作员：连接串口、控制风扇/电机、设定目标温度、记录与回放
 *   Guest    2  访客  ：只读，查看波形与统计，控制区整体灰化
 */

#ifndef USERINFO_H
#define USERINFO_H

#include <QDateTime>
#include <QMetaType>
#include <QString>

enum class UserRole {
    Admin = 0,
    Operator = 1,
    Guest = 2
};

/**
 * @brief 用户账号信息（不含口令，可安全地在界面间传递）
 */
struct UserInfo
{
    int id = -1;
    QString username;
    QString displayName;
    UserRole role = UserRole::Guest;
    bool enabled = true;
    QDateTime createdAt;
    QDateTime lastLogin;

    bool isValid() const { return id >= 0 && !username.isEmpty(); }
};

Q_DECLARE_METATYPE(UserInfo)

/**
 * @brief 角色 <-> 存储/显示文本 的转换
 */
namespace UserText {

inline QString role(UserRole r)
{
    switch (r) {
    case UserRole::Admin:    return QStringLiteral("管理员");
    case UserRole::Operator: return QStringLiteral("操作员");
    default:                 return QStringLiteral("访客");
    }
}

inline UserRole roleFromInt(int v)
{
    switch (v) {
    case 0:  return UserRole::Admin;
    case 1:  return UserRole::Operator;
    default: return UserRole::Guest;
    }
}

}

/**
 * @brief 权限判定：全部为纯函数，便于单元测试
 */
namespace Permission {

// 连接/断开串口
inline bool canConnectSerial(UserRole r) { return r != UserRole::Guest; }
// 下发控制指令（风扇、电机角度、回零）
inline bool canControl(UserRole r) { return r != UserRole::Guest; }
// 切换手动/自动模式、修改目标温度
inline bool canChangeSetpoint(UserRole r) { return r != UserRole::Guest; }
// 修改报警阈值
inline bool canConfigureAlarm(UserRole r) { return r != UserRole::Guest; }
// 开始/停止数据记录、载入回放
inline bool canRecord(UserRole r) { return r != UserRole::Guest; }
// 增删用户、改他人密码、启用/禁用账号
inline bool canManageUsers(UserRole r) { return r == UserRole::Admin; }
// 清空审计日志 / 报警历史
inline bool canPurgeData(UserRole r) { return r == UserRole::Admin; }

}

#endif // USERINFO_H
