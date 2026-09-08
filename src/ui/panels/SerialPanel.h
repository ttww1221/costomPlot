/**
 * @file    SerialPanel.h
 * @brief   串口面板 —— 展示层
 *
 * 提供串口参数配置界面：
 *  - 端口选择（自动枚举 + 手动刷新，支持 CH340 热插拔后刷新）
 *  - 波特率 / 数据位 / 停止位 / 校验位
 *  - 打开 / 关闭按钮 + LED 连接状态指示
 *
 * 面板本身不操作串口，只把用户配置通过 sigOpenRequested 交给
 * SerialManager 处理，实现 UI 与核心逻辑解耦。
 */

#ifndef SERIALPANEL_H
#define SERIALPANEL_H

#include <QGroupBox>
#include "SerialManager.h"

class QComboBox;
class QPushButton;
class QLabel;
class LedIndicator;

class SerialPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit SerialPanel(QWidget *parent = nullptr);

    // 切换面板的连接状态显示（LED、按钮文字、参数控件可用性）
    void setConnected(bool connected);

    // 收集当前界面上的串口配置
    SerialConfig currentConfig() const;

    // 把已保存的配置恢复到界面上（下拉框选中对应项）
    void applyConfig(const SerialConfig &cfg);

signals:
    void sigOpenRequested(const SerialConfig &cfg); // 用户点击"打开"（携带配置）
    void sigCloseRequested();                       // 用户点击"关闭"
    void sigMessage(const QString &msg);            // 提示消息（转到状态栏）

public slots:
    void refreshPorts(); // 重新枚举系统串口列表

private slots:
    void slotOpenCloseClicked(); // 打开/关闭按钮点击处理

private:
    QComboBox *m_cmbPort;      // 端口下拉框
    QComboBox *m_cmbBaud;      // 波特率（可编辑）
    QComboBox *m_cmbDataBits;  // 数据位
    QComboBox *m_cmbStopBits;  // 停止位
    QComboBox *m_cmbParity;    // 校验位
    QPushButton *m_btnRefresh; // 刷新端口列表
    QPushButton *m_btnOpenClose;// 打开/关闭按钮
    LedIndicator *m_led;       // 连接状态 LED
    QLabel *m_lblStatus;       // 状态文字（已连接/未连接）
    bool m_connected = false;  // 当前是否已连接
};

#endif // SERIALPANEL_H
