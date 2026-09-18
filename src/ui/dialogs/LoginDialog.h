/**
 * @file    LoginDialog.h
 * @brief   登录对话框 —— 展示层（S8）
 *
 * 程序启动后先弹出本对话框，认证通过才创建主窗口（见 main.cpp），
 * 因此"未登录不可用"是结构性保证，而不是靠界面置灰来约束。
 *
 * 安全细节：
 *  - 口令框默认掩码显示，可勾选临时明文（便于现场输入核对）；
 *  - 连续失败 5 次锁定 30 秒并倒计时，抑制交互式暴力猜测
 *    （离线哈希迭代的抗破解能力见 UserService 头文件说明）；
 *  - 每次成功/失败都写入审计日志，失败也留痕；
 *  - 错误提示统一为"用户名或密码错误"，不暴露账号是否存在。
 */

#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

#include "UserInfo.h"

class UserService;
class AuditService;
class Session;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(UserService *users, AuditService *audit, Session *session,
                         QWidget *parent = nullptr);

    UserInfo user() const { return m_user; }   // 登录成功的用户

private slots:
    void slotLogin();
    void slotToggleEcho(bool show);
    void slotLockTick();      // 锁定期倒计时

private:
    void setupUi();
    void startLockout();      // 触发锁定
    void setError(const QString &text);

    UserService *m_users;
    AuditService *m_audit;
    Session *m_session;

    QLineEdit *m_editUser;
    QLineEdit *m_editPwd;
    QCheckBox *m_chkShowPwd;
    QLabel *m_lblError;
    QLabel *m_lblHint;
    QPushButton *m_btnLogin;
    QPushButton *m_btnQuit;
    QTimer *m_lockTimer;

    UserInfo m_user;
    int m_failCount = 0;       // 连续失败次数
    int m_lockRemainSec = 0;   // 剩余锁定秒数

    static constexpr int kMaxFailBeforeLock = 5;
    static constexpr int kLockSeconds = 30;
};

#endif // LOGINDIALOG_H
