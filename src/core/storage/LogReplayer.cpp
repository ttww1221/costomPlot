/**
 * @file    LogReplayer.cpp
 * @brief   历史数据回放器实现
 */

#include "LogReplayer.h"
#include "CsvLoader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTimer>

namespace {
// 协议标称采样周期：下位机 200ms 上报一帧，用作时间戳缺失/异常时的兜底间隔
constexpr int kDefaultIntervalMs = 200;
// 间隔下限：防止倍速过高或时间戳抖动导致定时器空转
constexpr int kMinIntervalMs = 5;
// 间隔上限：防止录制中断（如断电）造成的巨大时间差把回放卡住
constexpr int kMaxIntervalMs = 2000;
}

LogReplayer::LogReplayer(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);   // 单次触发：每帧重新计算间隔，支持变速与不均匀采样
    connect(m_timer, &QTimer::timeout, this, &LogReplayer::slotTick);
}

QDateTime LogReplayer::originalTimeAt(int index) const
{
    if (index < 0 || index >= m_frames.size())
        return QDateTime();
    return m_frames.at(index).timestamp;
}

bool LogReplayer::load(const QString &filePath)
{
    const CsvLoader::Result res = CsvLoader::load(filePath);
    if (!res.ok) {
        emit sigMessage(res.error);
        return false;
    }
    if (res.frames.isEmpty()) {
        emit sigMessage(QStringLiteral("文件内无有效数据: %1").arg(QFileInfo(filePath).fileName()));
        return false;
    }

    slotStop();
    m_frames = res.frames;
    m_index = 0;
    emit sigLoaded(filePath, m_frames.size(), res.badLines);
    emit sigProgress(0, m_frames.size(), m_frames.first().timestamp);
    emit sigMessage(QStringLiteral("已载入 %1 帧（跳过非法行 %2）")
                        .arg(m_frames.size())
                        .arg(res.badLines));
    return true;
}

void LogReplayer::setFrames(const QVector<SensorData> &frames)
{
    slotStop();
    m_frames = frames;
    m_index = 0;
    emit sigLoaded(QString(), m_frames.size(), 0);
}

void LogReplayer::clear()
{
    slotStop();
    m_frames.clear();
    m_index = 0;
}

void LogReplayer::setSpeed(double multiplier)
{
    if (multiplier <= 0.0 || qFuzzyCompare(m_speed, multiplier))
        return;
    m_speed = multiplier;
    // 播放中改倍速立即生效
    if (m_playing)
        scheduleNext();
}

void LogReplayer::slotPlay()
{
    if (m_frames.isEmpty()) {
        emit sigMessage(QStringLiteral("请先载入日志文件"));
        return;
    }
    if (m_playing)
        return;
    if (m_index >= m_frames.size())
        m_index = 0;   // 播完后再次点播放 = 从头开始

    setPlaying(true);
    scheduleNext();
}

void LogReplayer::slotPause()
{
    if (!m_playing)
        return;
    m_timer->stop();
    setPlaying(false);
}

void LogReplayer::slotStop()
{
    m_timer->stop();
    const bool wasPlaying = m_playing;
    m_index = 0;
    if (wasPlaying)
        setPlaying(false);
    if (!m_frames.isEmpty())
        emit sigProgress(0, m_frames.size(), m_frames.first().timestamp);
}

void LogReplayer::slotSeek(int index)
{
    if (m_frames.isEmpty())
        return;
    m_index = qBound(0, index, int(m_frames.size()) - 1);
    emit sigProgress(m_index, m_frames.size(), m_frames.at(m_index).timestamp);
    if (m_playing)
        scheduleNext();   // 播放中拖动进度条：立即从新位置续播
}

void LogReplayer::slotTick()
{
    if (!m_playing)
        return;

    if (m_index >= m_frames.size()) {
        setPlaying(false);
        emit sigFinished();
        emit sigMessage(QStringLiteral("回放结束，共 %1 帧").arg(m_frames.size()));
        return;
    }

    emitCurrent();
    ++m_index;

    if (m_index >= m_frames.size()) {
        setPlaying(false);
        emit sigProgress(m_index, m_frames.size(), m_frames.last().timestamp);
        emit sigFinished();
        emit sigMessage(QStringLiteral("回放结束，共 %1 帧").arg(m_frames.size()));
        return;
    }

    emit sigProgress(m_index, m_frames.size(), m_frames.at(m_index).timestamp);
    scheduleNext();
}

void LogReplayer::emitCurrent()
{
    SensorData d = m_frames.at(m_index);
    // 重写时间戳为当前时刻：波形面板按"now - window"裁剪数据，
    // 沿用历史时间戳会导致回放点被立即移除（详见头文件设计说明 3）
    d.timestamp = QDateTime::currentDateTime();
    d.frameIndex = m_index + 1;
    emit sigFrame(d);
}

void LogReplayer::scheduleNext()
{
    int interval = kDefaultIntervalMs;

    // 用相邻帧的真实时间差还原原始采样节奏，并把差值夹在合理区间内
    if (m_index + 1 < m_frames.size()) {
        qint64 delta = m_frames.at(m_index).timestamp.msecsTo(m_frames.at(m_index + 1).timestamp);
        if (delta > 0) {
            if (delta < kMinIntervalMs)
                delta = kMinIntervalMs;   // 时间戳抖动/同刻多帧：不至于空转
            else if (delta > kMaxIntervalMs)
                delta = kMaxIntervalMs;   // 录制曾中断：不让回放长时间卡住
            interval = int(delta);
        }
    }

    m_timer->start(qMax(kMinIntervalMs, int(interval / m_speed)));
}

void LogReplayer::setPlaying(bool on)
{
    if (m_playing == on)
        return;
    m_playing = on;
    emit sigPlayingChanged(on);
}
