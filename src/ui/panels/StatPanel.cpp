/**
 * @file    StatPanel.cpp
 * @brief   统计分析面板实现
 */

#include "StatPanel.h"
#include "qcustomplot.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const QColor kTempBarColor(0xE7, 0x4C, 0x3C);   // 与波形温度曲线同色
const QColor kHumBarColor(0x34, 0x98, 0xDB);    // 与波形湿度曲线同色
const char *kValueStyle = "QLabel { color: #2c3e50; }";
const char *kDimStyle = "QLabel { color: #7f8c8d; }";

// 把 秒 格式化为 hh:mm:ss
QString formatDuration(qint64 secs)
{
    return QStringLiteral("%1:%2:%3")
        .arg(secs / 3600, 2, 10, QLatin1Char('0'))
        .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0'));
}

// 极值标签文本："27.90 @15:41:02"，无样本时显示 "--"
QString formatExtreme(double value, const QDateTime &t, bool valid)
{
    if (!valid)
        return QStringLiteral("--");
    return QStringLiteral("%1  @%2").arg(value, 0, 'f', 2).arg(t.toString(QStringLiteral("hh:mm:ss")));
}
}

StatPanel::StatPanel(StatEngine *engine, QWidget *parent)
    : QGroupBox(QStringLiteral("统计分析"), parent)
    , m_engine(engine)
{
    // 先建画布再建控件：setupUi 里下拉框的 addItem 会立刻触发 currentIndexChanged，
    // 进而回调 rebuildHistogram 访问 m_bars，顺序颠倒会访问未初始化指针
    setupPlot();
    setupUi();
    slotReset();
}

void StatPanel::setupUi()
{
    // ===== 概览 =====
    m_lblSamples = new QLabel(QStringLiteral("样本 0   时长 00:00:00"));
    m_lblSamples->setStyleSheet(QString::fromLatin1(kDimStyle));

    // ===== 温度 / 湿度 统计表 =====
    // 列：指标名 | 温度值 | 湿度值，比两个独立分组更省纵向空间
    QGridLayout *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(3);

    QLabel *titleTemp = new QLabel(QStringLiteral("温度 (°C)"));
    titleTemp->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(kTempBarColor.name()));
    QLabel *titleHum = new QLabel(QStringLiteral("湿度 (%)"));
    titleHum->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(kHumBarColor.name()));

    grid->addWidget(new QLabel(QString()), 0, 0);
    grid->addWidget(titleTemp, 0, 1);
    grid->addWidget(titleHum, 0, 2);

    const QString rowNames[5] = {
        QStringLiteral("最新值"), QStringLiteral("平均值"), QStringLiteral("标准差"),
        QStringLiteral("最小值"), QStringLiteral("最大值")
    };

    // 一次性创建 5 行 × 2 列的数值标签
    QLabel *tempLabels[5];
    QLabel *humLabels[5];
    for (int r = 0; r < 5; ++r) {
        QLabel *name = new QLabel(rowNames[r]);
        name->setStyleSheet(QString::fromLatin1(kDimStyle));
        grid->addWidget(name, r + 1, 0);

        tempLabels[r] = new QLabel(QStringLiteral("--"));
        tempLabels[r]->setStyleSheet(QString::fromLatin1(kValueStyle));
        grid->addWidget(tempLabels[r], r + 1, 1);

        humLabels[r] = new QLabel(QStringLiteral("--"));
        humLabels[r]->setStyleSheet(QString::fromLatin1(kValueStyle));
        grid->addWidget(humLabels[r], r + 1, 2);
    }
    m_lblTempLast = tempLabels[0];
    m_lblTempMean = tempLabels[1];
    m_lblTempStd = tempLabels[2];
    m_lblTempMin = tempLabels[3];
    m_lblTempMax = tempLabels[4];
    m_lblHumLast = humLabels[0];
    m_lblHumMean = humLabels[1];
    m_lblHumStd = humLabels[2];
    m_lblHumMin = humLabels[3];
    m_lblHumMax = humLabels[4];

    grid->setColumnStretch(3, 1);

    // ===== 链路质量 =====
    m_lblRate = new QLabel(QStringLiteral("RX 0 B/s   TX 0 B/s"));
    m_lblFrameRate = new QLabel(QStringLiteral("帧率 0.0 帧/s"));
    m_lblLoss = new QLabel(QStringLiteral("丢包率 0.00%"));

    QHBoxLayout *linkRow = new QHBoxLayout;
    linkRow->addWidget(m_lblRate);
    linkRow->addSpacing(10);
    linkRow->addWidget(m_lblFrameRate);
    linkRow->addSpacing(10);
    linkRow->addWidget(m_lblLoss);
    linkRow->addStretch(1);

    // ===== 直方图控制 =====
    m_cmbChannel = new QComboBox;
    m_cmbChannel->addItem(QStringLiteral("温度分布"), int(StatChannel::Temperature));
    m_cmbChannel->addItem(QStringLiteral("湿度分布"), int(StatChannel::Humidity));

    m_cmbBins = new QComboBox;
    m_cmbBins->addItem(QStringLiteral("10 箱"), 10);
    m_cmbBins->addItem(QStringLiteral("20 箱"), 20);
    m_cmbBins->addItem(QStringLiteral("40 箱"), 40);
    m_cmbBins->setCurrentIndex(1);   // 默认 20 箱

    m_btnReset = new QPushButton(QStringLiteral("复位统计"));

    QHBoxLayout *histRow = new QHBoxLayout;
    histRow->addWidget(new QLabel(QStringLiteral("分布")));
    histRow->addWidget(m_cmbChannel);
    histRow->addWidget(m_cmbBins);
    histRow->addStretch(1);
    histRow->addWidget(m_btnReset);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_lblSamples);
    mainLayout->addLayout(grid);
    mainLayout->addSpacing(4);
    mainLayout->addLayout(linkRow);
    mainLayout->addSpacing(4);
    mainLayout->addLayout(histRow);
    mainLayout->addWidget(m_plot);   // 分布直方图画布
    mainLayout->addStretch(1);

    connect(m_cmbChannel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatPanel::slotChannelChanged);
    connect(m_cmbBins, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StatPanel::slotBinsChanged);
    connect(m_btnReset, &QPushButton::clicked, this, [this]() {
        slotReset();
        emit sigResetRequested();
    });
}

