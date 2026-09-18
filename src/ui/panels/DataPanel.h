/**
 * @file    DataPanel.h
 * @brief   实时数据面板 —— 展示层
 *
 * 显示协议解析后的实时数据与链路质量统计：
 *  - 当前温度 / 湿度（大字号数字）
 *  - 最近一帧的序号与接收时间
 *  - 链路统计：总字节 / 总帧 / 有效帧 / CRC 错误 / 失步次数 / 帧率
 *  - "清零统计"按钮
 *
 * S7 起与 StatPanel 并存且分工明确：本面板关注"链路健康度"（字节/帧/CRC/失步/帧率）
 * 与瞬时值，StatPanel 关注"数据本身的统计特征"（极值、均值、标准差、分布直方图）。
 * 两者的数据同出 MainWindow::dispatchToView，实时与回放通路共用同一套显示逻辑。
 */

#ifndef DATAPANEL_H
#define DATAPANEL_H

#include <QGroupBox>
#include "ProtocolEngine.h"

class QLabel;
class QPushButton;
class QTimer;

class DataPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit DataPanel(QWidget *parent = nullptr);

public slots:
    void slotUpdateData(const SensorData &data);  // 新的一帧数据到达
    void slotUpdateStats(const ProtoStats &stats); // 统计信息刷新
    void slotReset();                             // 清零面板显示

signals:
    void sigResetRequested();  // 请求清零协议引擎统计

private slots:
    void slotUpdateFrameRate(); // 每秒刷新一次帧率显示

private:
    QLabel *m_lblTemp;       // 温度大数字
    QLabel *m_lblHum;        // 湿度大数字
    QLabel *m_lblLastFrame;  // 最近帧信息（序号 + 时间）
    QLabel *m_lblTotalBytes; // 总接收字节
    QLabel *m_lblFrames;     // 总帧数
    QLabel *m_lblValid;      // 有效帧数
    QLabel *m_lblCrcErrors;  // CRC 错误数
    QLabel *m_lblSyncLosses; // 失步次数
    QLabel *m_lblFrameRate;  // 帧率（帧/s）
    QPushButton *m_btnReset; // 清零统计
    QTimer *m_rateTimer;     // 帧率刷新定时器
    int m_framesInWindow = 0; // 本秒内收到的帧数
};

#endif // DATAPANEL_H
