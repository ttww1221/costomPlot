/**
 * @file    ControlPanel.h
 * @brief   控制面板 —— 展示层
 *
 * 三块功能区：
 *  1. 控制模式：手动/自动单选（切换时发 0x20 通知固件记录模式）
 *     + 目标温度设定（自动模式的控温目标，S5 接入闭环逻辑）
 *  2. 风扇手动开关（自动模式下禁用，风扇交由 S5 温控器接管）
 *  3. 仪表盘：自动跟随开关 + 手动角度滑块 + 发送角度/回零按钮
 *
 * 面板不直接接触串口，所有指令统一经 sigCommandRequested 交给 ControlRouter。
 */
#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <QGroupBox>

class QRadioButton;
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;
class QSlider;
class QLabel;

class ControlPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget *parent = nullptr);

    void setConnected(bool connected);  // 串口开关联动整板使能
    bool autoMode() const;              // 当前是否自动模式
    double targetTemp() const;          // 当前目标温度

public slots:
    void slotShowStatus(const QString &text);  // 显示指令过程状态
    void slotShowAutoState(const QString &text); // 显示自动温控状态（S5）
    void slotUpdateAngle(int angle);           // 自动跟随时同步滑块位置
    void slotFanStateConfirmed(bool on);       // 风扇ACK确认后同步按钮状态

signals:
    void sigCommandRequested(quint8 cmd, quint8 param); // 请求下发指令（接ControlRouter）
    void sigModeChanged(bool autoMode);                 // 手动/自动切换（S5接温控器）
    void sigTargetChanged(double target);               // 目标温度变化（接波形设定值曲线）
    void sigAutoFollowChanged(bool on);                 // 仪表自动跟随开关（接DialController）

private:
    QRadioButton *m_radioManual;   // 手动模式
    QRadioButton *m_radioAuto;     // 自动模式
    QDoubleSpinBox *m_spinTarget;  // 目标温度 0~50°C
    QPushButton *m_btnFan;         // 风扇开关（checkable，ACK确认后更新状态）
    QCheckBox *m_chkAutoFollow;    // 仪表自动跟随温度
    QSlider *m_sliderAngle;        // 手动角度滑块 0~180
    QLabel *m_lblAngle;            // 滑块当前值
    QPushButton *m_btnSendAngle;   // 发送角度
    QPushButton *m_btnHome;        // 仪表回零
    QLabel *m_lblStatus;           // 指令状态反馈
    QLabel *m_lblAutoState;        // 自动温控状态显示（S5）
    bool m_autoMode = false;
};

#endif // CONTROLPANEL_H
