/**
 * @file    AppSettings.cpp
 * @brief   应用配置管理实现
 */

#include "AppSettings.h"

#include <QCoreApplication>
#include <QSettings>

QSettings *AppSettings::settings()
{
    // 静态局部变量：首次调用时初始化，程序退出时自动析构并落盘
    // 路径取程序所在目录，保证"绿色"运行（不污染注册表/用户目录）
    static QSettings instance(QCoreApplication::applicationDirPath() + QStringLiteral("/config.ini"),
                              QSettings::IniFormat);
    return &instance;
}

SerialConfig AppSettings::loadSerialConfig()
{
    QSettings *s = settings();
    SerialConfig cfg;
    cfg.portName = s->value(QStringLiteral("serial/port")).toString();
    cfg.baudRate = s->value(QStringLiteral("serial/baud"), 115200).toInt();
    cfg.dataBits = static_cast<QSerialPort::DataBits>(
        s->value(QStringLiteral("serial/dataBits"), QSerialPort::Data8).toInt());
    cfg.stopBits = static_cast<QSerialPort::StopBits>(
        s->value(QStringLiteral("serial/stopBits"), QSerialPort::OneStop).toInt());
    cfg.parity = static_cast<QSerialPort::Parity>(
        s->value(QStringLiteral("serial/parity"), QSerialPort::NoParity).toInt());
    return cfg;
}

void AppSettings::saveSerialConfig(const SerialConfig &cfg)
{
    QSettings *s = settings();
    s->setValue(QStringLiteral("serial/port"), cfg.portName);
    s->setValue(QStringLiteral("serial/baud"), cfg.baudRate);
    // 枚举按 int 存储，读取时再转回枚举类型
    s->setValue(QStringLiteral("serial/dataBits"), static_cast<int>(cfg.dataBits));
    s->setValue(QStringLiteral("serial/stopBits"), static_cast<int>(cfg.stopBits));
    s->setValue(QStringLiteral("serial/parity"), static_cast<int>(cfg.parity));
    s->sync();  // 立即写入磁盘，防止程序异常退出丢配置
}

QByteArray AppSettings::loadGeometry()
{
    return settings()->value(QStringLiteral("window/geometry")).toByteArray();
}

void AppSettings::saveGeometry(const QByteArray &geometry)
{
    settings()->setValue(QStringLiteral("window/geometry"), geometry);
    settings()->sync();
}