void StatPanel::setupPlot()
{
    m_plot = new QCustomPlot;
    m_plot->setMinimumHeight(150);
    m_plot->setMaximumHeight(190);

    // QCPAbstractPlottable 构造函数内部已调用 registerPlottable，无需手动登记
    m_bars = new QCPBars(m_plot->xAxis, m_plot->yAxis);
    m_bars->setWidthType(QCPBars::wtAbsolute);
    m_bars->setWidth(14);
    m_bars->setPen(QPen(kTempBarColor.darker(120)));
    m_bars->setBrush(QBrush(QColor(kTempBarColor.red(), kTempBarColor.green(), kTempBarColor.blue(), 160)));

    m_plot->xAxis->setLabel(QStringLiteral("数值"));
    m_plot->yAxis->setLabel(QStringLiteral("样本数"));
    m_plot->yAxis->setRangeLower(0);
    m_plot->legend->setVisible(false);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
}

void StatPanel::fillChannel(const ChannelStat &s, QLabel *last, QLabel *mean, QLabel *stddev,
                            QLabel *min, QLabel *max) const
{
    const bool has = s.count > 0;
    last->setText(has ? QStringLiteral("%1").arg(s.last, 0, 'f', 2) : QStringLiteral("--"));
    mean->setText(has ? QStringLiteral("%1").arg(s.mean, 0, 'f', 2) : QStringLiteral("--"));
    stddev->setText(has ? QStringLiteral("±%1").arg(s.stddev, 0, 'f', 3) : QStringLiteral("--"));
    min->setText(formatExtreme(s.min, s.minTime, has));
    max->setText(formatExtreme(s.max, s.maxTime, has));
}

