/**
 * @file    LogPanel.cpp
 * @brief   数据记录与回放面板实现
 */

#include "LogPanel.h"
#include "ConvertUtils.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
const char *kGroupStyle = "QGroupBox { font-weight: bold; }";
}

LogPanel::LogPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("数据记录与回放"), parent)
{
    setStyleSheet(QString::fromLatin1(kGroupStyle));

    // ===== 记录区 =====
    m_btnRecord = new QPushButton(QStringLiteral("开始记录"));
    m_btnRecord->setCheckable(true);
    m_btnRecord->setToolTip(QStringLiteral("把实时数据追加写入 logs/log_日期_时间.csv"));

    m_lblFile = new QLabel(QStringLiteral("未记录"));
    m_lblFile->setStyleSheet(QStringLiteral("color: #7f8c8d;"));
    m_lblRecInfo = new QLabel(QStringLiteral("0 行 / 0 B"));

    QGridLayout *recGrid = new QGridLayout;
    recGrid->addWidget(m_btnRecord, 0, 0, 1, 2);
    recGrid->addWidget(new QLabel(QStringLiteral("文件:")), 1, 0);
    recGrid->addWidget(m_lblFile, 1, 1);
    recGrid->addWidget(new QLabel(QStringLiteral("进度:")), 2, 0);
    recGrid->addWidget(m_lblRecInfo, 2, 1);
    recGrid->setColumnStretch(1, 1);

    // ===== 回放区 =====
    m_btnOpen = new QPushButton(QStringLiteral("打开日志…"));
    m_lblReplayInfo = new QLabel(QStringLiteral("未载入"));
    m_lblReplayInfo->setStyleSheet(QStringLiteral("color: #7f8c8d;"));

    m_btnPlay = new QPushButton(QStringLiteral("播放"));
    m_btnPlay->setCheckable(true);
    m_btnPlay->setEnabled(false);
    m_btnStop = new QPushButton(QStringLiteral("停止"));
    m_btnStop->setEnabled(false);

    m_cmbSpeed = new QComboBox;
    m_cmbSpeed->addItem(QStringLiteral("1x"), 1.0);
    m_cmbSpeed->addItem(QStringLiteral("2x"), 2.0);
    m_cmbSpeed->addItem(QStringLiteral("5x"), 5.0);
    m_cmbSpeed->addItem(QStringLiteral("10x"), 10.0);
    m_cmbSpeed->setToolTip(QStringLiteral("回放倍速：按原始采样间隔除以倍速播放"));

    m_sliderProgress = new QSlider(Qt::Horizontal);
    m_sliderProgress->setRange(0, 0);
    m_sliderProgress->setEnabled(false);

    m_lblPosition = new QLabel(QStringLiteral("0 / 0"));

    QHBoxLayout *openRow = new QHBoxLayout;
    openRow->addWidget(m_btnOpen);
    openRow->addWidget(m_lblReplayInfo, 1);

    QHBoxLayout *playRow = new QHBoxLayout;
    playRow->addWidget(m_btnPlay);
    playRow->addWidget(m_btnStop);
    playRow->addWidget(new QLabel(QStringLiteral("倍速")));
    playRow->addWidget(m_cmbSpeed);
    playRow->addStretch(1);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(recGrid);
    mainLayout->addSpacing(6);
    mainLayout->addLayout(openRow);
    mainLayout->addLayout(playRow);
    mainLayout->addWidget(m_sliderProgress);
    mainLayout->addWidget(m_lblPosition);

    // ===== 交互接线 =====
    connect(m_btnRecord, &QPushButton::clicked, this, &LogPanel::slotToggleRecord);
    connect(m_btnOpen, &QPushButton::clicked, this, &LogPanel::slotOpenFile);
    connect(m_btnPlay, &QPushButton::clicked, this, &LogPanel::slotTogglePlay);
    connect(m_btnStop, &QPushButton::clicked, this, [this]() {
        m_btnPlay->setChecked(false);
        emit sigReplayStop();
    });
    connect(m_cmbSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogPanel::slotSpeedChanged);
    connect(m_sliderProgress, &QSlider::sliderMoved, this, &LogPanel::slotSliderMoved);
    connect(m_sliderProgress, &QSlider::sliderReleased, this, [this]() {
        emit sigReplaySeek(m_sliderProgress->value());
    });
}

// ---------------------------------------------------------------------------
// 私有槽
// ---------------------------------------------------------------------------

void LogPanel::slotToggleRecord()
{
    if (m_btnRecord->isChecked())
        emit sigStartRecording();
    else
        emit sigStopRecording();
}

