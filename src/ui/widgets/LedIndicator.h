/**
 * @file    LedIndicator.h
 * @brief   自绘 LED 状态指示灯
 *
 * 用于串口面板连接状态指示：
 *  - Off   : 灰色（未连接）
 *  - On    : 绿色（已连接）
 *  - Error : 红色（出错）
 */

#ifndef LEDINDICATOR_H
#define LEDINDICATOR_H

#include <QWidget>

class LedIndicator : public QWidget
{
    Q_OBJECT

public:
    enum State {
        Off,
        On,
        Error
    };

    explicit LedIndicator(QWidget *parent = nullptr);

    void setState(State state); // 设置状态并触发重绘
    State state() const;

protected:
    // 自绘圆形指示灯 + 高光点缀
    void paintEvent(QPaintEvent *event) override;

private:
    State m_state = Off;  // 当前状态
};

#endif // LEDINDICATOR_H
