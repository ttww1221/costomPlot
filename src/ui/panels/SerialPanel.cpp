/**
 * @file    SerialPanel.cpp
 * @brief   串口面板实现
 */

#include "SerialPanel.h"
#include "LedIndicator.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QVBoxLayout>

SerialPanel::SerialPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("串口连接"), parent)
{
    // ===== 端口选择 =====
    m_cmbPort = new QComboBox;
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"));
    m_btnRefresh->setFixedWidth(50);

    // ===== 波特率（可编辑，支持自定义非标波特率）=====
    m_cmbBaud = new QComboBox;
    m_cmbBaud->setEditable(true);
    m_cmbBaud->addItems({QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
                         QStringLiteral("57600"), QStringLiteral("115200"), QStringLiteral("230400"),
                         QStringLiteral("460800"), QStringLiteral("921600")});
    m_cmbBaud->setCurrentText(QStringLiteral("115200"));  // 下位机固定 115200

    // ===== 数据位（5/6/7/8，itemData 存枚举值便于取出）=====
    m_cmbDataBits = new QComboBox;
    m_cmbDataBits->addItem(QStringLiteral("5"), QVariant::fromValue(QSerialPort::Data5));
    m_cmbDataBits->addItem(QStringLiteral("6"), QVariant::fromValue(QSerialPort::Data6));
    m_cmbDataBits->addItem(QStringLiteral("7"), QVariant::fromValue(QSerialPort::Data7));
    m_cmbDataBits->addItem(QStringLiteral("8"), QVariant::fromValue(QSerialPort::Data8));
    m_cmbDataBits->setCurrentIndex(3);  // 默认 8

    // ===== 停止位 =====
    m_cmbStopBits = new QComboBox;
    m_cmbStopBits->addItem(QStringLiteral("1"), QVariant::fromValue(QSerialPort::OneStop));
    m_cmbStopBits->addItem(QStringLiteral("1.5"), QVariant::fromValue(QSerialPort::OneAndHalfStop));
    m_cmbStopBits->addItem(QStringLiteral("2"), QVariant::fromValue(QSerialPort::TwoStop));

    // ===== 校验位 =====
    m_cmbParity = new QComboBox;
    m_cmbParity->addItem(QStringLiteral("None"), QVariant::fromValue(QSerialPort::NoParity));
    m_cmbParity->addItem(QStringLiteral("Even"), QVariant::fromValue(QSerialPort::EvenParity));
    m_cmbParity->addItem(QStringLiteral("Odd"), QVariant::fromValue(QSerialPort::OddParity));
    m_cmbParity->addItem(QStringLiteral("Mark"), QVariant::fromValue(QSerialPort::MarkParity));
    m_cmbParity->addItem(QStringLiteral("Space"), QVariant::fromValue(QSerialPort::SpaceParity));

    // ===== 状态指示 =====
    m_led = new LedIndicator;
    m_lblStatus = new QLabel(QStringLiteral("未连接"));
    m_btnOpenClose = new QPushButton(QStringLiteral("打开串口"));

    // ===== 布局 =====
    QHBoxLayout *portRow = new QHBoxLayout;
    portRow->addWidget(m_cmbPort, 1);
    portRow->addWidget(m_btnRefresh);

    QFormLayout *form = new QFormLayout;
    form->addRow(QStringLiteral("端口"), portRow);
    form->addRow(QStringLiteral("波特率"), m_cmbBaud);
    form->addRow(QStringLiteral("数据位"), m_cmbDataBits);
    form->addRow(QStringLiteral("停止位"), m_cmbStopBits);
    form->addRow(QStringLiteral("校验位"), m_cmbParity);

    QHBoxLayout *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_led);
    statusRow->addWidget(m_lblStatus);
    statusRow->addStretch(1);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addLayout(statusRow);
    mainLayout->addWidget(m_btnOpenClose);
    mainLayout->addStretch(1);  // 顶住上部，防止控件被拉伸

    connect(m_btnRefresh, &QPushButton::clicked, this, &SerialPanel::refreshPorts);
    connect(m_btnOpenClose, &QPushButton::clicked, this, &SerialPanel::slotOpenCloseClicked);

    refreshPorts();  // 启动时立即枚举一次
}

