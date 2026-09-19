/**
 * @file    ControlRouter.h
 * @brief   下行指令路由器 —— 核心服务层
 *
 * 职责：指令排队、发送、ACK 匹配、超时重发（500ms × 最多3次尝试）。
 *
 * 设计要点：
 *  1. 同一时刻只有一条指令在"等 ACK"，其余排队 —— 串口是半双工链路，
 *     并发发送会让应答无法与请求对应；
 *  2. ACK 只按指令码匹配（固件应答的参数是"实际状态"，可能与请求值不同，
 *     例如角度超限时固件回复的是它实际记录的值）；
 *  3. 不直接持有 SerialManager，通过 sigSendFrame 请求发送 —— 保持模块解耦。
 */
#ifndef CONTROLROUTER_H
#define CONTROLROUTER_H

#include <QObject>
#include <QQueue>

class QTimer;

class ControlRouter : public QObject
{
    Q_OBJECT

public:
    explicit ControlRouter(QObject *parent = nullptr);

    int pendingCount() const;   // 队列中待发送 + 正在等ACK的指令数

public slots:
    void slotSendCommand(quint8 cmd, quint8 param); // 入队一条指令
    void slotAckReceived(quint8 cmd, quint8 param); // 收到ACK（来自ProtocolEngine）
    void slotPortClosed();                          // 串口关闭：清空队列停止重发

signals:
    void sigSendFrame(const QByteArray &frame); // 请求串口发送2字节指令帧
    void sigAcked(quint8 cmd, quint8 param);    // 指令被下位机确认
    void sigTimeout(quint8 cmd);                // 重试耗尽仍无应答
    void sigRetry(quint8 cmd, int attempt);     // 发起第 attempt 次尝试
    void sigStatusText(const QString &text);    // 过程状态文本（给面板显示）

private slots:
    void slotTimeoutTick();  // 500ms 定时器触发：重发或放弃

private:
    struct Command {
        quint8 cmd;
        quint8 param;
    };

    void dispatchNext();  // 队列非空且空闲时发送下一条
    void transmit();      // 发送当前指令并启动超时定时器

    QQueue<Command> m_queue;      // 待发送指令队列
    Command m_current = {0, 0};   // 当前正在等ACK的指令
    bool m_waitingAck = false;    // 是否正在等待ACK
    int m_retryCount = 0;         // 当前指令已尝试次数（不含首发）
    QTimer *m_timer;              // 500ms 单发定时器

    static constexpr int kMaxRetries   = 3;    // 一条指令最多尝试次数（含首发）
    static constexpr int kAckTimeoutMs = 500;  // ACK 超时时间（与设计文档约定一致）
};

#endif // CONTROLROUTER_H
