/**
 * @file    AuditService.cpp
 * @brief   操作审计服务实现
 */

#include "AuditService.h"
#include "DbTime.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

AuditService::AuditService(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
}

void AuditService::slotCurrentUser(int userId, const QString &username)
{
    m_userId = userId;
    m_username = username;
}

bool AuditService::log(const QString &action, const QString &detail, bool success)
{
    AuditEntry e;
    e.ts = QDateTime::currentDateTime();
    e.userId = m_userId;
    e.username = m_username;
    e.action = action;
    e.detail = detail;
    e.success = success;

    QSqlQuery q(m_db->handle());
    q.prepare(QStringLiteral(
        "INSERT INTO audit_log(ts, user_id, username, action, detail, success)"
        " VALUES(:t, :u, :n, :a, :d, :s)"));
    q.bindValue(QStringLiteral(":t"), DbTime::toDb(e.ts));
    q.bindValue(QStringLiteral(":u"), e.userId);
    q.bindValue(QStringLiteral(":n"), e.username);
    q.bindValue(QStringLiteral(":a"), e.action);
    q.bindValue(QStringLiteral(":d"), detail);
    q.bindValue(QStringLiteral(":s"), success ? 1 : 0);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    e.id = q.lastInsertId().toLongLong();
    emit sigEntryAdded(e);
    return true;
}

QList<AuditEntry> AuditService::query(int limit, const QString &actionFilter) const
{
    QList<AuditEntry> out;

    QSqlQuery q(m_db->handle());
    if (actionFilter.isEmpty()) {
        q.prepare(QStringLiteral("SELECT * FROM audit_log ORDER BY id DESC LIMIT :n"));
        q.bindValue(QStringLiteral(":n"), limit);
    } else {
        q.prepare(QStringLiteral("SELECT * FROM audit_log WHERE action=:a ORDER BY id DESC LIMIT :n"));
        q.bindValue(QStringLiteral(":a"), actionFilter);
        q.bindValue(QStringLiteral(":n"), limit);
    }

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return out;
    }

    while (q.next()) {
        AuditEntry e;
        e.id = q.value(QStringLiteral("id")).toLongLong();
        e.ts = DbTime::fromDb(q.value(QStringLiteral("ts")).toString());
        e.userId = q.value(QStringLiteral("user_id")).toInt();
        e.username = q.value(QStringLiteral("username")).toString();
        e.action = q.value(QStringLiteral("action")).toString();
        e.detail = q.value(QStringLiteral("detail")).toString();
        e.success = q.value(QStringLiteral("success")).toInt() != 0;
        out.append(e);
    }
    return out;
}

int AuditService::count() const
{
    QSqlQuery q(m_db->handle());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM audit_log")) && q.next())
        return q.value(0).toInt();
    return 0;
}

bool AuditService::clear()
{
    QSqlQuery q(m_db->handle());
    if (!q.exec(QStringLiteral("DELETE FROM audit_log"))) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}
