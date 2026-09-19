/**
 * @file    StatPanel.h
 * @brief   统计分析面板 —— 展示层（S7）
 *
 * 与 DataPanel 的分工：
 *  - DataPanel 关注"链路健康度"（总字节/总帧/CRC错误/失步/帧率）与当前瞬时值；
 *  - StatPanel 关注"数据本身的统计特征"：温度与湿度的极值（含出现时刻）、
 *    均值、标准差，收发速率、丢包率，以及温度/湿度的分布直方图。
 *
 * 直方图用 QCustomPlot 的 QCPBars 绘制：把 StatEngine 给出的等宽分箱
 * 结果映射为柱子，可直观看出数据是否集中、是否存在双峰（如风扇启停造成的两个温度簇）。
 *
 * 面板持有 StatEngine 指针仅用于按需查询直方图（快照信号只推标量统计，
 * 避免 1Hz 传输整个分箱数组），符合"UI 查询核心服务"的既有分层约定。
 */

#ifndef STATPANEL_H
#define STATPANEL_H

#include <QGroupBox>

#include "StatEngine.h"

class QCustomPlot;
class QCPBars;
class QComboBox;
class QLabel;
class QPushButton;

class StatPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit StatPanel(StatEngine *engine, QWidget *parent = nullptr);

public slots:
    void slotUpdateSnapshot(const StatSnapshot &s);   // 1Hz 刷新统计数值与直方图
    void slotReset();                                 // 界面复位（引擎复位由 sigResetRequested 触发）

signals:
    void sigResetRequested();   // 请求清零统计引擎

private slots:
    void slotChannelChanged(int index);   // 切换直方图通道
    void slotBinsChanged(int index);      // 切换分箱数

private:
    void setupUi();
    void setupPlot();
    void rebuildHistogram();
    // 当前直方图通道 / 分箱数（取自下拉框 itemData）
    StatChannel currentChannel() const;
    int currentBins() const;
    // 把统计结构体填进一组标签
    void fillChannel(const ChannelStat &s, QLabel *last, QLabel *mean, QLabel *stddev,
                     QLabel *min, QLabel *max) const;

    StatEngine *m_engine;    // 统计引擎（只读查询）

    // 概览
    QLabel *m_lblSamples;    // 样本数 / 统计时长

    // 温度
    QLabel *m_lblTempLast;
    QLabel *m_lblTempMean;
    QLabel *m_lblTempStd;
    QLabel *m_lblTempMin;
    QLabel *m_lblTempMax;

    // 湿度
    QLabel *m_lblHumLast;
    QLabel *m_lblHumMean;
    QLabel *m_lblHumStd;
    QLabel *m_lblHumMin;
    QLabel *m_lblHumMax;

    // 链路质量
    QLabel *m_lblRate;       // RX / TX bytes/s
    QLabel *m_lblFrameRate;  // 帧率
    QLabel *m_lblLoss;       // 丢包率

    // 直方图
    QCustomPlot *m_plot = nullptr;
    QCPBars *m_bars = nullptr;
    QComboBox *m_cmbChannel = nullptr;
    QComboBox *m_cmbBins = nullptr;

    QPushButton *m_btnReset = nullptr;
};

#endif // STATPANEL_H
