/**
 * @file    StatEngine.cpp
 * @brief   统计分析引擎实现
 */

#include "StatEngine.h"

#include <QTimer>

#include <cmath>

namespace {
constexpr int kTickIntervalMs = 1000;   // 结算周期：1s 滑动窗口
}

StatEngine::StatEngine(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &StatEngine::slotTick);
    m_timer->start();
}

// ---------------------------------------------------------------------------
// Welford 在线统计
// ---------------------------------------------------------------------------

void StatEngine::push(Accum &a, double x, const QDateTime &t)
{
    ++a.count;

    // 先更新均值，再用"新均值"求第二个增量，这是 Welford 的数值稳定关键
    const double delta = x - a.mean;
    a.mean += delta / double(a.count);
    a.m2 += delta * (x - a.mean);

    if (a.count == 1) {
        a.min = a.max = x;
        a.minTime = a.maxTime = t;
    } else {
        if (x < a.min) { a.min = x; a.minTime = t; }
        if (x > a.max) { a.max = x; a.maxTime = t; }
    }
    a.last = x;

    // 直方图样本有上限，超限只保留统计量，内存不再增长
    if (a.values.size() < kMaxHistSamples)
        a.values.append(x);
}

ChannelStat StatEngine::toStat(const Accum &a)
{
    ChannelStat s;
    s.count = a.count;
    s.last = a.last;
    s.min = a.min;
    s.max = a.max;
    s.mean = a.mean;
    s.minTime = a.minTime;
    s.maxTime = a.maxTime;
    // 样本标准差用 n-1（贝塞尔校正）；单样本时无离散度可言
    s.stddev = (a.count > 1) ? std::sqrt(a.m2 / double(a.count - 1)) : 0.0;
    return s;
}

const StatEngine::Accum &StatEngine::accum(StatChannel ch) const
{
    return (ch == StatChannel::Temperature) ? m_temp : m_hum;
}

// ---------------------------------------------------------------------------
// 数据入口
// ---------------------------------------------------------------------------

void StatEngine::slotSample(const SensorData &data)
{
    push(m_temp, data.temperature, data.timestamp);
    push(m_hum, data.humidity, data.timestamp);
    ++m_framesInWindow;
}

void StatEngine::slotProtoStats(const ProtoStats &stats)
{
    m_proto = stats;
}

void StatEngine::slotBytesChanged(qint64 rxBytes, qint64 txBytes)
{
    m_rxTotal = rxBytes;
    m_txTotal = txBytes;
}

void StatEngine::slotReset()
{
    m_temp = Accum();
    m_hum = Accum();
    m_proto = ProtoStats();
    m_rxTotal = m_txTotal = 0;
    m_rxLast = m_txLast = 0;
    m_framesInWindow = 0;
    m_rxBps = m_txBps = m_frameRate = 0.0;
    m_durationSecs = 0;
    emit sigSnapshot(snapshot());
}

// ---------------------------------------------------------------------------
// 每秒结算 + 快照推送
// ---------------------------------------------------------------------------

void StatEngine::slotTick()
{
    // 差值法：本秒增量即为本秒速率
    m_rxBps = double(m_rxTotal - m_rxLast);
    m_txBps = double(m_txTotal - m_txLast);
    m_rxLast = m_rxTotal;
    m_txLast = m_txTotal;

    m_frameRate = double(m_framesInWindow);
    m_framesInWindow = 0;
    ++m_durationSecs;

    emit sigSnapshot(snapshot());
}

StatSnapshot StatEngine::snapshot() const
{
    StatSnapshot s;
    s.temp = toStat(m_temp);
    s.hum = toStat(m_hum);
    s.rxBps = m_rxBps;
    s.txBps = m_txBps;
    s.frameRate = m_frameRate;

    s.totalFrames = m_proto.totalFrames;
    s.validFrames = m_proto.validFrames;
    s.crcErrors = m_proto.crcErrors;
    s.syncLosses = m_proto.syncLosses;

    // 丢包率 = (CRC错误 + 失步) / 总帧数
    s.lossRate = (m_proto.totalFrames > 0)
                     ? double(m_proto.crcErrors + m_proto.syncLosses) / double(m_proto.totalFrames)
                     : 0.0;

    s.samples = m_temp.count;
    s.durationSecs = m_durationSecs;
    return s;
}

// ---------------------------------------------------------------------------
// 分布直方图
// ---------------------------------------------------------------------------

double StatEngine::histogram(StatChannel ch, int bins, QVector<double> &centers, QVector<int> &counts) const
{
    centers.clear();
    counts.clear();
    if (bins <= 0)
        return 0.0;

    const Accum &a = accum(ch);
    if (a.values.isEmpty())
        return 0.0;

    double lo = a.min;
    double hi = a.max;
    // 所有样本同值（如恒温箱稳态）时人为撑开区间，避免除零
    if (qFuzzyCompare(lo, hi)) {
        lo -= 0.5;
        hi += 0.5;
    }

    const double width = (hi - lo) / bins;
    centers.resize(bins);
    counts = QVector<int>(bins, 0);

    for (double v : a.values) {
        int idx = int((v - lo) / width);
        if (idx < 0)
            idx = 0;
        else if (idx >= bins)
            idx = bins - 1;   // 上边界（最大值）归入最后一箱
        ++counts[idx];
    }

    for (int i = 0; i < bins; ++i)
        centers[i] = lo + width * (i + 0.5);

    return width;
}