void SerialPanel::setConnected(bool connected)
{
    m_connected = connected;
    m_led->setState(connected ? LedIndicator::On : LedIndicator::Off);
    m_lblStatus->setText(connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    m_btnOpenClose->setText(connected ? QStringLiteral("关闭串口") : QStringLiteral("打开串口"));

    // 连接状态下锁定参数控件，防止运行中修改配置导致串口异常
    m_cmbPort->setEnabled(!connected);
    m_btnRefresh->setEnabled(!connected);
    m_cmbBaud->setEnabled(!connected);
    m_cmbDataBits->setEnabled(!connected);
    m_cmbStopBits->setEnabled(!connected);
    m_cmbParity->setEnabled(!connected);
}

SerialConfig SerialPanel::currentConfig() const
{
    SerialConfig cfg;
    cfg.portName = m_cmbPort->currentData().toString();
    cfg.baudRate = m_cmbBaud->currentText().toInt();
    cfg.dataBits = m_cmbDataBits->currentData().value<QSerialPort::DataBits>();
    cfg.stopBits = m_cmbStopBits->currentData().value<QSerialPort::StopBits>();
    cfg.parity = m_cmbParity->currentData().value<QSerialPort::Parity>();
    return cfg;
}

void SerialPanel::applyConfig(const SerialConfig &cfg)
{
    // 逐项恢复配置；若端口已不存在（设备没插），保持第一项不动
    for (int i = 0; i < m_cmbPort->count(); ++i) {
        if (m_cmbPort->itemData(i).toString() == cfg.portName) {
            m_cmbPort->setCurrentIndex(i);
            break;
        }
    }
    m_cmbBaud->setCurrentText(QString::number(cfg.baudRate));
    for (int i = 0; i < m_cmbDataBits->count(); ++i) {
        if (m_cmbDataBits->itemData(i).value<QSerialPort::DataBits>() == cfg.dataBits) {
            m_cmbDataBits->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < m_cmbStopBits->count(); ++i) {
        if (m_cmbStopBits->itemData(i).value<QSerialPort::StopBits>() == cfg.stopBits) {
            m_cmbStopBits->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < m_cmbParity->count(); ++i) {
        if (m_cmbParity->itemData(i).value<QSerialPort::Parity>() == cfg.parity) {
            m_cmbParity->setCurrentIndex(i);
            break;
        }
    }
}

void SerialPanel::refreshPorts()
{
    const QString current = m_cmbPort->currentData().toString();
    m_cmbPort->clear();

    // 枚举系统所有串口；Windows 下 CH340 显示为 "COM3 [USB-SERIAL CH340]"
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString text = info.portName();
        if (!info.description().isEmpty())
            text += QStringLiteral("  [%1]").arg(info.description());
        m_cmbPort->addItem(text, info.portName());
    }

    // 尽量保持刷新前的选择（热插拔时用户体验更好）
    if (!current.isEmpty()) {
        for (int i = 0; i < m_cmbPort->count(); ++i) {
            if (m_cmbPort->itemData(i).toString() == current) {
                m_cmbPort->setCurrentIndex(i);
                return;
            }
        }
    }
}

void SerialPanel::slotOpenCloseClicked()
{
    // 已连接 → 请求关闭
    if (m_connected) {
        emit sigCloseRequested();
        return;
    }

    // 未连接 → 检查端口有效性后请求打开
    const SerialConfig cfg = currentConfig();
    if (cfg.portName.isEmpty()) {
        m_lblStatus->setText(QStringLiteral("未发现串口"));
        emit sigMessage(QStringLiteral("未发现可用串口，请检查 CH340 连接后点击刷新"));
        return;
    }
    emit sigOpenRequested(cfg);
}
