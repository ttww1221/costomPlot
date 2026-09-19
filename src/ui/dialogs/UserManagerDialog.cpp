/**
 * @file    UserManagerDialog.cpp
 * @brief   用户管理对话框实现
 */

#include "UserManagerDialog.h"

#include "AuditService.h"
#include "Session.h"
#include "UserService.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QBrush>
#include <QColor>
#include <QVBoxLayout>

namespace {
// 表格列定义
enum Column { ColId = 0, ColName, ColDisplay, ColRole, ColState, ColCreated, ColLastLogin, ColCount };
}

// ---------------------------------------------------------------------------
// 新增用户子对话框（就地构建，不单独开文件）
// ---------------------------------------------------------------------------
namespace {

struct NewUserInput
{
    QString username;
    QString password;
    QString displayName;
    UserRole role = UserRole::Guest;
};

bool askNewUser(QWidget *parent, NewUserInput *out)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("新增用户"));
    dlg.setModal(true);

    QLineEdit *name = new QLineEdit;
    name->setPlaceholderText(QStringLiteral("3~20 位字母/数字/下划线"));
    QLineEdit *disp = new QLineEdit;
    QLineEdit *pwd = new QLineEdit;
    pwd->setEchoMode(QLineEdit::Password);
    QLineEdit *pwd2 = new QLineEdit;
    pwd2->setEchoMode(QLineEdit::Password);
    QComboBox *role = new QComboBox;
    role->addItem(QStringLiteral("管理员"), int(UserRole::Admin));
    role->addItem(QStringLiteral("操作员"), int(UserRole::Operator));
    role->addItem(QStringLiteral("访客"), int(UserRole::Guest));
    role->setCurrentIndex(1);   // 默认操作员，最小权限原则

    QLabel *err = new QLabel;
    err->setStyleSheet(QStringLiteral("color: #c0392b;"));
    err->hide();

    QFormLayout *form = new QFormLayout;
    form->addRow(QStringLiteral("用户名:"), name);
    form->addRow(QStringLiteral("显示名:"), disp);
    form->addRow(QStringLiteral("密码:"), pwd);
    form->addRow(QStringLiteral("确认密码:"), pwd2);
    form->addRow(QStringLiteral("角色:"), role);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(err);
    lay->addWidget(box);
    dlg.connect(box, &QDialogButtonBox::accepted, &dlg, [name, pwd, pwd2, err, &dlg]() {
        // 两次口令一致才关闭，错误就地提示，避免反复弹窗
        if (pwd->text() != pwd2->text()) {
            err->setText(QStringLiteral("两次输入的密码不一致"));
            err->show();
            return;
        }
        dlg.accept();
    });
    dlg.connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    out->username = name->text().trimmed();
    out->displayName = disp->text().trimmed();
    out->password = pwd->text();
    out->role = UserRole(role->currentData().toInt());
    return true;
}

bool askPassword(QWidget *parent, const QString &title, QString *out)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setModal(true);

    QLineEdit *pwd = new QLineEdit;
    pwd->setEchoMode(QLineEdit::Password);
    QLineEdit *pwd2 = new QLineEdit;
    pwd2->setEchoMode(QLineEdit::Password);
    QLabel *err = new QLabel;
    err->setStyleSheet(QStringLiteral("color: #c0392b;"));
    err->hide();

    QFormLayout *form = new QFormLayout;
    form->addRow(QStringLiteral("新密码:"), pwd);
    form->addRow(QStringLiteral("确认新密码:"), pwd2);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(err);
    lay->addWidget(box);
    dlg.connect(box, &QDialogButtonBox::accepted, &dlg, [pwd, pwd2, err, &dlg]() {
        if (pwd->text() != pwd2->text()) {
            err->setText(QStringLiteral("两次输入的密码不一致"));
            err->show();
            return;
        }
        dlg.accept();
    });
    dlg.connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;
    *out = pwd->text();
    return true;
}

}

// ---------------------------------------------------------------------------
// UserManagerDialog
// ---------------------------------------------------------------------------

UserManagerDialog::UserManagerDialog(UserService *users, AuditService *audit, Session *session,
                                     QWidget *parent)
    : QDialog(parent)
    , m_users(users)
    , m_audit(audit)
    , m_session(session)
{
    setupUi();
    fillTable();
}

