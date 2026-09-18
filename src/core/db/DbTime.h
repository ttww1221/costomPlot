/**
 * @file    DbTime.h
 * @brief   数据库时间字段的统一序列化格式 —— 基础设施层（S8）
 *
 * SQLite 没有日期类型，时间以 TEXT 存储。全库统一使用
 * "yyyy-MM-dd hh:mm:ss.zzz" 格式，理由：
 *  1. 字典序即时间序，ORDER BY ts DESC 与建索引都能正确工作；
 *  2. 毫秒精度与协议 200ms 采样周期匹配，可区分同一秒内的多条事件；
 *  3. 人眼可直接阅读，用 DB 工具打开排查问题无需转换。
 */

#ifndef DBTIME_H
#define DBTIME_H

#include <QDateTime>
#include <QString>

namespace DbTime {

inline QString format()
{
    return QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz");
}

// QDateTime -> 数据库文本（无效时间返回空串）
inline QString toDb(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(format()) : QString();
}

// 数据库文本 -> QDateTime（兼容带毫秒与不带毫秒两种写法）
inline QDateTime fromDb(const QString &text)
{
    QDateTime dt = QDateTime::fromString(text, format());
    if (!dt.isValid())
        dt = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    if (!dt.isValid())
        dt = QDateTime::fromString(text, Qt::ISODateWithMs);
    return dt;
}

// 当前时刻的数据库文本
inline QString now()
{
    return toDb(QDateTime::currentDateTime());
}

}

#endif // DBTIME_H
