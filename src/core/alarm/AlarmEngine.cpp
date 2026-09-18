/**
 * @file    AlarmEngine.cpp
 * @brief   阈值报警引擎实现
 */

#include "AlarmEngine.h"

#include <QStringList>
#include <QtMath>

namespace {
// 温度分级判据：越限幅度（°C）
constexpr double kCriticalDeviation = 5.0;   // 超过 5°C 判为严重
constexpr double kWarningDeviation = 2.0;    // 超过 2°C 判为警告，其余为提示
}

AlarmEngine::AlarmEngine(QObject *parent)
    : QObject(parent)
{
}

QString AlarmEngine::activeSummary() const
{
    if (m_active.isEmpty())
        return QString();

    QStringList parts;
    for (const AlarmEvent &e : m_active)
        parts << e.message;
    return parts.join(QStringLiteral("；"));
}

bool AlarmEngine::setThresholds(const AlarmThresholds &t)
{
    if (!t.isValid())
        return false;   // 拒绝非法配置，保持原有阈值

    m_th = t;
    // 阈值收紧后原有的"连续计数"不再有意义，清零重新计数
    m_temp = ChannelState();
    m_hum = ChannelState();
    emit sigThresholdsChanged(m_th);
    return true;
}

void AlarmEngine::slotSensorData(const SensorData &data)
{
    if (!m_th.enabled)
        return;

    if (m_th.tempEnabled)
        evaluate(AlarmChannel::Temperature, data.temperature, data.timestamp);
    if (m_th.humEnabled)
        evaluate(AlarmChannel::Humidity, data.humidity, data.timestamp);
}

void AlarmEngine::slotClearActive()
{
    m_active.clear();
    m_temp = ChannelState();
    m_hum = ChannelState();
    refreshActiveSignal();
}

void AlarmEngine::evaluate(AlarmChannel ch, double value, const QDateTime &ts)
{
    ChannelState &s = (ch == AlarmChannel::Temperature) ? m_temp : m_hum;

    const double low = (ch == AlarmChannel::Temperature) ? m_th.tempLow : m_th.humLow;
    const double high = (ch == AlarmChannel::Temperature) ? m_th.tempHigh : m_th.humHigh;

    const bool aboveHigh = value > high;
    const bool belowLow = value < low;

    s.highCount = aboveHigh ? s.highCount + 1 : 0;
    s.lowCount = belowLow ? s.lowCount + 1 : 0;

    // ---- 触发：连续 debounceFrames 帧越限 ----
    if (!s.highActive && !s.lowActive) {
        if (s.highCount >= m_th.debounceFrames) {
            s.highActive = true;
            s.lowCount = 0;
            trigger(ch, AlarmKind::HighTrigger, value, high, ts);
            return;
        }
        if (s.lowCount >= m_th.debounceFrames) {
            s.lowActive = true;
            s.highCount = 0;
            trigger(ch, AlarmKind::LowTrigger, value, low, ts);
            return;
        }
    }

    // ---- 恢复：活动状态下连续 debounceFrames 帧回到正常区间 ----
    if (s.highActive || s.lowActive) {
        s.recoverCount = (!aboveHigh && !belowLow) ? s.recoverCount + 1 : 0;
        if (s.recoverCount >= m_th.debounceFrames) {
            s.highActive = false;
            s.lowActive = false;
            s.recoverCount = 0;
            recover(ch, value, ts);
        }
    }
}

void AlarmEngine::trigger(AlarmChannel ch, AlarmKind kind, double value, double threshold, const QDateTime &ts)
{
    AlarmEvent e;
    e.ts = ts.isValid() ? ts : QDateTime::currentDateTime();
    e.channel = ch;
    e.kind = kind;
    e.value = value;
    e.threshold = threshold;
    e.level = levelFor(ch, value, threshold);
    e.acked = false;
    e.message = QStringLiteral("%1%2: %3%4 (阈值 %5%6)")
                    .arg(AlarmText::channel(ch),
                         AlarmText::kind(kind),
                         QString::number(value, 'f', 2),
                         AlarmText::unit(ch),
                         QString::number(threshold, 'f', 2),
                         AlarmText::unit(ch));

    m_active.append(e);
    emit sigAlarm(e);
    refreshActiveSignal();
}

void AlarmEngine::recover(AlarmChannel ch, double value, const QDateTime &ts)
{
    // 从活动集中移除该通道的报警
    for (int i = m_active.size() - 1; i >= 0; --i) {
        if (m_active.at(i).channel == ch)
            m_active.removeAt(i);
    }

    AlarmEvent e;
    e.ts = ts.isValid() ? ts : QDateTime::currentDateTime();
    e.channel = ch;
    e.kind = AlarmKind::Recovered;
    e.level = AlarmLevel::Info;
    e.value = value;
    e.threshold = (ch == AlarmChannel::Temperature) ? m_th.tempHigh : m_th.humHigh;
    e.acked = false;
    e.message = QStringLiteral("%1已恢复正常: %2%3")
                    .arg(AlarmText::channel(ch), QString::number(value, 'f', 2), AlarmText::unit(ch));

    emit sigAlarm(e);   // 恢复同样入库，形成完整的报警-恢复配对记录
    refreshActiveSignal();
}

AlarmLevel AlarmEngine::levelFor(AlarmChannel ch, double value, double threshold)
{
    if (ch == AlarmChannel::Humidity)
        return AlarmLevel::Warning;   // 湿度越限统一按警告处理

    const double dev = qAbs(value - threshold);
    if (dev > kCriticalDeviation)
        return AlarmLevel::Critical;
    if (dev > kWarningDeviation)
        return AlarmLevel::Warning;
    return AlarmLevel::Info;
}

void AlarmEngine::refreshActiveSignal()
{
    const bool active = !m_active.isEmpty();
    const QString summary = activeSummary();
    // 状态或摘要变化时才发信号，避免每帧都刷新界面
    if (active != m_lastActiveState || summary != m_lastSummary) {
        m_lastActiveState = active;
        m_lastSummary = summary;
        emit sigActiveChanged(active, summary);
    }
}
