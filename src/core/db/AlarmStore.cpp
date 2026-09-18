/**
 * @file    AlarmStore.cpp
 * @brief   报警事件持久化实现
 */

#include "AlarmStore.h"
#include "DbTime.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

AlarmStore::AlarmStore(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

qint64 AlarmStore::insert(const AlarmEvent &ev)
{
    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral(
        "INSERT INTO alarm_event(ts, channel, level, kind, value, threshold, message, acked)"
        " VALUES(:t, :c, :l, :k, :v, :h, :m, :a)"));
    q.bindValue(QStringLiteral(":t"), DbTime::toDb(ev.ts.isValid() ? ev.ts : QDateTime::currentDateTime()));
    q.bindValue(QStringLiteral(":c"), AlarmText::channelKey(ev.channel));
    q.bindValue(QStringLiteral(":l"), static_cast<int>(ev.level));
    q.bindValue(QStringLiteral(":k"), static_cast<int>(ev.kind));
    q.bindValue(QStringLiteral(":v"), ev.value);
    q.bindValue(QStringLiteral(":h"), ev.threshold);
    q.bindValue(QStringLiteral(":m"), ev.message);
    q.bindValue(QStringLiteral(":a"), ev.acked ? 1 : 0);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

QList<AlarmEvent> AlarmStore::query(int limit, bool onlyUnacked) const
{
    QList<AlarmEvent> out;

    QSqlQuery q(m_db->handle());
    if (onlyUnacked)
        q.prepare(QStringLiteral("SELECT * FROM alarm_event WHERE acked=0 ORDER BY id DESC LIMIT :n"));
    else
        q.prepare(QStringLiteral("SELECT * FROM alarm_event ORDER BY id DESC LIMIT :n"));
    q.bindValue(QStringLiteral(":n"), limit);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return out;
    }

    while (q.next()) {
        AlarmEvent e;
        e.id = q.value(QStringLiteral("id")).toLongLong();
        e.ts = DbTime::fromDb(q.value(QStringLiteral("ts")).toString());
        e.channel = AlarmText::channelFromKey(q.value(QStringLiteral("channel")).toString());
        e.level = static_cast<AlarmLevel>(q.value(QStringLiteral("level")).toInt());
        e.kind = static_cast<AlarmKind>(q.value(QStringLiteral("kind")).toInt());
        e.value = q.value(QStringLiteral("value")).toDouble();
        e.threshold = q.value(QStringLiteral("threshold")).toDouble();
        e.message = q.value(QStringLiteral("message")).toString();
        e.acked = q.value(QStringLiteral("acked")).toInt() != 0;
        out.append(e);
    }
    return out;
}

int AlarmStore::count() const
{
    QSqlQuery q(m_db->handle());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM alarm_event")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int AlarmStore::unackedCount() const
{
    QSqlQuery q(m_db->handle());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM alarm_event WHERE acked=0")) && q.next())
        return q.value(0).toInt();
    return 0;
}

bool AlarmStore::setAcked(qint64 id, bool acked)
{
    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral("UPDATE alarm_event SET acked=:a WHERE id=:id"));
    q.bindValue(QStringLiteral(":a"), acked ? 1 : 0);
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool AlarmStore::ackAll()
{
    QSqlQuery q(m_db->handle());
    if (!q.exec(QStringLiteral("UPDATE alarm_event SET acked=1 WHERE acked=0"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool AlarmStore::clear()
{
    QSqlQuery q(m_db->handle());
    if (!q.exec(QStringLiteral("DELETE FROM alarm_event"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}
