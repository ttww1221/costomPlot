/**
 * @file    AppSettings.h
 * @brief   应用配置管理 —— 基础设施层
 *
 * 基于 QSettings（ini 格式）实现配置持久化：
 *  - 串口参数记忆（下次启动自动恢复上次的端口/波特率等）
 *  - 主窗口位置与大小记忆
 *  - 报警阈值记忆（S8）
 *  - 上次登录用户名记忆（S8，只记用户名不记口令）
 *
 * 配置文件位置：程序所在目录下的 config.ini
 */

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QByteArray>
#include <QString>

#include "AlarmEngine.h"
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

    // 读取 / 保存报警阈值；库中无记录时返回 AlarmThresholds 的默认值
    static AlarmThresholds loadAlarmThresholds();
    static void saveAlarmThresholds(const AlarmThresholds &t);

    // 读取 / 保存上次登录的用户名（便于登录框预填，不涉及口令）
    static QString loadLastUserName();
    static void saveLastUserName(const QString &username);

private:
    // 返回全局唯一的 QSettings 实例（惰性初始化，线程安全）
    static QSettings *settings();
};

#endif // APPSETTINGS_H
