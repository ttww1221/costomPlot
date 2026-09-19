/**
 * @file    UserManagerDialog.h
 * @brief   用户管理对话框 —— 展示层（S8，仅管理员可打开）
 *
 * 提供账号的全生命周期管理：新增、删除、改密、启用/禁用、调整角色。
 * 所有写操作都经 UserService 校验业务规则（唯一管理员不可删/不可降级、
 * 不可删除自己、用户名唯一、口令强度），并写入审计日志。
 */

#ifndef USERMANAGERDIALOG_H
#define USERMANAGERDIALOG_H

#include <QDialog>

#include "UserInfo.h"

class UserService;
class AuditService;
class Session;
class QTableWidget;
class QPushButton;
class QComboBox;
class QLabel;

class UserManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserManagerDialog(UserService *users, AuditService *audit, Session *session,
                               QWidget *parent = nullptr);

private slots:
    void slotRefresh();
    void slotAdd();
    void slotDelete();
    void slotChangePassword();
    void slotToggleEnabled();
    void slotApplyRole();
    void slotSelectionChanged();

private:
    void setupUi();
    void fillTable();
    UserInfo selectedUser() const;
    void setStatus(const QString &text, bool error = false);
    // 依据"不可操作自己 / 不可动唯一管理员"动态调整按钮可用状态
    void updateButtons();

    UserService *m_users;
    AuditService *m_audit;
    Session *m_session;

    QTableWidget *m_table;
    QComboBox *m_cmbRole;
    QPushButton *m_btnAdd;
    QPushButton *m_btnDelete;
    QPushButton *m_btnPassword;
    QPushButton *m_btnEnabled;
    QPushButton *m_btnApplyRole;
    QLabel *m_lblStatus;
};

#endif // USERMANAGERDIALOG_H
