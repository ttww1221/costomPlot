/**
 * @file    SerialManager.h
 * @brief   串口管理器 —— 核心服务层
 *
 * 对 QSerialPort 做一层封装，向上提供：
 *  - 串口的打开 / 关闭 / 数据发送
 *  - RX / TX 字节计数
 *  - 设备意外拔出（ResourceError）时的自动关断与通知
 *
 * 所有对外交互均通过信号完成，UI 层不直接接触 QSerialPort，
 * 与 PocketAPPV3 中 SerialDrive 的定位一致。
 */

#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H

#include <QObject>
#include <QSerialPort>

/**
 * @brief 串口配置参数结构体
 *
 * 封装打开串口所需的全部参数，在串口面板与串口管理器之间传递。
 */
struct SerialConfig
{
    QString portName;                                     // 串口名，如 "COM3"
    qint32 baudRate = 115200;                             // 波特率，默认 115200
    QSerialPort::DataBits dataBits = QSerialPort::Data8;  // 数据位
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;// 停止位
    QSerialPort::Parity parity = QSerialPort::NoParity;   // 校验位
};

// 注册元类型：允许 SerialConfig 作为信号参数传递（含跨线程队列连接）
Q_DECLARE_METATYPE(SerialConfig)

/**
 * @class SerialManager
 * @brief 串口管理器
 */
class SerialManager : public QObject
{
    Q_OBJECT

public:
    explicit SerialManager(QObject *parent = nullptr);
    ~SerialManager() override;

    bool isOpen() const;          // 串口是否已打开
    QString portName() const;     // 当前串口名
    SerialConfig config() const;  // 当前生效的串口配置
    qint64 rxBytes() const;       // 累计接收字节数
    qint64 txBytes() const;       // 累计发送字节数

public slots:
    bool slotOpenPort(const SerialConfig &cfg);  // 按配置打开串口，失败返回 false
    void slotClosePort();                        // 关闭串口
    bool slotSend(const QByteArray &data);       // 发送数据，失败返回 false
    void slotResetCounters();                    // 收发字节计数清零

signals:
    void sigPortOpened(const SerialConfig &cfg);          // 串口打开成功
    void sigPortClosed();                                 // 串口已关闭
    void sigDataReceived(const QByteArray &data);         // 收到新数据（每次 readyRead 发一块）
    void sigBytesChanged(qint64 rxBytes, qint64 txBytes); // 收发计数变化
    void sigError(const QString &msg);                    // 错误消息

private slots:
    void slotReadyRead();                                    // 读取缓冲数据并转发
    void slotSerialError(QSerialPort::SerialPortError error);// 串口错误处理

private:
    QSerialPort *m_port;     // 底层串口对象
    SerialConfig m_config;   // 当前生效的配置
    qint64 m_rxBytes = 0;    // 接收字节计数
    qint64 m_txBytes = 0;    // 发送字节计数
};

#endif // SERIALMANAGER_H
