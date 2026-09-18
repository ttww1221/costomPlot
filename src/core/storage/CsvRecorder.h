/**
 * @file    CsvRecorder.h
 * @brief   CSV 数据记录器 —— 基础设施层（S6 数据持久化）
 *
 * 职责：把协议引擎解析出的每一帧 SensorData 追加写入 CSV 文件，
 * 使数据在程序关闭后依然可查、可回放（配合 CsvLoader / LogReplayer）。
 *
 * 文件格式（UTF-8，逗号分隔，首行表头）：
 *   timestamp,frame_index,temperature,humidity
 *   2026-09-17 15:30:22.123,42,25.60,60.10
 *
 * 关键设计：
 *  1. 按启动时刻自动命名 log_yyyyMMdd_hhmmss.csv，避免覆盖历史数据；
 *  2. 写入经 QTextStream 缓冲，由 1s 定时器统一刷盘 —— 5Hz 数据量下
 *     逐帧 flush 会产生大量小块磁盘 IO，定时刷盘把 IO 次数降到 1/5；
 *  3. 单文件超过 50MB 自动切割为新文件（长时间无人值守运行的磁盘保护）；
 *  4. 析构与 stop() 都会强制刷盘，保证异常退出不丢已收数据。
 */

#ifndef CSVRECORDER_H
#define CSVRECORDER_H

#include <QFile>
#include <QObject>
#include <QTextStream>

#include "ProtocolEngine.h"

class QTimer;

class CsvRecorder : public QObject
{
    Q_OBJECT

public:
    explicit CsvRecorder(QObject *parent = nullptr);
    ~CsvRecorder() override;

    bool isRecording() const { return m_recording; }
    QString filePath() const;      // 当前正在写入的文件全路径
    qint64 rowCount() const { return m_rows; }          // 累计写入数据行数
    qint64 bytesInFile() const { return m_bytesInFile; } // 当前文件已写字节数

    // 默认日志目录：<程序所在目录>/logs
    static QString defaultLogDir();

public slots:
    /**
     * @brief 开始记录
     * @param dir 日志目录，为空则使用 defaultLogDir()
     * @return 文件是否成功创建（失败时已通过 sigError 报告原因）
     */
    bool start(const QString &dir = QString());

    // 停止记录并刷盘关闭文件
    void stop();

    // 写入一帧数据（由 ProtocolEngine.sigFrameParsed 驱动）
    void slotRecord(const SensorData &data);

signals:
    void sigStarted(const QString &path);                 // 已开始记录
    void sigStopped(const QString &path, qint64 rows);    // 已停止（附本次总行数）
    void sigRotated(const QString &newPath);              // 文件达到上限被切割
    void sigProgress(const QString &path, qint64 rows, qint64 bytes); // 每帧写入后刷新
    void sigError(const QString &msg);                    // 目录/文件操作失败

private slots:
    void slotFlush();   // 定时刷盘（1s）

private:
    // 在 dir 下按当前时刻创建新文件并写表头
    bool openNewFile(const QString &dir);
    void writeHeader();

    QFile m_file;
    QTextStream m_stream;
    QTimer *m_flushTimer;   // 1s 刷盘定时器
    QString m_dir;          // 当前日志目录（切割新文件时复用）
    qint64 m_rows = 0;          // 累计行数（跨切割文件累加）
    qint64 m_rowsInFile = 0;    // 当前文件行数
    qint64 m_bytesInFile = 0;   // 当前文件字节数（自行累加，避免依赖文件系统刷新）
    bool m_recording = false;
};

#endif // CSVRECORDER_H
