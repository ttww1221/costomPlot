/**
 * @file    AlarmDefs.h
 * @brief   报警领域模型 —— 报警引擎与持久化层共用（S8）
 *
 * 单独抽出头文件是为了打破依赖环：
 * AlarmEngine（产生事件）与 AlarmStore（存取事件）都需要 AlarmEvent，
 * 若定义在任一方都会导致另一方反向依赖。
 */

#ifndef ALARMDEFS_H
#define ALARMDEFS_H

#include <QDateTime>
#include <QMetaType>
#include <QString>

// 报警通道
enum class AlarmChannel { Temperature, Humidity };
// 报警等级（数值越大越严重，界面据此着色）
enum class AlarmLevel { Info = 0, Warning = 1, Critical = 2 };
// 事件类型：越上限触发 / 越下限触发 / 恢复正常
enum class AlarmKind { HighTrigger = 0, LowTrigger = 1, Recovered = 2 };

/**
 * @brief 一条报警事件（id < 0 表示尚未入库）
 */
struct AlarmEvent
{
    qint64 id = -1;
    QDateTime ts;
    AlarmChannel channel = AlarmChannel::Temperature;
    AlarmLevel level = AlarmLevel::Info;
    AlarmKind kind = AlarmKind::HighTrigger;
    double value = 0.0;       // 触发时的实测值
    double threshold = 0.0;   // 被越过（或恢复所依据）的阈值
    QString message;
    bool acked = false;       // 是否已被操作员确认
};

Q_DECLARE_METATYPE(AlarmEvent)

namespace AlarmText {

inline QString channel(AlarmChannel c)
{
    return c == AlarmChannel::Temperature ? QStringLiteral("温度") : QStringLiteral("湿度");
}

inline QString unit(AlarmChannel c)
{
    return c == AlarmChannel::Temperature ? QStringLiteral("°C") : QStringLiteral("%");
}

inline QString level(AlarmLevel l)
{
    switch (l) {
    case AlarmLevel::Critical: return QStringLiteral("严重");
    case AlarmLevel::Warning:  return QStringLiteral("警告");
    default:                   return QStringLiteral("提示");
    }
}

inline QString kind(AlarmKind k)
{
    switch (k) {
    case AlarmKind::HighTrigger: return QStringLiteral("超上限");
    case AlarmKind::LowTrigger:  return QStringLiteral("低于下限");
    default:                     return QStringLiteral("已恢复");
    }
}

// 通道在数据库中的存储标识（避免中文入库带来的编码不确定性）
inline QString channelKey(AlarmChannel c)
{
    return c == AlarmChannel::Temperature ? QStringLiteral("TEMP") : QStringLiteral("HUM");
}

inline AlarmChannel channelFromKey(const QString &key)
{
    return key == QLatin1String("HUM") ? AlarmChannel::Humidity : AlarmChannel::Temperature;
}

}

#endif // ALARMDEFS_H
