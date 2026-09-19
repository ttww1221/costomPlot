/**
 * @file    Session.cpp
 * @brief   登录会话实现
 */

#include "Session.h"

Session::Session(QObject *parent)
    : QObject(parent)
{
}

void Session::login(const UserInfo &user)
{
    m_user = user;
    emit sigUserChanged(m_user.id, m_user.username);
    emit sigPermissionsChanged();
    emit sigLoggedIn(m_user);
}

void Session::logout()
{
    if (!m_user.isValid())
        return;
    m_user = UserInfo();   // 复位为无效用户
    emit sigUserChanged(-1, QString());
    emit sigPermissionsChanged();
    emit sigLoggedOut();
}

void Session::updateUser(const UserInfo &user)
{
    // 仅当修改的是当前登录用户时才需要同步
    if (!m_user.isValid() || user.id != m_user.id)
        return;
    const bool roleChanged = user.role != m_user.role;
    m_user = user;
    if (roleChanged) {
        emit sigPermissionsChanged();
        emit sigUserChanged(m_user.id, m_user.username);
    }
}
