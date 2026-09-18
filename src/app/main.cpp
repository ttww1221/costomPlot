/**
 * @file    main.cpp
 * @brief   程序入口
 *
 * 启动顺序（S8 起加入登录门禁）：
 *  1. 创建 QApplication，应用 Fusion 风格（跨平台观感一致）；
 *  2. 打开 SQLite 数据库（首次运行自动建库建表），失败则直接退出——
 *     没有数据库就无法鉴权与审计，宁可启动失败也不进入"无权限管控"状态；
 *  3. 库为空时植入默认管理员 admin/admin123（登录框会给出提示）；
 *  4. 弹出登录框，认证通过才创建主窗口；用户取消即退出程序。
 *     这样"未登录不可用"是结构性保证，而非仅靠界面置灰约束。
 *
 * 自检模式 --selftest：
 *  不弹登录框，改用内存数据库 + 合成的管理员会话构建主窗口，
 *  以 offscreen 平台插件跑 2 秒事件循环后退出（返回 0 表示界面装配无误）。
 *  用于装机自检与回归验证：能在无人值守、无显示器的环境下确认
 *  "数据库建表 → 会话建立 → 全部面板构造 → 信号接线"整条链路不崩溃。
 */

#include "MainWindow.h"

#include "AuditService.h"
#include "Database.h"
#include "LoginDialog.h"
#include "Session.h"
#include "UserService.h"

#include <QApplication>
#include <QMessageBox>
#include <QTimer>

namespace {

constexpr int kSelfTestRunMs = 2000;

// 命令行里是否带 --selftest（必须在 QApplication 构造前判断，
// 因为 QT_QPA_PLATFORM 只有在其之前设置才生效）
bool hasSelfTestFlag(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0)
            return true;
    }
    return false;
}

/**
 * @brief 自检：内存库 + 合成会话，构建主窗口并跑一小段事件循环
 * @return 0 表示全部装配成功
 */
int runSelfTest(QApplication &app)
{
    Database db;
    if (!db.open(QStringLiteral(":memory:")))
        return 11;       // 数据库打不开

    UserService users(&db);
    AuditService audit(&db);
    if (!users.ensureDefaultAdmin())
        return 12;       // 默认管理员植入失败

    const UserInfo admin = users.findByName(QStringLiteral("admin"));
    if (!admin.isValid())
        return 13;

    Session session;
    session.login(admin);
    audit.log(QStringLiteral("SELFTEST"), QStringLiteral("自检模式启动"));

    MainWindow window(&session, &db);
    window.show();

    // 跑一段事件循环：让所有 1s 定时器（统计结算、帧率、运行时间）至少触发一次
    QTimer::singleShot(kSelfTestRunMs, &app, &QCoreApplication::quit);
    const int rc = app.exec();

    audit.log(QStringLiteral("SELFTEST"), QStringLiteral("自检模式结束"));
    db.close();
    return rc;
}

}

int main(int argc, char *argv[])
{
    const bool selfTest = hasSelfTestFlag(argc, argv);
    if (selfTest)
        qputenv("QT_QPA_PLATFORM", "offscreen");   // 无显示器环境下也能构建界面

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SerialDebugger"));
    QApplication::setOrganizationName(QStringLiteral("HBPU-AI"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    if (selfTest)
        return runSelfTest(app);

    // ---- 1. 数据库 ----
    Database db;
    if (!db.open()) {
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                              QStringLiteral("无法打开数据库，程序即将退出。\n\n%1")
                                  .arg(db.lastError()));
        return 1;
    }

    UserService users(&db);
    AuditService audit(&db);

    // ---- 2. 首次运行植入默认管理员 ----
    if (users.ensureDefaultAdmin())
        audit.log(QStringLiteral("SYSTEM_INIT"), QStringLiteral("创建默认管理员 admin"));

    // ---- 3. 登录门禁 ----
    Session session;
    {
        LoginDialog login(&users, &audit, &session);
        if (login.exec() != QDialog::Accepted)
            return 0;   // 用户取消登录：不进入主界面
    }

    // ---- 4. 主窗口 ----
    MainWindow window(&session, &db);
    window.show();

    const int rc = app.exec();
    db.close();
    return rc;
}
