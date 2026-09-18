/**
 * @file    AlarmStore.h
 * @brief   报警事件持久化 —— 数据访问层（S8）
 *
 * 与 CSV 记录器的分工：CSV 存"连续采样数据"（体量大、顺序追加、便于导出分析），
 * 数据库存"离散事件"（报警、审计、账号），需要按条件检索、更新确认状态、
 * 做聚合统计。两者互补，不重复存储。
 */

#ifndef ALARMSTORE_H
#define ALARMSTORE_H

#include <QList>
#include <QObject>

#include "AlarmDefs.h"
#include "Database.h"

class AlarmStore : public QObject
{
    Q_OBJECT

public:
    explicit AlarmStore(Database *db, QObject *parent = nullptr);

    // 查询历史报警（按时间倒序）；onlyUnacked=true 只取未确认的
    QList<AlarmEvent> query(int limit = 500, bool onlyUnacked = false) const;
    int count() const;
    int unackedCount() const;
    QString lastError() const { return m_lastError; }

public slots:
    // 入库并返回自增 id（失败返回 -1）；可直接连接 AlarmEngine::sigAlarm
    qint64 insert(const AlarmEvent &ev);
    // 标记确认（操作员点"确认"后不再高亮提示）
    bool setAcked(qint64 id, bool acked = true);
    bool ackAll();
    bool clear();

private:
    Database *m_db;
    mutable QString m_lastError;
};

#endif // ALARMSTORE_H