void LogPanel::slotTogglePlay()
{
    if (m_btnPlay->isChecked())
        emit sigReplayPlay();
    else
        emit sigReplayPause();
}

void LogPanel::slotSpeedChanged(int index)
{
    if (index < 0)
        return;
    emit sigReplaySpeed(m_cmbSpeed->itemData(index).toDouble());
}

void LogPanel::slotSliderMoved(int value)
{
    // 拖动过程中实时显示位置，松手才真正 seek，避免频繁跳帧
    m_lblPosition->setText(QStringLiteral("%1 / %2").arg(value).arg(m_totalFrames));
}

void LogPanel::slotOpenFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开历史日志"), QStringLiteral("logs"),
        QStringLiteral("CSV 日志 (*.csv);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    emit sigLoadFile(path);
}

// ---------------------------------------------------------------------------
// 记录器状态回显
// ---------------------------------------------------------------------------

void LogPanel::slotRecorderStarted(const QString &path)
{
    m_btnRecord->setChecked(true);
    m_btnRecord->setText(QStringLiteral("停止记录"));
    m_lblFile->setText(QFileInfo(path).fileName());
    m_lblFile->setToolTip(path);
    m_lblFile->setStyleSheet(QStringLiteral("color: #27ae60;"));
    m_lblRecInfo->setText(QStringLiteral("0 行 / 0 B"));
}

void LogPanel::slotRecorderStopped(const QString &path, qint64 rows)
{
    m_btnRecord->setChecked(false);
    m_btnRecord->setText(QStringLiteral("开始记录"));
    m_lblFile->setText(QFileInfo(path).fileName());
    m_lblFile->setStyleSheet(QStringLiteral("color: #7f8c8d;"));
    m_lblRecInfo->setText(QStringLiteral("共 %1 行（已保存）").arg(rows));
    emit sigMessage(QStringLiteral("记录已保存: %1（%2 行）").arg(path).arg(rows));
}

void LogPanel::slotRecorderProgress(const QString &path, qint64 rows, qint64 bytes)
{
    Q_UNUSED(path);
    m_lblRecInfo->setText(QStringLiteral("%1 行 / %2").arg(rows).arg(ConvertUtils::formatByteSize(bytes)));
}

void LogPanel::slotRecorderRotated(const QString &newPath)
{
    m_lblFile->setText(QFileInfo(newPath).fileName());
    m_lblFile->setToolTip(newPath);
    emit sigMessage(QStringLiteral("日志文件已达上限，自动切割为 %1").arg(QFileInfo(newPath).fileName()));
}

// ---------------------------------------------------------------------------
// 回放器状态回显
// ---------------------------------------------------------------------------

void LogPanel::slotReplayLoaded(const QString &path, int frames, int badLines)
{
    m_totalFrames = frames;

    if (!path.isEmpty()) {
        m_lblReplayInfo->setText(QStringLiteral("%1 (%2 帧)").arg(QFileInfo(path).fileName()).arg(frames));
        m_lblReplayInfo->setToolTip(path);
    } else {
        m_lblReplayInfo->setText(QStringLiteral("已注入 %1 帧").arg(frames));
    }
    m_lblReplayInfo->setStyleSheet(QStringLiteral("color: #2980b9;"));

    m_sliderProgress->setRange(0, qMax(0, frames - 1));
    m_sliderProgress->setValue(0);
    m_sliderProgress->setEnabled(frames > 0);
    m_btnPlay->setEnabled(frames > 0);
    m_btnStop->setEnabled(frames > 0);
    m_lblPosition->setText(QStringLiteral("0 / %1").arg(frames));

    if (badLines > 0)
        emit sigMessage(QStringLiteral("载入 %1 帧，跳过非法行 %2 行").arg(frames).arg(badLines));
}

void LogPanel::slotReplayProgress(int index, int total, const QDateTime &originalTime)
{
    m_totalFrames = total;
    if (!m_sliderProgress->isSliderDown())
        m_sliderProgress->setValue(index);

    m_lblPosition->setText(QStringLiteral("%1 / %2   录制于 %3")
                               .arg(index)
                               .arg(total)
                               .arg(originalTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))));
}

void LogPanel::slotReplayPlayingChanged(bool playing)
{
    // 以回放器的真实状态为准回填按钮，避免按钮与实际不同步
    m_btnPlay->setChecked(playing);
    m_btnPlay->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
}

void LogPanel::slotReplayFinished()
{
    m_btnPlay->setChecked(false);
    m_btnPlay->setText(QStringLiteral("播放"));
    if (m_sliderProgress->maximum() > 0)
        m_sliderProgress->setValue(m_sliderProgress->maximum());
}
