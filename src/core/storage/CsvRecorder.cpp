/**
 * @file    CsvRecorder.cpp
 * @brief   CSV 数据记录器实现
 */

#include "CsvRecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTimer>

namespace {
// 单文件大小上限：超过则切割新文件（设计文档 6.4 节"磁盘管理"）
constexpr qint64 kMaxFileBytes = 50LL * 1024 * 1024;
// 刷盘周期：1s。5Hz 采样下一秒约 5 行，IO 次数从 5 次降到 1 次
constexpr int kFlushIntervalMs = 1000;
// CSV 表头
const char *kCsvHeader = "timestamp,frame_index,temperature,humidity";
}

CsvRecorder::CsvRecorder(QObject *parent)
    : QObject(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &CsvRecorder::slotFlush);
}

CsvRecorder::~CsvRecorder()
{
    // 对象销毁前强制落盘，防止程序异常退出丢失缓冲数据
    stop();
}

QString CsvRecorder::defaultLogDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
}

QString CsvRecorder::filePath() const
{
    return m_recording ? m_file.fileName() : QString();
}

bool CsvRecorder::start(const QString &dir)
{
    if (m_recording)
        return true;   // 已在记录，幂等返回

    m_dir = dir.isEmpty() ? defaultLogDir() : dir;
    if (!openNewFile(m_dir))
        return false;

    m_rows = 0;
    m_recording = true;
    m_flushTimer->start();
    emit sigStarted(m_file.fileName());
    return true;
}

void CsvRecorder::stop()
{
    if (!m_recording)
        return;

    m_flushTimer->stop();
    m_recording = false;

    const QString path = m_file.fileName();
    const qint64 rows = m_rows;

    slotFlush();          // 关闭前把缓冲全部写出
    m_stream.setDevice(nullptr);
    m_file.close();

    emit sigStopped(path, rows);
}

void CsvRecorder::slotRecord(const SensorData &data)
{
    if (!m_recording)
        return;

    // 时间戳精确到毫秒，与协议帧的采样周期（200ms）相匹配
    const QString line = QStringLiteral("%1,%2,%3,%4\n")
                             .arg(data.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
                             .arg(data.frameIndex)
                             .arg(data.temperature, 0, 'f', 2)
                             .arg(data.humidity, 0, 'f', 2);

    m_stream << line;
    ++m_rows;
    ++m_rowsInFile;
    m_bytesInFile += line.toUtf8().size();

    emit sigProgress(m_file.fileName(), m_rows, m_bytesInFile);

    // 超过单文件上限：切割到新文件，行数继续累加
    if (m_bytesInFile >= kMaxFileBytes) {
        slotFlush();
        m_stream.setDevice(nullptr);
        m_file.close();
        if (openNewFile(m_dir))
            emit sigRotated(m_file.fileName());
        else
            stop();   // 切割失败（磁盘满等）则安全停止
    }
}

void CsvRecorder::slotFlush()
{
    if (m_recording) {
        m_stream.flush();
        m_file.flush();
    }
}

bool CsvRecorder::openNewFile(const QString &dir)
{
    QDir d(dir);
    if (!d.exists() && !d.mkpath(QStringLiteral("."))) {
        emit sigError(QStringLiteral("无法创建日志目录: %1").arg(dir));
        return false;
    }

    // 文件名带启动时刻，同一目录内多次记录不会互相覆盖；
    // 若同一秒内重复创建（如快速切割），追加序号保证唯一
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"));
    QString path = d.filePath(QStringLiteral("log_%1.csv").arg(stamp));
    for (int i = 1; QFileInfo::exists(path); ++i)
        path = d.filePath(QStringLiteral("log_%1_%2.csv").arg(stamp).arg(i));

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit sigError(QStringLiteral("无法写入日志文件: %1 (%2)").arg(path, m_file.errorString()));
        return false;
    }

    m_stream.setDevice(&m_file);
    writeHeader();
    m_rowsInFile = 0;
    m_bytesInFile = qint64(m_file.size());
    return true;
}

void CsvRecorder::writeHeader()
{
    const QString header = QString::fromLatin1(kCsvHeader) + QLatin1Char('\n');
    m_stream << header;
    m_bytesInFile += header.toUtf8().size();
    m_stream.flush();   // 表头立即落盘，方便用户随时用 Excel 打开查看
}
