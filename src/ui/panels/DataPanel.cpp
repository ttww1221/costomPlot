/**
 * @file    DataPanel.cpp
 * @brief   实时数据面板实现
 */

#include "DataPanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// 数值大字号样式：温度红色 / 湿度蓝色（与 S3 波形曲线颜色保持一致）
constexpr const char *kTempStyle = "font-size: 26px; font-weight: bold; color: #e74c3c;";
constexpr const char *kHumStyle  = "font-size: 26px; font-weight: bold; color: #3498db;";
}

DataPanel::DataPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("实时数据"), parent)
{
    // ===== 温湿度大数字 =====
    m_lblTemp = new QLabel(QStringLiteral("--.- °C"));
    m_lblTemp->setStyleSheet(QString::fromLatin1(kTempStyle));
    m_lblHum = new QLabel(QStringLiteral("--.- %"));
    m_lblHum->setStyleSheet(QString::fromLatin1(kHumStyle));

    m_lblLastFrame = new QLabel(QStringLiteral("等待数据..."));
    m_lblLastFrame->setStyleSheet(QStringLiteral("color: #7f8c8d;"));

    // ===== 链路统计标签 =====
    m_lblTotalBytes = new QLabel(QStringLiteral("0 B"));
    m_lblFrames = new QLabel(QStringLiteral("0"));
    m_lblValid = new QLabel(QStringLiteral("0"));
    m_lblCrcErrors = new QLabel(QStringLiteral("0"));
    m_lblSyncLosses = new QLabel(QStringLiteral("0"));
    m_lblFrameRate = new QLabel(QStringLiteral("0 帧/s"));

    m_btnReset = new QPushButton(QStringLiteral("清零统计"));

    // ===== 布局 =====
    // 左侧：温湿度；右侧：统计表
    QVBoxLayout *valueLayout = new QVBoxLayout;
    valueLayout->addWidget(new QLabel(QStringLiteral("温度")));
    valueLayout->addWidget(m_lblTemp);
    valueLayout->addSpacing(8);
    valueLayout->addWidget(new QLabel(QStringLiteral("湿度")));
    valueLayout->addWidget(m_lblHum);
    valueLayout->addStretch(1);

    QGridLayout *statsGrid = new QGridLayout;
    statsGrid->addWidget(new QLabel(QStringLiteral("总字节:")), 0, 0);
    statsGrid->addWidget(m_lblTotalBytes, 0, 1);
    statsGrid->addWidget(new QLabel(QStringLiteral("总帧数:")), 1, 0);
    statsGrid->addWidget(m_lblFrames, 1, 1);
    statsGrid->addWidget(new QLabel(QStringLiteral("有效帧:")), 2, 0);
    statsGrid->addWidget(m_lblValid, 2, 1);
    statsGrid->addWidget(new QLabel(QStringLiteral("CRC错误:")), 3, 0);
    statsGrid->addWidget(m_lblCrcErrors, 3, 1);
    statsGrid->addWidget(new QLabel(QStringLiteral("失步次数:")), 4, 0);
    statsGrid->addWidget(m_lblSyncLosses, 4, 1);
    statsGrid->addWidget(new QLabel(QStringLiteral("帧率:")), 5, 0);
    statsGrid->addWidget(m_lblFrameRate, 5, 1);

    QHBoxLayout *mainRow = new QHBoxLayout;
    mainRow->addLayout(valueLayout, 1);
    mainRow->addLayout(statsGrid, 1);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(mainRow);
    mainLayout->addWidget(m_lblLastFrame);
    mainLayout->addWidget(m_btnReset);

    // ===== 帧率统计：每秒结算一次窗口内帧数 =====
    m_rateTimer = new QTimer(this);
    m_rateTimer->setInterval(1000);
    connect(m_rateTimer, &QTimer::timeout, this, &DataPanel::slotUpdateFrameRate);
    m_rateTimer->start();

    connect(m_btnReset, &QPushButton::clicked, this, &DataPanel::sigResetRequested);
}

void DataPanel::slotUpdateData(const SensorData &data)
{
    // 大数字显示（保留 1 位小数，与 DHT11 精度一致）
    m_lblTemp->setText(QStringLiteral("%1 °C").arg(data.temperature, 0, 'f', 1));
    m_lblHum->setText(QStringLiteral("%1 %").arg(data.humidity, 0, 'f', 1));
    m_lblLastFrame->setText(QStringLiteral("帧 #%1  @ %2")
                                .arg(data.frameIndex)
                                .arg(data.timestamp.toString(QStringLiteral("hh:mm:ss.zzz"))));
    ++m_framesInWindow;  // 帧率窗口计数
}

void DataPanel::slotUpdateStats(const ProtoStats &stats)
{
    m_lblTotalBytes->setText(QStringLiteral("%1").arg(stats.totalBytes));
    m_lblFrames->setText(QString::number(stats.totalFrames));
    m_lblValid->setText(QString::number(stats.validFrames));
    m_lblCrcErrors->setText(QString::number(stats.crcErrors));
    m_lblSyncLosses->setText(QString::number(stats.syncLosses));
}

void DataPanel::slotReset()
{
    m_lblTemp->setText(QStringLiteral("--.- °C"));
    m_lblHum->setText(QStringLiteral("--.- %"));
    m_lblLastFrame->setText(QStringLiteral("等待数据..."));
    m_lblTotalBytes->setText(QStringLiteral("0 B"));
    m_lblFrames->setText(QStringLiteral("0"));
    m_lblValid->setText(QStringLiteral("0"));
    m_lblCrcErrors->setText(QStringLiteral("0"));
    m_lblSyncLosses->setText(QStringLiteral("0"));
    m_lblFrameRate->setText(QStringLiteral("0 帧/s"));
    m_framesInWindow = 0;
}

void DataPanel::slotUpdateFrameRate()
{
    m_lblFrameRate->setText(QStringLiteral("%1 帧/s").arg(m_framesInWindow));
    m_framesInWindow = 0;
}
