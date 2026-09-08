/**
 * @file    SerialManager.cpp
 * @brief   串口管理器实现
 *
 * 关键设计点：
 *  1. 打开失败时不在此处主动发 sigError —— QSerialPort::open() 失败时
 *     会同步触发 errorOccurred 信号，slotSerialError() 已经负责发错误消息，
 *     避免同一次失败弹出两条错误。
 *  2. ResourceError 表示设备被拔出（如拔掉 CH340 USB 线），
 *     此时必须主动 close 并通知上层，否则 QSerialPort 会处于僵尸状态。
 */

#include "SerialManager.h"

SerialManager::SerialManager(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))  // 串口对象挂在当前 QObject 树下，随父对象自动销毁
{
    connect(m_port, &QSerialPort::readyRead, this, &SerialManager::slotReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialManager::slotSerialError);
}

SerialManager::~SerialManager()
{
    if (m_port->isOpen())
        m_port->close();
}

bool SerialManager::isOpen() const
{
    return m_port->isOpen();
}

QString SerialManager::portName() const
{
    return m_port->portName();
}

SerialConfig SerialManager::config() const
{
    return m_config;
}

qint64 SerialManager::rxBytes() const
{
    return m_rxBytes;
}

qint64 SerialManager::txBytes() const
{
    return m_txBytes;
}

bool SerialManager::slotOpenPort(const SerialConfig &cfg)
{
    if (cfg.portName.isEmpty()) {
        emit sigError(QStringLiteral("未选择串口"));
        return false;
    }

    // 若已打开，先关闭再按新参数打开（支持运行中切换串口）
    if (m_port->isOpen())
        m_port->close();

    // 应用配置：端口名 / 波特率 / 数据位 / 停止位 / 校验位 / 流控
    m_port->setPortName(cfg.portName);
    m_port->setBaudRate(cfg.baudRate);
    m_port->setDataBits(cfg.dataBits);
    m_port->setStopBits(cfg.stopBits);
    m_port->setParity(cfg.parity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    // 以读写模式打开；失败时错误消息由 slotSerialError 发出
    if (!m_port->open(QIODevice::ReadWrite))
        return false;

    // 打开成功：记录配置、清零计数并通知上层
    m_config = cfg;
    m_rxBytes = 0;
    m_txBytes = 0;
    emit sigBytesChanged(0, 0);
    emit sigPortOpened(cfg);
    return true;
}

void SerialManager::slotClosePort()
{
    if (!m_port->isOpen())
        return;
    m_port->close();
    emit sigPortClosed();
}

bool SerialManager::slotSend(const QByteArray &data)
{
    if (!m_port->isOpen() || data.isEmpty())
        return false;

    const qint64 written = m_port->write(data);
    if (written < 0) {
        emit sigError(QStringLiteral("发送失败: %1").arg(m_port->errorString()));
        return false;
    }
    m_txBytes += written;
    emit sigBytesChanged(m_rxBytes, m_txBytes);
    return true;
}

void SerialManager::slotResetCounters()
{
    m_rxBytes = 0;
    m_txBytes = 0;
    emit sigBytesChanged(0, 0);
}

void SerialManager::slotReadyRead()
{
    // 一次读空缓冲区；注意串口流是字节流，不能假设"读一次 = 一帧"，
    // 粘包 / 断帧的切割由上层协议解析器（ProtocolEngine）负责
    const QByteArray data = m_port->readAll();
    if (data.isEmpty())
        return;
    m_rxBytes += data.size();
    emit sigBytesChanged(m_rxBytes, m_txBytes);
    emit sigDataReceived(data);
}

void SerialManager::slotSerialError(QSerialPort::SerialPortError error)
{
    // NoError 是正常状态（close() 成功也会触发一次），直接忽略
    if (error == QSerialPort::NoError)
        return;

    // 设备被拔出：主动关断并通知，防止串口对象卡死
    if (m_port->isOpen() && error == QSerialPort::ResourceError) {
        m_port->close();
        emit sigError(QStringLiteral("串口设备已断开，连接已关闭"));
        emit sigPortClosed();
        return;
    }

    // 其余错误（打开失败等）只上报错误消息
    emit sigError(m_port->errorString());
}
