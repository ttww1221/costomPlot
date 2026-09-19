/**
 * @file    Session.h
 * @brief   登录会话 —— 业务逻辑层（S8）
 *
 * 持有"当前登录用户"，是 UI 权限门控与审计日志取用户信息的唯一来源。
 * 刻意不做成全局单例：由 main() 创建后显式注入 MainWindow，
 * 依赖关系可见、单元测试可替换。
 *
 * 登录状态变化通过信号广播，主窗口收到后统一刷新各面板的可用状态
 * （applyPermissions），面板自身不持有权限判断逻辑。
 */

#ifndef SESSION_H
#define SESSION_H

#include <QObject>

#include "UserInfo.h"

class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(QObject *parent = nullptr);

    bool isLoggedIn() const { return m_user.isValid(); }
    UserInfo user() const { return m_user; }
    int userId() const { return m_user.id; }
    QString userName() const { return m_user.username; }
    UserRole role() const { return m_user.role; }
    QString roleText() const { return UserText::role(m_user.role); }

    // 便捷权限查询（转发给 Permission 命名空间）
    bool canControl() const { return Permission::canControl(m_user.role); }
    bool canManageUsers() const { return Permission::canManageUsers(m_user.role); }
    bool canPurgeData() const { return Permission::canPurgeData(m_user.role); }

public slots:
    void login(const UserInfo &user);   // 登录成功，建立会话
    void logout();                      // 注销，清空会话
    // 管理员改了当前用户的资料/角色后同步显示（不改登录状态）
    void updateUser(const UserInfo &user);

signals:
    void sigLoggedIn(const UserInfo &user);
    void sigLoggedOut();
    // 审计服务据此记住"当前操作者"
    void sigUserChanged(int userId, const QString &username);
    // 权限变化：主窗口据此刷新面板可用状态
    void sigPermissionsChanged();

private:
    UserInfo m_user;
};

#endif // SESSION_H
