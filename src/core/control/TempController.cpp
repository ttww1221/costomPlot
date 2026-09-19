/**
 * @file    TempController.cpp
 * @brief   温度闭环控制器实现（滞环单边控制）
 */
#include "TempController.h"

TempController::TempController(QObject *parent)
    : QObject(parent)
{
}

void TempController::setTarget(double target)
{
    m_target = target;
    // 设定值变化不立即动作：等下一帧数据(<=200ms)自然触发决策，
    // 避免拖动SpinBox时连续发指令
}

void TempController::setHysteresis(double hyst)
{
    if (hyst > 0.0)
        m_hyst = hyst;
}

double TempController::target() const
{
    return m_target;
}

bool TempController::fanOn() const
{
    return m_fanOn;
}

void TempController::setEnabled(bool on)
{
    m_enabled = on;
    // 进入自动模式不立即发指令：首帧数据到来时按滞环规则决策，
    // 若当前状态已符合规则则一条指令都不会发（无扰动切换）
}

void TempController::slotFanStateConfirmed(bool on)
{
    // ACK 回填：以固件确认的真实状态为准（手动按钮/自动指令共用此同步点）
    m_fanOn = on;
}

void TempController::slotSensorData(const SensorData &data)
{
    // 每帧都上报状态（UI 实时显示偏差），但指令只在状态翻转时发
    const double error = data.temperature - m_target;

    if (m_enabled) {
        // 滞环决策：死区内保持，越上界开、越下界关
        bool next = m_fanOn;
        if (data.temperature > m_target + m_hyst)
            next = true;
        else if (data.temperature < m_target - m_hyst)
            next = false;

        if (next != m_fanOn) {
            m_fanOn = next;
            emit sigFanCommand(m_fanOn);   // 经ControlRouter发0x12，ACK后再次同步
        }
    }

    emit sigStateChanged(m_fanOn, error);
}
