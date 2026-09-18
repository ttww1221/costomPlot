/**
 * @file    ControlRouter.cpp
 * @brief   下行指令路由器实现
 *
 * 指令生命周期：
 *   入队 → dispatchNext 取出 → transmit 发送+启动500ms定时器
 *     ├─ 收到匹配ACK → sigAcked → 处理下一条
 *     └─ 超时 → 重发（最多3次尝试）→ 仍失败则 sigTimeout → 处理下一条
 */
#include "ControlRouter.h"
#include "ProtocolDefs.h"

#include <QTimer>

namespace {
// 指令码 → 中文名称（状态提示用）
QString commandName(quint8 cmd)
{
    switch (cmd) {
    case Proto::CMD_MOTOR_ANGLE: return QStringLiteral("仪表角度");
    case Proto::CMD_MOTOR_HOME:  return QStringLiteral("仪表回零");
    case Proto::CMD_FAN_CTRL:    return QStringLiteral("风扇开关");
    case Proto::CMD_AUTO_MODE:   return QStringLiteral("模式设置");
    case Proto::CMD_HEARTBEAT:   return QStringLiteral("心跳查询");
    default: return QStringLiteral("指令0x%1").arg(cmd, 2, 16, QLatin1Char('0'));
    }
}
}

ControlRouter::ControlRouter(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);   // 单发模式：每次发送后重新 start
    connect(m_timer, &QTimer::timeout, this, &ControlRouter::slotTimeoutTick);
}

int ControlRouter::pendingCount() const
{
    return m_queue.size() + (m_waitingAck ? 1 : 0);
}

void ControlRouter::slotSendCommand(quint8 cmd, quint8 param)
{
    m_queue.enqueue(Command{cmd, param});
    dispatchNext();   // 若当前空闲立即发送，否则排队等待
}

void ControlRouter::dispatchNext()
{
    if (m_waitingAck)        // 上一条还没确认，保持排队
        return;
    if (m_queue.isEmpty())
        return;

    m_current = m_queue.dequeue();
    m_retryCount = 0;
    transmit();
}

void ControlRouter::transmit()
{
    m_waitingAck = true;

    // 组 2 字节指令帧：[指令码][参数]
    QByteArray frame;
    frame.append(static_cast<char>(m_current.cmd));
    frame.append(static_cast<char>(m_current.param));
    emit sigSendFrame(frame);

    m_timer->start(kAckTimeoutMs);   // 启动500ms ACK超时
    emit sigStatusText(QStringLiteral("%1 已发送(参数 %2)，等待ACK...")
                           .arg(commandName(m_current.cmd)).arg(m_current.param));
}

void ControlRouter::slotAckReceived(quint8 cmd, quint8 param)
{
    if (!m_waitingAck)          // 没有在途指令（迟到的ACK/手动心跳应答）：忽略
        return;
    if (cmd != m_current.cmd)   // 指令码不匹配：忽略，继续等待
        return;

    m_timer->stop();
    m_waitingAck = false;
    emit sigAcked(cmd, param);
    emit sigStatusText(QStringLiteral("%1 已确认 ✓ (返回值 %2)")
                           .arg(commandName(cmd)).arg(param));
    dispatchNext();             // 立即处理队列中的下一条
}

void ControlRouter::slotTimeoutTick()
{
    if (!m_waitingAck)
        return;

    m_retryCount++;
    if (m_retryCount >= kMaxRetries) {
        // 尝试次数耗尽：放弃该指令，继续处理队列（不阻塞后续指令）
        m_waitingAck = false;
        emit sigTimeout(m_current.cmd);
        emit sigStatusText(QStringLiteral("%1 超时未应答（已尝试%2次）✗")
                               .arg(commandName(m_current.cmd)).arg(kMaxRetries));
        dispatchNext();
    } else {
        // 还有重试机会：原参数重发
        emit sigRetry(m_current.cmd, m_retryCount + 1);
        emit sigStatusText(QStringLiteral("%1 超时，第 %2/%3 次尝试...")
                               .arg(commandName(m_current.cmd))
                               .arg(m_retryCount + 1).arg(kMaxRetries));
        transmit();
    }
}

void ControlRouter::slotPortClosed()
{
    m_timer->stop();
    m_queue.clear();
    m_waitingAck = false;
    emit sigStatusText(QStringLiteral("串口已关闭，指令队列已清空"));
}
