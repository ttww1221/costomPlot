/**
 * @file    ProtocolEngine.h
 * @brief   协议解析引擎 —— 核心服务层
 *
 * 上行帧格式（13 字节，200ms/帧，来自 STM32 下位机）：
 *   [0] [1]       帧头 0xAA 0xBB
 *   [2..5]        温度 float（小端）
 *   [6..9]        湿度 float（小端）
 *   [10]          CRC-8-ATM（对 [2..9] 共 8 字节校验）
 *   [11] [12]     帧尾 0x0D 0x0A
 *
 * 关键设计 —— 帧同步状态机：
 *   串口是字节流，不能假设"读一次 = 一帧"。数据可能粘包（两帧连在一起）、
 *   断帧（一帧拆成多次到达）、混入噪声字节。状态机逐字节扫描，
 *   找到 0xAA 0xBB 后按固定长度收取载荷并校验帧尾；
 *   任何一步失配立即回到搜索态，且失配字节若是 0xAA 则作为新帧头重试，
 *   保证一帧不丢、坏帧计数丢弃。
 */

#ifndef PROTOCOLENGINE_H
#define PROTOCOLENGINE_H

#include <QDateTime>
#include <QObject>

/**
 * @brief 一帧解析后的传感器数据
 */
struct SensorData
{
    double temperature = 0.0;  // 温度 °C
    double humidity = 0.0;     // 湿度 %
    QDateTime timestamp;       // 接收时间
    qint64 frameIndex = 0;     // 有效帧序号（从 1 开始）
};

Q_DECLARE_METATYPE(SensorData)

/**
 * @brief 协议解析统计信息
 */
struct ProtoStats
{
    qint64 totalBytes = 0;    // 累计收到字节数
    qint64 totalFrames = 0;   // 累计收到帧数（含坏帧）
    qint64 validFrames = 0;   // 校验通过的有效帧数
    qint64 crcErrors = 0;     // CRC 校验失败帧数
    qint64 syncLosses = 0;    // 帧同步丢失次数（部分帧被噪声打断）
};

Q_DECLARE_METATYPE(ProtoStats)

class ProtocolEngine : public QObject
{
    Q_OBJECT

public:
    explicit ProtocolEngine(QObject *parent = nullptr);

    // 当前统计信息快照
    ProtoStats stats() const;

    // 复位所有统计计数
    void reset();

signals:
    // 一帧数据解析成功（CRC 通过）
    void sigFrameParsed(const SensorData &data);
    // 解析到一条 ACK 应答帧（BB + 指令码 + 参数）
    void sigAckReceived(quint8 cmd, quint8 param);
    // 统计信息变化（每收到一批字节触发一次）
    void sigStatsChanged(const ProtoStats &stats);

public slots:
    // 喂入原始串口字节流（由 SerialManager.sigDataReceived 驱动）
    void slotFeed(const QByteArray &data);

private:
    // 帧同步状态机的 7 个状态
    enum State {
        SeekAA,          // 搜索帧头第 1 字节 0xAA（或 ACK 帧头 0xBB）
        SeekBB,          // 已找到 0xAA，等待 0xBB
        CollectPayload,  // 收取 9 字节：温度4 + 湿度4 + CRC1
        SeekTail1,       // 等待帧尾 0x0D
        SeekTail2,       // 等待帧尾 0x0A
        AckCmd,          // ACK：等待指令码字节
        AckParam         // ACK：等待参数字节
    };

    // 处理一条完整候选帧：CRC 校验 + 解析温湿度
    void processFrame(const QByteArray &frame);
    // 失配处理：若当前字节是 0xAA 则直接作为新帧头（不丢帧），否则回搜索态
    void resync(uint8_t byte);
    // 发送统计变化信号
    void emitStats();

    QByteArray m_candidate;  // 当前候选帧缓冲区
    State m_state = SeekAA;  // 当前状态
    quint8 m_pendingAckCmd = 0; // ACK 解析暂存的指令码
    ProtoStats m_stats;      // 统计信息
};

#endif // PROTOCOLENGINE_H