void StatPanel::slotUpdateSnapshot(const StatSnapshot &s)
{
    m_lblSamples->setText(QStringLiteral("样本 %1   时长 %2   有效帧 %3")
                              .arg(s.samples)
                              .arg(formatDuration(s.durationSecs))
                              .arg(s.validFrames));

    fillChannel(s.temp, m_lblTempLast, m_lblTempMean, m_lblTempStd, m_lblTempMin, m_lblTempMax);
    fillChannel(s.hum, m_lblHumLast, m_lblHumMean, m_lblHumStd, m_lblHumMin, m_lblHumMax);

    m_lblRate->setText(QStringLiteral("RX %1 B/s   TX %2 B/s")
                           .arg(s.rxBps, 0, 'f', 0)
                           .arg(s.txBps, 0, 'f', 0));
    m_lblFrameRate->setText(QStringLiteral("帧率 %1 帧/s").arg(s.frameRate, 0, 'f', 1));
    m_lblLoss->setText(QStringLiteral("丢包率 %1%").arg(s.lossRate * 100.0, 0, 'f', 2));
    // 丢包率超阈值时标红提示链路质量问题
    m_lblLoss->setStyleSheet(s.lossRate > 0.01 ? QStringLiteral("color: #c0392b; font-weight: bold;")
                                               : QString::fromLatin1(kValueStyle));

    rebuildHistogram();
}

void StatPanel::slotChannelChanged(int index)
{
    Q_UNUSED(index);
    if (!m_bars)
        return;   // 构造期保护
    // 切换通道后柱状图配色跟随曲线颜色，避免误读
    const bool isTemp = currentChannel() == StatChannel::Temperature;
    const QColor c = isTemp ? kTempBarColor : kHumBarColor;
    m_bars->setPen(QPen(c.darker(120)));
    m_bars->setBrush(QBrush(QColor(c.red(), c.green(), c.blue(), 160)));
    m_plot->xAxis->setLabel(isTemp ? QStringLiteral("温度 (°C)") : QStringLiteral("湿度 (%)"));
    rebuildHistogram();
}

void StatPanel::slotBinsChanged(int index)
{
    Q_UNUSED(index);
    rebuildHistogram();
}

StatChannel StatPanel::currentChannel() const
{
    if (!m_cmbChannel)
        return StatChannel::Temperature;
    return StatChannel(m_cmbChannel->currentData().toInt());
}

int StatPanel::currentBins() const
{
    if (!m_cmbBins)
        return StatEngine::kDefaultBins;
    const int bins = m_cmbBins->currentData().toInt();
    return bins > 0 ? bins : StatEngine::kDefaultBins;
}

void StatPanel::rebuildHistogram()
{
    if (!m_engine || !m_bars || !m_plot)
        return;

    QVector<double> centers;
    QVector<int> counts;
    const double width = m_engine->histogram(currentChannel(), currentBins(), centers, counts);

    if (centers.isEmpty()) {
        m_bars->data()->clear();
        m_plot->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    QVector<double> keys, values;
    keys.reserve(centers.size());
    values.reserve(counts.size());
    for (int i = 0; i < centers.size(); ++i) {
        keys.append(centers.at(i));
        values.append(double(counts.at(i)));
    }
    m_bars->setData(keys, values, true);   // centers 升序，alreadySorted = true

    // x 轴留出半个箱宽的边距，y 轴上留 10% 余量
    const double halfWidth = width * 0.5;
    m_plot->xAxis->setRange(keys.first() - halfWidth * 2.0, keys.last() + halfWidth * 2.0);

    int peak = 0;
    for (int v : counts)
        peak = qMax(peak, v);
    m_plot->yAxis->setRange(0, qMax(1, int(peak * 1.15)));

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void StatPanel::slotReset()
{
    m_lblSamples->setText(QStringLiteral("样本 0   时长 00:00:00   有效帧 0"));
    const QString dash = QStringLiteral("--");
    for (QLabel *l : {m_lblTempLast, m_lblTempMean, m_lblTempStd, m_lblTempMin, m_lblTempMax,
                      m_lblHumLast, m_lblHumMean, m_lblHumStd, m_lblHumMin, m_lblHumMax})
        l->setText(dash);
    m_lblRate->setText(QStringLiteral("RX 0 B/s   TX 0 B/s"));
    m_lblFrameRate->setText(QStringLiteral("帧率 0.0 帧/s"));
    m_lblLoss->setText(QStringLiteral("丢包率 0.00%"));
    m_lblLoss->setStyleSheet(QString::fromLatin1(kValueStyle));
    if (m_bars) {
        m_bars->data()->clear();
        m_plot->replot(QCustomPlot::rpQueuedReplot);
    }
}
