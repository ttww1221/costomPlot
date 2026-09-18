/**
 * @file    WavePanel.cpp
 * @brief   波形面板占位实现（S3 接入 QCustomPlot 后重写）
 */

#include "WavePanel.h"
#include "qcustomplot.h"
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QTimer>
#include <QVBoxLayout>

namespace {
    //四条曲线颜色
    const QColor kTempColor(0xE7,0x4C, 0x3C);
    const QColor kHumColor(0x34, 0x98, 0xDB);
    const QColor kSetpointColor(0x2E, 0xCC, 0x71);
    const QColor kAngleColor(0xE6, 0x7E, 0x22);

}

WavePanel::WavePanel(QWidget *parent)
    : QGroupBox(QStringLiteral("实时波形"),parent)
{
    setupPlot();

    //===== 通道显隐勾选框（文字颜色与曲线一致，方便对应）=====
    m_chkTemp = new QCheckBox(QStringLiteral("温度"));
    m_chkHum = new QCheckBox(QStringLiteral("湿度"));
    m_chkSetpoint = new QCheckBox(QStringLiteral("设定值"));
    m_chkAngle = new QCheckBox(QStringLiteral("仪表角度"));

    for (QCheckBox *box :{m_chkTemp,m_chkHum,m_chkSetpoint,m_chkAngle})
        box->setChecked(true);
    m_chkTemp->setStyleSheet(QStringLiteral("color: %1;").arg(kTempColor.name()));
    m_chkHum->setStyleSheet(QStringLiteral("color: %1;").arg(kHumColor.name()));
    m_chkSetpoint->setStyleSheet(QStringLiteral("color: %1;").arg(kSetpointColor.name()));
    m_chkAngle->setStyleSheet(QStringLiteral("color: %1;").arg(kAngleColor.name()));

    connect(m_chkTemp, &QCheckBox::toggled,this,
            [this](bool on) { slotToggleChannel(GraphTemp,on);});
    connect(m_chkHum, &QCheckBox::toggled,this,
            [this](bool on) { slotToggleChannel(GraphHum,on);});
    connect(m_chkSetpoint, &QCheckBox::toggled,this,
            [this](bool on) { slotToggleChannel(GraphSetpoint,on);});
    connect(m_chkAngle, &QCheckBox::toggled,this,
            [this](bool on) { slotToggleChannel(GraphAngle,on);});

    // ===== 工具条：时间窗口 / 暂停 / 清空 =====
    m_cmbWindow = new QComboBox;
    m_cmbWindow->addItem(QStringLiteral("30s"), 30);
    m_cmbWindow->addItem(QStringLiteral("60s"), 60);
    m_cmbWindow->addItem(QStringLiteral("2min"), 120);
    m_cmbWindow->addItem(QStringLiteral("5min"), 300);
    m_cmbWindow->setCurrentIndex(1);
    connect(m_cmbWindow, &QComboBox::currentIndexChanged, this, [this](int) {
        m_windowSecs = m_cmbWindow->currentData().toInt();
        m_dirty = true;
    });

    m_btnPause = new QPushButton(QStringLiteral("暂停"));
    m_btnClear = new QPushButton(QStringLiteral("清空"));
    connect(m_btnPause, &QPushButton::clicked, this, &WavePanel::slotTogglePause);
    connect(m_btnClear, &QPushButton::clicked, this, &WavePanel::slotClear);

    QHBoxLayout *optionRow = new QHBoxLayout;
    optionRow->addWidget(m_chkTemp);
    optionRow->addWidget(m_chkHum);
    optionRow->addWidget(m_chkSetpoint);
    optionRow->addWidget(m_chkAngle);
    optionRow->addStretch(1);
    optionRow->addWidget(new QLabel(QStringLiteral("窗口:")));
    optionRow->addWidget(m_cmbWindow);
    optionRow->addWidget(m_btnPause);
    optionRow->addWidget(m_btnClear);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_plot, 1);   // 绘图区占主要空间
    mainLayout->addLayout(optionRow);

    // ===== 30Hz 节流重绘定时器 =====
    m_replotTimer = new QTimer(this);
    m_replotTimer->setInterval(33);
    connect(m_replotTimer, &QTimer::timeout, this, &WavePanel::slotReplot);
    m_replotTimer->start();
}

