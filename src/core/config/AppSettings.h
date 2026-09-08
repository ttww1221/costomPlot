/**
 * @file    AppSettings.h
 * @brief   应用配置管理 —— 基础设施层
 *
 * 基于 QSettings（ini 格式）实现配置持久化：
 *  - 串口参数记忆（下次启动自动恢复上次的端口/波特率等）
 *  - 主窗口位置与大小记忆
 *
 * 配置文件位置：程序所在目录下的 config.ini
 */

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QByteArray>
#include "SerialManager.h"

class QSettings;

class AppSettings
{
public:
    // 读取 / 保存上次使用的串口配置
    static SerialConfig loadSerialConfig();
    static void saveSerialConfig(const SerialConfig &cfg);

    // 读取 / 保存主窗口几何信息（位置 + 大小）
    static QByteArray loadGeometry();
    static void saveGeometry(const QByteArray &geometry);

private:
    // 返回全局唯一的 QSettings 实例（惰性初始化，线程安全）
    static QSettings *settings();
};

#endif // APPSETTINGS_H
