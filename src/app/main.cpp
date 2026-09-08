/**
 * @file    main.cpp
 * @brief   程序入口
 *
 * 创建 QApplication，应用 Fusion 风格（跨平台观感一致），
 * 显示主窗口并进入事件循环。
 */

#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SerialDebugger"));
    QApplication::setOrganizationName(QStringLiteral("HBPU-AI"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    MainWindow window;
    window.show();

    return app.exec();
}