void WavePanel::setupPlot()
{
    m_plot = new QCustomPlot(this);

    // 4 条曲线，索引与 GraphId 枚举对应
    m_plot->addGraph();
    m_plot->graph(GraphTemp)->setName(QStringLiteral("温度(°C)"));
    m_plot->graph(GraphTemp)->setPen(QPen(kTempColor, 2));

    m_plot->addGraph();
    m_plot->graph(GraphHum)->setName(QStringLiteral("湿度(%)"));
    m_plot->graph(GraphHum)->setPen(QPen(kHumColor, 2));

    m_plot->addGraph();
    m_plot->graph(GraphSetpoint)->setName(QStringLiteral("设定值"));
    m_plot->graph(GraphSetpoint)->setPen(QPen(kSetpointColor, 2, Qt::DashLine)); // 虚线

    m_plot->addGraph();
    m_plot->graph(GraphAngle)->setName(QStringLiteral("仪表角度(°)"));
    m_plot->graph(GraphAngle)->setPen(QPen(kAngleColor, 2));

    // 图例：绘图区右上角
    m_plot->legend->setVisible(true);
    m_plot->legend->setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);

    // x 轴：key 是 unix 时间戳(秒)，用 DateTime ticker 显示成 hh:mm:ss
    QSharedPointer<QCPAxisTickerDateTime> ticker(new QCPAxisTickerDateTime);
    ticker->setDateTimeFormat(QStringLiteral("hh:mm:ss"));
    m_plot->xAxis->setTicker(ticker);

    m_plot->yAxis->setLabel(QStringLiteral("数值"));
    m_plot->setMinimumHeight(320);

    // 交互：左键拖拽平移、滚轮缩放、图例可点选
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend);
}

double WavePanel::currentKey() const
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

void WavePanel::slotAppendData(const SensorData &data)
{
    if (m_paused)
        return;

    // 用帧自带的时间戳做 x 轴 key。S6 的 LogReplayer 回放时会把历史帧的
    // timestamp 重写为当前时刻，因此本逻辑对"实时/回放"两条通路完全通用，
    // 回放数据同样落在滑动窗口内，不会被 removeOldData 立即裁掉。
    const double key = data.timestamp.toMSecsSinceEpoch() / 1000.0;
    m_plot->graph(GraphTemp)->addData(key, data.temperature);
    m_plot->graph(GraphHum)->addData(key, data.humidity);

    removeOldData();
    m_dirty = true;   // 只置脏，真正重绘交给 slotReplot（节流核心）
}

void WavePanel::removeOldData()
{
    // 滑动窗口：只保留 [now-window, now] 内的数据点，内存恒定不增长
    const double oldest = currentKey() - m_windowSecs;
    for (int i = 0; i < m_plot->graphCount(); ++i)
        m_plot->graph(i)->data()->removeBefore(oldest);
}

void WavePanel::slotReplot()
{
    if (m_paused || !m_dirty)
        return;   // 无新数据就不重绘，CPU 占用接近零

    // x 轴范围跟随当前时间滚动
    const double now = currentKey();
    m_plot->xAxis->setRange(now - m_windowSecs, now);
    // rpQueuedReplot：同一帧内的多次重绘请求会被合并
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    m_dirty = false;
}

void WavePanel::slotTogglePause()
{
    m_paused = !m_paused;
    m_btnPause->setText(m_paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
}

void WavePanel::slotClear()
{
    for (int i = 0; i < m_plot->graphCount(); ++i)
        m_plot->graph(i)->data()->clear();
    m_dirty = true;
}

void WavePanel::slotToggleChannel(int graphId, bool visible)
{
    m_plot->graph(graphId)->setVisible(visible);
    m_dirty = true;
}

void WavePanel::slotSetSetpoint(double temp)
{
    if (m_paused)
        return;
    m_plot->graph(GraphSetpoint)->addData(currentKey(), temp);
    removeOldData();
    m_dirty = true;
}

void WavePanel::slotSetAngle(double angle)
{
    if (m_paused)
        return;
    m_plot->graph(GraphAngle)->addData(currentKey(), angle);
    removeOldData();
    m_dirty = true;
}