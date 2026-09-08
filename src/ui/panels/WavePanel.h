/**
 * @file    WavePanel.h
 * @brief   波形面板 —— 展示层（占位）
 *
 * S3 阶段将替换为 QCustomPlot 实时波形：
 * 温度曲线（红）/ 湿度曲线（蓝）/ 设定值虚线（绿）/ 仪表角度（橙），
 * 支持缩放、拖拽、通道显隐、暂停与清空。
 */

#ifndef WAVEPANEL_H
#define WAVEPANEL_H

#include <QGroupBox>
#include "ProtocolEngine.h"
#include <QWidget>

class QCustomPlot;
class QCheckBox;
class QPushBotton;
class QComboBox;
class QTimer;

class WavePanel : public QWidget
{
    Q_OBJECT

public:
    //曲线索引：与setupPlot中的addGraph的顺序一一对应
    enum GraphId { GraphTemp = 0, GraphHum = 1, GraphSetpoint = 2, GraphAngle = 3 };
    explicit WavePanel(QWidget *parent = nullptr);

public slots:
    void slotAppendData(const SensorData &data);
    void slotSetSetpoint(double temp);
    void slotSetAngle(double angle);

private slots:
    void slotReplot(); //
    void slotTogglePause(); //
    void slotClear(); //
    void slotToggleChannel(int graphId,bool visible); //

private:
    void setupPlot();  //
    void removeOldData();  //
    double currentkey() const; //

    QCustomPlot *m_plot;
    QTimer *m_replotTimer;
    QComboBox *m_cmbWindow;
    QCheckBox *m_chkTemp;
    QCheckBox *m_chkHum;
    QCheckBox *m_chkSetpoint;
    QCheckBox *m_chkAngle;
    QPushBotton *m_btnPause;
    QPushBotton *m_btnClear;

    bool m_paused = false;
    bool m_dirty = false;
    int m_windowSecs = 60;



};

#endif // WAVEPANEL_H
