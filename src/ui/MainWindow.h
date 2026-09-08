/**
 * @file    MainWindow.h
 * @brief   主窗口 —— 展示层
 *
 * 布局（三栏 Splitter，参照设计文档 6.1 节）：
 *   ┌──────────┬──────────────────────┬────────────────────┐
 *   │ 串口面板  │      波形显示区        │  数据面板 / 调试面板 │
 *   ├──────────┴──────────────────────┴────────────────────┤
 *   │ 状态栏：连接状态 | 串口参数 | 温度 | RX/TX | 运行时间    │
 *   └──────────────────────────────────────────────────────┘
 *
 * 职责：创建各面板与核心服务，用信号槽把它们"接线"（信号总线模式），
 * 不持有任何业务逻辑。
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include "SerialManager.h"

class SerialPanel;
class WavePanel;
class DataPanel;
class DiagPanel;
class SerialManager;
class ProtocolEngine;
class QLabel;
class QTimer;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // 关闭窗口时：关闭串口 + 保存串口参数与窗口几何信息
    void closeEvent(QCloseEvent *event) override;

private slots:
    void slotPortOpened(const SerialConfig &cfg);  // 串口打开成功：刷新面板与状态栏，启动计时
    void slotPortClosed();                         // 串口关闭：复位各显示
    void slotSerialError(const QString &msg);      // 串口错误：状态栏提示
    void slotBytesChanged(qint64 rxBytes, qint64 txBytes); // 收发计数变化
    void slotSensorData(double temperature, double humidity); // 收到解析后的温湿度
    void slotUpdateRuntime();                      // 每秒刷新运行时间显示

private:
    void setupUi();          // 构建三栏布局
    void setupMenuBar();     // 菜单栏（文件/帮助）
    void setupStatusBar();   // 状态栏各标签
    void setupSignals();     // 核心信号槽接线（信号总线）

    // 把串口配置格式化为 "115200bps 8N1" 形式的状态栏文本
    static QString formatConfig(const SerialConfig &cfg);

    SerialManager *m_serialManager;  // 串口管理器（核心服务）
    ProtocolEngine *m_protocolEngine; // 协议解析引擎（核心服务）
    SerialPanel *m_serialPanel;      // 串口面板（左）
    WavePanel *m_wavePanel;          // 波形面板（中）
    DataPanel *m_dataPanel;          // 数据面板（右上）
    DiagPanel *m_diagPanel;          // 调试收发面板（右下）

    QLabel *m_lblConn;     // 状态栏：连接状态
    QLabel *m_lblParams;   // 状态栏：串口参数
    QLabel *m_lblTemp;     // 状态栏：实时温湿度
    QLabel *m_lblCounters; // 状态栏：RX/TX 计数
    QLabel *m_lblRuntime;  // 状态栏：运行时间

    QTimer *m_runtimeTimer;  // 运行时间刷新定时器（1s）
    QElapsedTimer m_elapsed; // 运行计时器
};

#endif // MAINWINDOW_H
