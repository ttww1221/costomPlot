/**
 * @file    Database.h
 * @brief   SQLite 数据库连接与模式管理 —— 基础设施层（S8）
 *
 * 为什么选 SQLite 而不是 MySQL：
 *  上位机是单机桌面程序，数据只在本地产生与消费，不需要网络数据库的
 *  并发与远程访问能力；SQLite 是零配置的单文件嵌入式库，Qt 自带 QSQLITE
 *  驱动，程序"绿色"运行，答辩现场不依赖任何外部服务。
 *
 * 关键设计：
 *  1. 连接名唯一化（db_<对象地址>）—— QSqlDatabase 的连接是全局注册的，
 *     若都用默认连接名，单元测试里同时存在多个 Database 实例会互相顶掉；
 *  2. 打开时执行 PRAGMA：
 *       journal_mode = WAL   写前日志，读写不互相阻塞，掉电不易损坏库文件
 *       foreign_keys = ON    SQLite 默认不强制外键，必须显式开启
 *       synchronous = NORMAL WAL 下兼顾安全与写入速度（FULL 会显著拖慢插入）
 *  3. 建表统一用 CREATE TABLE IF NOT EXISTS，程序启动即自愈；
 *     模式版本号写在 schema_info 表里，为后续升级预留 migrate() 钩子。
 */

#ifndef DATABASE_H
#define DATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>

class Database : public QObject
{
    Q_OBJECT

public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    /**
     * @brief 打开（必要时创建）数据库并建表
     * @param path 库文件路径；传 ":memory:" 得到纯内存库（单元测试用）；
     *             留空则使用 defaultPath()
     */
    bool open(const QString &path = QString());
    void close();

    bool isOpen() const;
    QString filePath() const;
    QString lastError() const;
    int schemaVersion() const;

    // 供各 DAO 构造查询使用（同一连接，主线程内共享）
    QSqlDatabase handle() const { return m_db; }

    // 默认库文件位置：<程序目录>/data/app.db
    static QString defaultPath();

private:
    bool createSchema();          // 建表 + 索引
    bool exec(const QString &sql); // 执行一条 DDL，失败时记录 lastError
    bool setSchemaVersion(int version);

    QSqlDatabase m_db;
    QString m_connName;   // 本实例独占的连接名
    QString m_lastError;
    int m_schemaVersion = 0;
};

#endif // DATABASE_H
