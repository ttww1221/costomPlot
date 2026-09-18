/**
 * @file    DialController.cpp
 * @brief   仪表盘控制器实现
 */
#include "DialController.h"

DialController::DialController(QObject *parent)
    : QObject(parent)
{
}

void DialController::setRange(double tMin, double tMax)
{
    if (tMax > tMin) {
        m_tMin = tMin;
        m_tMax = tMax;
    }
}

void DialController::setAutoFollow(bool on)
{
    m_autoFollow = on;
    if (on)
        m_lastSentAngle = -1;   // 重新开启跟随：下一帧立即下发一次校准角度
}

int DialController::currentAngle() const
{
    return m_currentAngle;
}

void DialController::slotSensorData(const SensorData &data)
{
    if (!m_autoFollow)
        return;

    // 线性映射：温度 → [0,1] → [0°,180°]，越界温度钳制到端点
    double ratio = (data.temperature - m_tMin) / (m_tMax - m_tMin);
    ratio = qBound(0.0, ratio, 1.0);
    const int angle = static_cast<int>(qRound(ratio * 180.0));

    m_currentAngle = angle;
    emit sigAngleChanged(angle);

    // 死区判断：与上次下发相比变化足够大才发指令，防电机抖动
    if (qAbs(angle - m_lastSentAngle) >= kAngleDeadzone) {
        m_lastSentAngle = angle;
        emit sigSendAngleCommand(angle);
    }
}