void UserManagerDialog::setupUi()
{
    setWindowTitle(QStringLiteral("用户管理"));
    setMinimumSize(720, 420);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("用户名"), QStringLiteral("显示名"),
                                        QStringLiteral("角色"), QStringLiteral("状态"), QStringLiteral("创建时间"),
                                        QStringLiteral("最后登录")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnHidden(ColId, true);   // ID 只作数据载体，不展示

    m_btnAdd = new QPushButton(QStringLiteral("新增用户"));
    m_btnDelete = new QPushButton(QStringLiteral("删除"));
    m_btnPassword = new QPushButton(QStringLiteral("重置密码"));
    m_btnEnabled = new QPushButton(QStringLiteral("启用/禁用"));

    m_cmbRole = new QComboBox;
    m_cmbRole->addItem(QStringLiteral("管理员"), int(UserRole::Admin));
    m_cmbRole->addItem(QStringLiteral("操作员"), int(UserRole::Operator));
    m_cmbRole->addItem(QStringLiteral("访客"), int(UserRole::Guest));
    m_btnApplyRole = new QPushButton(QStringLiteral("修改角色"));

    QPushButton *btnRefresh = new QPushButton(QStringLiteral("刷新"));
    QPushButton *btnClose = new QPushButton(QStringLiteral("关闭"));

    QHBoxLayout *bar = new QHBoxLayout;
    bar->addWidget(m_btnAdd);
    bar->addWidget(m_btnDelete);
    bar->addWidget(m_btnPassword);
    bar->addWidget(m_btnEnabled);
    bar->addSpacing(16);
    bar->addWidget(new QLabel(QStringLiteral("角色:")));
    bar->addWidget(m_cmbRole);
    bar->addWidget(m_btnApplyRole);
    bar->addStretch(1);
    bar->addWidget(btnRefresh);
    bar->addWidget(btnClose);

    m_lblStatus = new QLabel(QStringLiteral("就绪"));
    m_lblStatus->setStyleSheet(QStringLiteral("color: #7f8c8d;"));

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(m_table, 1);
    main->addLayout(bar);
    main->addWidget(m_lblStatus);

    connect(m_btnAdd, &QPushButton::clicked, this, &UserManagerDialog::slotAdd);
    connect(m_btnDelete, &QPushButton::clicked, this, &UserManagerDialog::slotDelete);
    connect(m_btnPassword, &QPushButton::clicked, this, &UserManagerDialog::slotChangePassword);
    connect(m_btnEnabled, &QPushButton::clicked, this, &UserManagerDialog::slotToggleEnabled);
    connect(m_btnApplyRole, &QPushButton::clicked, this, &UserManagerDialog::slotApplyRole);
    connect(btnRefresh, &QPushButton::clicked, this, &UserManagerDialog::slotRefresh);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &UserManagerDialog::slotSelectionChanged);

    updateButtons();
}

