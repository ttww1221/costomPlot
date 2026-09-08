/**
 * @file    LedIndicator.cpp
 * @brief   自绘 LED 状态指示灯实现
 */

#include "LedIndicator.h"

#include <QPainter>

LedIndicator::LedIndicator(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(16, 16);
    setMaximumSize(24, 24);
}

void LedIndicator::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    update();  // 触发 paintEvent 重绘
}

LedIndicator::State LedIndicator::state() const
{
    return m_state;
}

void LedIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);  // 抗锯齿，圆更平滑

    // 按状态选择颜色
    QColor color;
    switch (m_state) {
    case On:
        color = QColor(0x2E, 0xCC, 0x71);  // 绿
        break;
    case Error:
        color = QColor(0xE7, 0x4C, 0x3C);  // 红
        break;
    default:
        color = QColor(0x95, 0xA5, 0xA6);  // 灰
        break;
    }

    // 主体圆形（留 2px 边距，深色描边）
    const QRectF rect = this->rect().adjusted(2, 2, -2, -2);
    painter.setPen(QPen(color.darker(140), 1));
    painter.setBrush(color);
    painter.drawEllipse(rect);

    // 左上角高光小椭圆，模拟 LED 玻璃反光
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 90));
    painter.drawEllipse(QRectF(rect.x() + rect.width() * 0.18,
                               rect.y() + rect.height() * 0.12,
                               rect.width() * 0.38,
                               rect.height() * 0.30));
}
