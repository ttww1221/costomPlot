/**
 * @file    ProtocolEngine.cpp
 * @brief   协议解析引擎实现
 *
 * 状态机流转图（每个字节触发一次状态跳转）：
 *
 *  SeekAA ──0xAA──► SeekBB ──0xBB──► CollectPayload ──收满9字节──► SeekTail1
 *    ▲                 │                                                  │
 *    │非0xAA           ├─0xAA(重试新帧头)                                   ├─0x0D─► SeekTail2
 *    └─────(忽略)──────┴─其他(失步+1)─► SeekAA ◄──(失步+1)─────────────────┤
 *                                        ▲                                 │0x0A─► 校验+解析
 *                                        └──────────────────────────────────┘
 *
 * 处理规则：
 *  1. 噪声字节（未形成候选帧时）直接忽略，不计数；
 *  2. 候选帧中途失配 → 失步计数 +1，若该字节恰为 0xAA 则立即开始新候选帧；
 *  3. 帧尾匹配完成后做 CRC 校验：失败仅丢弃并计数，不影响后续帧同步。
 */

#include "ProtocolEngine.h"

#include "Crc8.h"

#include <QtEndian>

#include <cstring>

namespace {
// 帧格式常量（与下位机固件协议保持一致）
constexpr uint8_t kHead1 = 0xAA;  // 帧头 1
constexpr uint8_t kHead2 = 0xBB;  // 帧头 2
constexpr uint8_t kTail1 = 0x0D;  // 帧尾 1
constexpr uint8_t kTail2 = 0x0A;  // 帧尾 2
constexpr int kPayloadLen = 9;    // 温度4 + 湿度4 + CRC1
constexpr int kFrameLen = 13;     // 2 + 4 + 4 + 1 + 2
}

ProtocolEngine::ProtocolEngine(QObject *parent)
    : QObject(parent)
{
}

ProtoStats ProtocolEngine::stats() const
{
    return m_stats;
}

void ProtocolEngine::reset()
{
    m_stats = ProtoStats();
    m_state = SeekAA;
    m_candidate.clear();
}

void ProtocolEngine::slotFeed(const QByteArray &data)
{
    m_stats.totalBytes += data.size();
    bool statsDirty = false;  // 失步计数变化标志（减少无意义的信号发送）

    // 逐字节驱动状态机
    for (const char ch : data) {
        const uint8_t b = static_cast<uint8_t>(ch);

        switch (m_state) {
        case SeekAA:
            // 找到 0xAA 才开始组帧；其余噪声字节直接丢弃
            if (b == kHead1) {
                m_candidate.clear();
                m_candidate.append(static_cast<char>(b));
                m_state = SeekBB;
            }
            break;

        case SeekBB:
            if (b == kHead2) {
                // 帧头完整，开始收载荷
                m_candidate.append(static_cast<char>(b));
                m_state = CollectPayload;
            } else if (b == kHead1) {
                // 连续的 0xAA 0xAA：后一个 0xAA 可能是新帧头
                m_candidate.clear();
                m_candidate.append(static_cast<char>(b));
                ++m_stats.syncLosses;
                statsDirty = true;
            } else {
                // 帧头被破坏，回到搜索态
                m_state = SeekAA;
                ++m_stats.syncLosses;
                statsDirty = true;
            }
            break;

        case CollectPayload:
            // 无脑收取 9 字节（载荷里的 0xAA/0xBB 不会干扰，因为按长度计数）
            m_candidate.append(static_cast<char>(b));
            if (m_candidate.size() == 2 + kPayloadLen)
                m_state = SeekTail1;
            break;

        case SeekTail1:
            if (b == kTail1) {
                m_candidate.append(static_cast<char>(b));
                m_state = SeekTail2;
            } else {
                resync(b);
                ++m_stats.syncLosses;
                statsDirty = true;
            }
            break;

        case SeekTail2:
            if (b == kTail2) {
                // 帧尾完整：校验 + 解析，然后回到搜索态等下一帧
                m_candidate.append(static_cast<char>(b));
                processFrame(m_candidate);
                m_state = SeekAA;
                m_candidate.clear();
            } else {
                resync(b);
                ++m_stats.syncLosses;
                statsDirty = true;
            }
            break;
        }
    }

    // 每批数据结束都上报一次统计（数据面板刷新成本很低）
    if (statsDirty || !data.isEmpty())
        emitStats();
}

void ProtocolEngine::processFrame(const QByteArray &frame)
{
    ++m_stats.totalFrames;

    // CRC 校验：对 [2..9] 共 8 字节（温度+湿度），失败即丢弃
    const uint8_t crc = Crc8::compute(reinterpret_cast<const uint8_t *>(frame.constData() + 2), 8);
    if (crc != static_cast<uint8_t>(frame.at(10))) {
        ++m_stats.crcErrors;
        return;
    }

    // 小端序 float 解码（qFromLittleEndian 保证大端平台上同样正确），
    // 先还原为 float 再隐式提升为 double，不能直接把 4 字节 memcpy 进 double
    SensorData data;
    const quint32 tempBits = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(frame.constData() + 2));
    const quint32 humBits = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(frame.constData() + 6));
    float temp = 0.0f;
    float hum = 0.0f;
    std::memcpy(&temp, &tempBits, sizeof(temp));
    std::memcpy(&hum, &humBits, sizeof(hum));
    data.temperature = temp;
    data.humidity = hum;

    data.timestamp = QDateTime::currentDateTime();
    data.frameIndex = ++m_stats.validFrames;

    emit sigFrameParsed(data);
}

void ProtocolEngine::resync(uint8_t byte)
{
    // 失配字节若是 0xAA：直接当作新帧头，避免"两帧粘连时丢第二帧"
    if (byte == kHead1) {
        m_candidate.clear();
        m_candidate.append(static_cast<char>(byte));
        m_state = SeekBB;
    } else {
        m_state = SeekAA;
        m_candidate.clear();
    }
}

void ProtocolEngine::emitStats()
{
    emit sigStatsChanged(m_stats);
}