void UserManagerDialog::fillTable()
{
    const QList<UserInfo> users = m_users->listUsers();
    m_table->setRowCount(users.size());

    for (int r = 0; r < users.size(); ++r) {
        const UserInfo &u = users.at(r);
        auto *item = new QTableWidgetItem(QString::number(u.id));
        item->setData(Qt::UserRole, u.id);
        m_table->setItem(r, ColId, item);
        m_table->setItem(r, ColName, new QTableWidgetItem(u.username));
        m_table->setItem(r, ColDisplay, new QTableWidgetItem(u.displayName));
        m_table->setItem(r, ColRole, new QTableWidgetItem(UserText::role(u.role)));

        QTableWidgetItem *state = new QTableWidgetItem(u.enabled ? QStringLiteral("启用") : QStringLiteral("禁用"));
        state->setForeground(u.enabled ? QBrush(QColor(0x27, 0xae, 0x60)) : QBrush(QColor(0xc0, 0x39, 0x2b)));
        m_table->setItem(r, ColState, state);

        m_table->setItem(r, ColCreated,
                         new QTableWidgetItem(u.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
        m_table->setItem(r, ColLastLogin,
                         new QTableWidgetItem(u.lastLogin.isValid()
                                                    ? u.lastLogin.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
                                                    : QStringLiteral("从未登录")));
    }
    m_table->resizeColumnsToContents();
}

void UserManagerDialog::slotRefresh()
{
    fillTable();
    setStatus(QStringLiteral("已刷新，共 %1 个账号").arg(m_table->rowCount()));
    updateButtons();
}

UserInfo UserManagerDialog::selectedUser() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return UserInfo();
    const QTableWidgetItem *idItem = m_table->item(row, ColId);
    if (!idItem)
        return UserInfo();
    return m_users->findById(idItem->data(Qt::UserRole).toInt());
}

void UserManagerDialog::slotSelectionChanged()
{
    updateButtons();
}

void UserManagerDialog::updateButtons()
{
    const UserInfo u = selectedUser();
    const bool has = u.isValid();
    const bool isSelf = has && m_session && u.id == m_session->userId();

    m_btnDelete->setEnabled(has && !isSelf);
    m_btnPassword->setEnabled(has);
    m_btnEnabled->setEnabled(has && !isSelf);   // 不允许把自己锁在门外
    m_btnApplyRole->setEnabled(has && !isSelf);

    if (has) {
        // 角色下拉框同步为所选用户当前角色，避免误改
        const int idx = m_cmbRole->findData(int(u.role));
        if (idx >= 0)
            m_cmbRole->setCurrentIndex(idx);
    }
}

void UserManagerDialog::setStatus(const QString &text, bool error)
{
    m_lblStatus->setText(text);
    m_lblStatus->setStyleSheet(error ? QStringLiteral("color: #c0392b;")
                                     : QStringLiteral("color: #7f8c8d;"));
}

void UserManagerDialog::slotAdd()
{
    NewUserInput in;
    if (!askNewUser(this, &in))
        return;

    QString err;
    if (!m_users->createUser(in.username, in.password, in.role, in.displayName, &err)) {
        setStatus(err, true);
        QMessageBox::warning(this, QStringLiteral("新增失败"), err);
        return;
    }

    if (m_audit)
        m_audit->log(QLatin1String(AuditAction::kUserCreate),
                     QStringLiteral("用户=%1 角色=%2").arg(in.username, UserText::role(in.role)));
    setStatus(QStringLiteral("已创建用户 %1（%2）").arg(in.username, UserText::role(in.role)));
    fillTable();
    updateButtons();
}

void UserManagerDialog::slotDelete()
{
    const UserInfo u = selectedUser();
    if (!u.isValid())
        return;

    if (QMessageBox::question(this, QStringLiteral("确认删除"),
                              QStringLiteral("确定删除用户 %1 吗？该操作不可撤销。").arg(u.username))
        != QMessageBox::Yes)
        return;

    QString err;
    if (!m_users->removeUser(u.id, m_session ? m_session->userId() : -1, &err)) {
        setStatus(err, true);
        QMessageBox::warning(this, QStringLiteral("删除失败"), err);
        return;
    }

    if (m_audit)
        m_audit->log(QLatin1String(AuditAction::kUserDelete), QStringLiteral("用户=%1").arg(u.username));
    setStatus(QStringLiteral("已删除用户 %1").arg(u.username));
    fillTable();
    updateButtons();
}

void UserManagerDialog::slotChangePassword()
{
    const UserInfo u = selectedUser();
    if (!u.isValid())
        return;

    QString pwd;
    if (!askPassword(this, QStringLiteral("重置 %1 的密码").arg(u.username), &pwd))
        return;

    QString err;
    if (!m_users->changePassword(u.id, pwd, &err)) {
        setStatus(err, true);
        QMessageBox::warning(this, QStringLiteral("修改失败"), err);
        return;
    }

    if (m_audit)
        m_audit->log(QLatin1String(AuditAction::kUserPassword), QStringLiteral("用户=%1").arg(u.username));
    setStatus(QStringLiteral("已重置 %1 的密码").arg(u.username));
}

void UserManagerDialog::slotToggleEnabled()
{
    const UserInfo u = selectedUser();
    if (!u.isValid())
        return;

    QString err;
    if (!m_users->setEnabled(u.id, !u.enabled, &err)) {
        setStatus(err, true);
        QMessageBox::warning(this, QStringLiteral("操作失败"), err);
        return;
    }

    if (m_audit)
        m_audit->log(QStringLiteral("USER_ENABLE"),
                     QStringLiteral("用户=%1 状态=%2").arg(u.username, !u.enabled ? QStringLiteral("启用")
                                                                                : QStringLiteral("禁用")));
    setStatus(QStringLiteral("用户 %1 已%2").arg(u.username, !u.enabled ? QStringLiteral("启用")
                                                                       : QStringLiteral("禁用")));
    fillTable();
    updateButtons();
}

void UserManagerDialog::slotApplyRole()
{
    const UserInfo u = selectedUser();
    if (!u.isValid())
        return;

    const UserRole role = UserRole(m_cmbRole->currentData().toInt());
    if (role == u.role) {
        setStatus(QStringLiteral("角色未变化"));
        return;
    }

    QString err;
    if (!m_users->setRole(u.id, role, &err)) {
        setStatus(err, true);
        QMessageBox::warning(this, QStringLiteral("操作失败"), err);
        return;
    }

    if (m_audit)
        m_audit->log(QStringLiteral("USER_ROLE"),
                     QStringLiteral("用户=%1 %2→%3").arg(u.username, UserText::role(u.role), UserText::role(role)));
    setStatus(QStringLiteral("用户 %1 的角色已改为%2").arg(u.username, UserText::role(role)));

    // 改的是当前登录者自己的角色时（理论上按钮已禁用，这里做双保险），同步会话
    if (m_session && u.id == m_session->userId())
        m_session->updateUser(m_users->findById(u.id));

    fillTable();
    updateButtons();
}
