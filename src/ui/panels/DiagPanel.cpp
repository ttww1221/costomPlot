/**
 * @file    DiagPanel.cpp
 * @brief   调试收发面板实现
 */

#include "DiagPanel.h"
#include "ConvertUtils.h"

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

DiagPanel::DiagPanel(QWidget *parent)
    : QGroupBox(QStringLiteral("数据收发 (HEX)"), parent)
{
    // ===== 接收区 =====
    m_txtRx = new QPlainTextEdit;
    m_txtRx->setReadOnly(true);
    m_txtRx->setMaximumBlockCount(8000);  // 限制行数，防止长时间运行内存无限增长
    m_txtRx->setLineWrapMode(QPlainTextEdit::NoWrap);  // HEX 显示不自动换行
    QFont monoFont(QStringLiteral("Consolas"), 9);     // 等宽字体对齐 HEX
    m_txtRx->setFont(monoFont);

    m_chkRxHex = new QCheckBox(QStringLiteral("HEX 显示"));
    m_chkRxHex->setChecked(true);
    m_chkTimestamp = new QCheckBox(QStringLiteral("时间戳"));
    m_chkTimestamp->setChecked(true);
    m_btnClearRx = new QPushButton(QStringLiteral("清空接收"));

    m_lblRxCount = new QLabel(QStringLiteral("RX: 0 B"));
    m_lblTxCount = new QLabel(QStringLiteral("TX: 0 B"));

    // ===== 发送区 =====
    m_edtTx = new QLineEdit;
    m_edtTx->setFont(monoFont);
    m_edtTx->setPlaceholderText(QStringLiteral("输入发送内容，HEX 模式如: 10 5A"));
    m_chkTxHex = new QCheckBox(QStringLiteral("HEX 发送"));
    m_chkTxHex->setChecked(true);
    m_btnSend = new QPushButton(QStringLiteral("发送"));

    // ===== 布局 =====
    QHBoxLayout *rxOptionRow = new QHBoxLayout;
    rxOptionRow->addWidget(new QLabel(QStringLiteral("接收区")));
    rxOptionRow->addStretch(1);
    rxOptionRow->addWidget(m_chkRxHex);
    rxOptionRow->addWidget(m_chkTimestamp);
    rxOptionRow->addWidget(m_btnClearRx);

    QHBoxLayout *counterRow = new QHBoxLayout;
    counterRow->addWidget(m_lblRxCount);
    counterRow->addWidget(m_lblTxCount);
    counterRow->addStretch(1);

    QHBoxLayout *txRow = new QHBoxLayout;
    txRow->addWidget(m_edtTx, 1);
    txRow->addWidget(m_btnSend);

    QHBoxLayout *txOptionRow = new QHBoxLayout;
    txOptionRow->addWidget(new QLabel(QStringLiteral("发送区")));
    txOptionRow->addStretch(1);
    txOptionRow->addWidget(m_chkTxHex);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(rxOptionRow);
    mainLayout->addWidget(m_txtRx, 1);  // 接收区占主要空间
    mainLayout->addLayout(counterRow);
    mainLayout->addLayout(txOptionRow);
    mainLayout->addLayout(txRow);

    connect(m_btnClearRx, &QPushButton::clicked, m_txtRx, &QPlainTextEdit::clear);
    connect(m_btnSend, &QPushButton::clicked, this, &DiagPanel::slotSendClicked);
    connect(m_edtTx, &QLineEdit::returnPressed, this, &DiagPanel::slotSendClicked);  // 回车也发送
}

void DiagPanel::slotAppendRx(const QByteArray &data)
{
    // 根据显示模式转换数据格式
    QString text = m_chkRxHex->isChecked() ? ConvertUtils::bytesToHex(data)
                                           : ConvertUtils::bytesToPrintable(data);
    // 时间戳前缀，便于对照发送/接收时序
    if (m_chkTimestamp->isChecked())
        text.prepend(QStringLiteral("[%1] ").arg(QTime::currentTime().toString(QStringLiteral("hh:mm:ss.zzz"))));
    m_txtRx->appendPlainText(text);
}

void DiagPanel::slotSetCounters(qint64 rxBytes, qint64 txBytes)
{
    m_lblRxCount->setText(QStringLiteral("RX: %1").arg(ConvertUtils::formatByteSize(rxBytes)));
    m_lblTxCount->setText(QStringLiteral("TX: %1").arg(ConvertUtils::formatByteSize(txBytes)));
}

void DiagPanel::slotSendClicked()
{
    const QString text = m_edtTx->text().trimmed();
    if (text.isEmpty())
        return;

    QByteArray data;
    if (m_chkTxHex->isChecked()) {
        // HEX 模式：解析失败时提示并聚焦输入框，不发数据
        bool ok = false;
        data = ConvertUtils::hexToBytes(text, &ok);
        if (!ok) {
            emit sigMessage(QStringLiteral("HEX 格式错误：仅支持成对十六进制字符，如 10 5A"));
            m_edtTx->selectAll();
            m_edtTx->setFocus();
            return;
        }
    } else {
        // 文本模式：直接按 UTF-8 编码发送
        data = text.toUtf8();
    }

    emit sigSendData(data);
}
