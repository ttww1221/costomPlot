/**
 * @file    TempController.h
 * @brief   温度闭环控制器 —— 业务逻辑层（S5）
 *
 * 控制对象特性：风扇只能降温（单边执行器），不能加热，
 * 因此采用"滞环（死区）开关控制"而非双向 PID：
 *   温度 > 设定值 + 滞环宽度  → 开风扇
 *   温度 < 设定值 - 滞环宽度  → 关风扇
 *   死区之内                  → 保持原状态
 * 死区的作用：DHT11 有 ±0.5°C 级噪声，若无死区，温度在设定值附近
 * 抖动会导致风扇每秒启停数次（继电器/电机寿命 + 观感都不可接受）。
 *
 * 状态同步：m_fanOn 不靠自己记忆，而是由风扇指令的 ACK 回填
 * （slotFanStateConfirmed），保证"手动→自动"切换时控制器知道
 * 风扇的真实状态，不会发出错误的首条指令。
 */
#ifndef TEMPCONTROLLER_H
#define TEMPCONTROLLER_H

#include <QObject>
#include "ProtocolEngine.h"

class TempController : public QObject
{
    Q_OBJECT

public:
    explicit TempController(QObject *parent = nullptr);

    void setTarget(double target);        // 设定温度（°C）
    void setHysteresis(double hyst);      // 滞环宽度（°C，单边）
    double target() const;
    bool fanOn() const;                   // 控制器当前认定的风扇状态

public slots:
    void setEnabled(bool on);             // 自动模式开关（接管/释放风扇）
    void slotSensorData(const SensorData &data); // 每帧传感器数据驱动一次决策
    void slotFanStateConfirmed(bool on);  // 风扇ACK回填真实状态

signals:
    void sigFanCommand(bool on);                  // 风扇开关指令（接ControlRouter发0x12）
    void sigStateChanged(bool fanOn, double error); // 状态反馈（error = 实测-设定，给UI显示）

private:
    double m_target = 25.0;   // 设定温度（与ControlPanel默认值一致）
    double m_hyst = 0.5;      // 滞环宽度（单边0.5°C，总死区1°C）
    bool m_enabled = false;   // 自动模式默认关（手动模式风扇归按钮管）
    bool m_fanOn = false;     // 当前风扇状态（由ACK同步）
};

#endif // TEMPCONTROLLER_H
