/**
 * @file    DialController.h
 * @brief   仪表盘控制器 —— 业务逻辑层
 *
 * 把当前温度线性映射为步进电机（仪表盘指针）角度：
 *   0~50°C（DHT11 量程）→ 0~180°，量程可配置。
 *
 * 死区设计：角度变化 >= 2° 才下发指令，两个目的：
 *   1. 抑制 DHT11 ±0.5°C 噪声导致的电机频繁抖动；
 *   2. 限制指令速率，避免淹没 ACK 通道。
 */
#ifndef DIALCONTROLLER_H
#define DIALCONTROLLER_H

#include <QObject>
#include "ProtocolEngine.h"

class DialController : public QObject
{
    Q_OBJECT

public:
    explicit DialController(QObject *parent = nullptr);

    void setRange(double tMin, double tMax);  // 设置温度量程（°C）
    void setAutoFollow(bool on);              // 开关自动跟随
    int currentAngle() const;                 // 当前映射角度

public slots:
    void slotSensorData(const SensorData &data); // 每帧传感器数据驱动一次

signals:
    void sigAngleChanged(int angle);         // 角度变化（UI滑块/波形曲线用）
    void sigSendAngleCommand(int angle);     // 需要下发0x10指令（接ControlRouter）

private:
    double m_tMin = 0.0;        // 量程下限（°C）
    double m_tMax = 50.0;       // 量程上限（°C）
    int m_currentAngle = 0;     // 当前映射角度
    int m_lastSentAngle = -1;   // 最近一次下发的角度（-1 = 尚未下发）
    bool m_autoFollow = true;   // 自动跟随开关（默认开）

    static constexpr int kAngleDeadzone = 2; // 下发死区（°）
};

#endif // DIALCONTROLLER_H
