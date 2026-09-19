/**
 * @file    LoginDialog.cpp
 * @brief   登录对话框实现
 */

#include "LoginDialog.h"

#include "AppSettings.h"
#include "AuditService.h"
#include "Session.h"
#include "UserService.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
const char *kErrorStyle = "color: #c0392b;";
const char *kHintStyle = "color: #7f8c8d; font-size: 11px;";
}

LoginDialog::LoginDialog(UserService *users, AuditService *audit, Session *session, QWidget *parent)
    : QDialog(parent)
    , m_users(users)
    , m_audit(audit)
    , m_session(session)
{
    setupUi();

    // 预填上次登录的用户名（只记名不记口令），减少现场重复输入
    const QString lastUser = AppSettings::loadLastUserName();
    if (!lastUser.isEmpty()) {
        m_editUser->setText(lastUser);
        m_editPwd->setFocus();
    } else {
        m_editUser->setFocus();
    }

    // 库里只有一个账号且是首次植入的默认管理员时给出提示，避免现场无从下手
    if (m_users && m_users->userCount() == 1) {
        const QList<UserInfo> all = m_users->listUsers();
        if (!all.isEmpty() && all.first().username.compare(QLatin1String("admin"), Qt::CaseInsensitive) == 0)
            m_lblHint->setText(QStringLiteral("首次运行已创建默认管理员 admin / admin123，登录后请尽快修改密码"));
    }
}

void LoginDialog::setupUi()
{
    setWindowTitle(QStringLiteral("用户登录"));
    setModal(true);
    setMinimumWidth(360);
    // 登录框不提供关闭按钮的"绕过"语义：点 X 等同退出程序
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QLabel *title = new QLabel(QStringLiteral("串口数据采集与测控上位机"));
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #2c3e50;"));
    title->setAlignment(Qt::AlignCenter);

    m_editUser = new QLineEdit;
    m_editUser->setPlaceholderText(QStringLiteral("用户名"));
    m_editUser->setMaxLength(20);
    m_editPwd = new QLineEdit;
    m_editPwd->setPlaceholderText(QStringLiteral("密码"));
    m_editPwd->setEchoMode(QLineEdit::Password);
    m_editPwd->setMaxLength(64);
    m_chkShowPwd = new QCheckBox(QStringLiteral("显示密码"));

    QFormLayout *form = new QFormLayout;
    form->addRow(QStringLiteral("用户名:"), m_editUser);
    form->addRow(QStringLiteral("密  码:"), m_editPwd);
    form->addRow(QString(), m_chkShowPwd);

    m_lblError = new QLabel;
    m_lblError->setStyleSheet(QString::fromLatin1(kErrorStyle));
    m_lblError->setWordWrap(true);
    m_lblError->hide();

    m_lblHint = new QLabel(QStringLiteral("管理员：全部功能　操作员：采集与控制　访客：只读"));
    m_lblHint->setStyleSheet(QString::fromLatin1(kHintStyle));
    m_lblHint->setWordWrap(true);

    m_btnLogin = new QPushButton(QStringLiteral("登录"));
    m_btnLogin->setDefault(true);
    m_btnQuit = new QPushButton(QStringLiteral("退出"));

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(m_btnLogin);
    btnRow->addWidget(m_btnQuit);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(title);
    main->addSpacing(10);
    main->addLayout(form);
    main->addWidget(m_lblError);
    main->addWidget(m_lblHint);
    main->addSpacing(6);
    main->addLayout(btnRow);

    m_lockTimer = new QTimer(this);
    m_lockTimer->setInterval(1000);
    connect(m_lockTimer, &QTimer::timeout, this, &LoginDialog::slotLockTick);

    connect(m_btnLogin, &QPushButton::clicked, this, &LoginDialog::slotLogin);
    connect(m_btnQuit, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_chkShowPwd, &QCheckBox::toggled, this, &LoginDialog::slotToggleEcho);
    // 在密码框回车即登录，符合用户习惯
    connect(m_editPwd, &QLineEdit::returnPressed, this, &LoginDialog::slotLogin);
    connect(m_editUser, &QLineEdit::returnPressed, this, [this]() { m_editPwd->setFocus(); });
}

void LoginDialog::slotToggleEcho(bool show)
{
    m_editPwd->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
}

void LoginDialog::slotLogin()
{
    if (m_lockRemainSec > 0)
        return;

    const QString name = m_editUser->text().trimmed();
    const QString pwd = m_editPwd->text();

    if (name.isEmpty() || pwd.isEmpty()) {
        setError(QStringLiteral("请输入用户名和密码"));
        return;
    }

    QString err;
    const UserInfo u = m_users->authenticate(name, pwd, &err);
    if (!u.isValid()) {
        ++m_failCount;
        if (m_audit)
            m_audit->log(QLatin1String(AuditAction::kLoginFailed),
                         QStringLiteral("用户=%1 原因=%2").arg(name, err), false);

        if (m_failCount >= kMaxFailBeforeLock)
            startLockout();
        else
            setError(QStringLiteral("%1（连续失败 %2/%3 次，达上限将锁定 %4 秒）")
                         .arg(err).arg(m_failCount).arg(kMaxFailBeforeLock).arg(kLockSeconds));

        m_editPwd->clear();
        m_editPwd->setFocus();
        return;
    }

    // 认证成功：清零失败计数、建立会话、再写审计
    // 顺序很重要：Session::login 会触发 sigUserChanged，
    // AuditService 据此记住操作者，这条 LOGIN 记录才带得上用户身份
    m_failCount = 0;
    m_user = u;
    if (m_session)
        m_session->login(u);
    if (m_audit)
        m_audit->log(QLatin1String(AuditAction::kLogin),
                     QStringLiteral("角色=%1").arg(UserText::role(u.role)));

    accept();
}

void LoginDialog::startLockout()
{
    m_lockRemainSec = kLockSeconds;
    m_btnLogin->setEnabled(false);
    m_lockTimer->start();
    setError(QStringLiteral("失败次数过多，账号输入已锁定，请 %1 秒后重试").arg(m_lockRemainSec));
}

void LoginDialog::slotLockTick()
{
    --m_lockRemainSec;
    if (m_lockRemainSec <= 0) {
        m_lockTimer->stop();
        m_failCount = 0;
        m_btnLogin->setEnabled(true);
        m_lblError->hide();
        return;
    }
    setError(QStringLiteral("失败次数过多，账号输入已锁定，请 %1 秒后重试").arg(m_lockRemainSec));
}

void LoginDialog::setError(const QString &text)
{
    m_lblError->setText(text);
    m_lblError->show();
}
