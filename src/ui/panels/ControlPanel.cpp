/**
 * @file    ControlPanel.cpp
 * @brief   控制面板实现
 */
#include "ControlPanel.h"
#include "ProtocolDefs.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>

ControlPanel::ControlPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("控制面板"), parent)
{
    // ===== 控制模式 =====
    m_radioManual = new QRadioButton(QStringLiteral("手动"));
    m_radioAuto = new QRadioButton(QStringLiteral("自动"));
    m_radioManual->setChecked(true);

    m_spinTarget = new QDoubleSpinBox;
    m_spinTarget->setRange(0.0, 50.0);       // DHT11 量程
    m_spinTarget->setValue(25.0);
    m_spinTarget->setDecimals(1);
    m_spinTarget->setSingleStep(0.5);
    m_spinTarget->setSuffix(QStringLiteral(" °C"));

    // ===== 风扇（checkable 按钮：按下 = 开）=====
    m_btnFan = new QPushButton(QStringLiteral("风扇: 关"));
    m_btnFan->setCheckable(true);

    // ===== 仪表盘 =====
    m_chkAutoFollow = new QCheckBox(QStringLiteral("自动跟随温度 (0~50°C → 0~180°)"));
    m_chkAutoFollow->setChecked(true);
    m_sliderAngle = new QSlider(Qt::Horizontal);
    m_sliderAngle->setRange(0, 180);
    m_sliderAngle->setEnabled(false);        // 默认自动跟随 → 手动滑块禁用
    m_lblAngle = new QLabel(QStringLiteral("0°"));
    m_btnSendAngle = new QPushButton(QStringLiteral("发送角度"));
    m_btnSendAngle->setEnabled(false);
    m_btnHome = new QPushButton(QStringLiteral("回零"));

    // ===== 指令状态 =====
    m_lblAutoState = new QLabel(QStringLiteral("手动模式"));
    m_lblAutoState->setStyleSheet(QStringLiteral("color: #2980b9; font-weight: bold;"));
    m_lblStatus = new QLabel(QStringLiteral("空闲"));
    m_lblStatus->setStyleSheet(QStringLiteral("color: #7f8c8d;"));
    m_lblStatus->setWordWrap(true);

    // ===== 布局 =====
    QHBoxLayout *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(QStringLiteral("模式:")));
    modeRow->addWidget(m_radioManual);
    modeRow->addWidget(m_radioAuto);
    modeRow->addStretch(1);

    QHBoxLayout *targetRow = new QHBoxLayout;
    targetRow->addWidget(new QLabel(QStringLiteral("目标温度:")));
    targetRow->addWidget(m_spinTarget, 1);

    QHBoxLayout *sliderRow = new QHBoxLayout;
    sliderRow->addWidget(m_sliderAngle, 1);
    sliderRow->addWidget(m_lblAngle);

    QHBoxLayout *dialBtnRow = new QHBoxLayout;
    dialBtnRow->addWidget(m_btnSendAngle);
    dialBtnRow->addWidget(m_btnHome);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(modeRow);
    mainLayout->addLayout(targetRow);
    mainLayout->addWidget(m_btnFan);
    mainLayout->addWidget(m_chkAutoFollow);
    mainLayout->addLayout(sliderRow);
    mainLayout->addLayout(dialBtnRow);
    mainLayout->addWidget(m_lblAutoState);
    mainLayout->addWidget(m_lblStatus);
    mainLayout->addStretch(1);

    // ===== 信号连接 =====
    // 模式切换：发 0x20 让固件记录模式；自动模式下禁用风扇手动按钮
    connect(m_radioAuto, &QRadioButton::toggled, this, [this](bool autoOn) {
        m_autoMode = autoOn;
        m_btnFan->setEnabled(!autoOn);   // 自动模式风扇由 S5 温控器接管
        emit sigCommandRequested(Proto::CMD_AUTO_MODE, autoOn ? 1 : 0);
        emit sigModeChanged(autoOn);
    });

    // 目标温度变化 → 波形设定值曲线（S5 还将接温控器）
    connect(m_spinTarget, &QDoubleSpinBox::valueChanged,
            this, [this](double v) { emit sigTargetChanged(v); });

    // 风扇按钮：发出指令，按钮真实状态等 ACK 确认后由 slotFanStateConfirmed 更新
    connect(m_btnFan, &QPushButton::clicked, this, [this](bool checked) {
        emit sigCommandRequested(Proto::CMD_FAN_CTRL, checked ? 1 : 0);
        m_lblStatus->setText(checked ? QStringLiteral("指令已发出: 风扇开...")
                                     : QStringLiteral("指令已发出: 风扇关..."));
    });

    // 仪表自动跟随开关：切换滑块可用性并通知 DialController
    connect(m_chkAutoFollow, &QCheckBox::toggled, this, [this](bool on) {
        m_sliderAngle->setEnabled(!on);
        m_btnSendAngle->setEnabled(!on);
        emit sigAutoFollowChanged(on);
    });

    // 滑块拖动实时显示数值
    connect(m_sliderAngle, &QSlider::valueChanged,
            this, [this](int v) { m_lblAngle->setText(QStringLiteral("%1°").arg(v)); });

    // 发送角度 / 回零
    connect(m_btnSendAngle, &QPushButton::clicked, this, [this]() {
        emit sigCommandRequested(Proto::CMD_MOTOR_ANGLE,
                                 static_cast<quint8>(m_sliderAngle->value()));
    });
    connect(m_btnHome, &QPushButton::clicked, this, [this]() {
        emit sigCommandRequested(Proto::CMD_MOTOR_HOME, 0);
        m_sliderAngle->setValue(0);
    });
}

void ControlPanel::setConnected(bool connected)
{
    setEnabled(connected);   // 未连接串口时整板置灰
}

bool ControlPanel::autoMode() const
{
    return m_autoMode;
}

double ControlPanel::targetTemp() const
{
    return m_spinTarget->value();
}

void ControlPanel::slotShowStatus(const QString &text)
{
    m_lblStatus->setText(text);
}

void ControlPanel::slotShowAutoState(const QString &text)
{
    m_lblAutoState->setText(text);
}

void ControlPanel::slotUpdateAngle(int angle)
{
    // 仅自动跟随模式下同步滑块；手动模式不抢用户的滑块
    if (m_chkAutoFollow->isChecked())
        m_sliderAngle->setValue(angle);
}

void ControlPanel::slotFanStateConfirmed(bool on)
{
    // ACK 确认后更新按钮真实状态（clicked 信号不会被 setChecked 触发）
    m_btnFan->setChecked(on);
    m_btnFan->setText(on ? QStringLiteral("风扇: 开") : QStringLiteral("风扇: 关"));
    m_lblStatus->setText(on ? QStringLiteral("风扇开 ✓（ACK已确认）")
                            : QStringLiteral("风扇关 ✓（ACK已确认）"));
}
