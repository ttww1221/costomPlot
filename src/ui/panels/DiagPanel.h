/**
 * @file    DiagPanel.h
 * @brief   调试收发面板 —— 展示层
 *
 * 串口调试助手功能（相当于精简版 SSCOM）：
 *  - 接收区：HEX / 文本双模式显示，可选时间戳，自动清空保护
 *  - 发送区：HEX / 文本双模式发送，HEX 格式校验
 *  - 实时显示 RX / TX 字节计数
 */

#ifndef DIAGPANEL_H
#define DIAGPANEL_H

#include <QGroupBox>

class QPlainTextEdit;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;

class DiagPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit DiagPanel(QWidget *parent = nullptr);

signals:
    void sigSendData(const QByteArray &data); // 请求发送数据
    void sigMessage(const QString &msg);      // 提示消息（转到状态栏）

public slots:
    void slotAppendRx(const QByteArray &data); // 追加接收数据显示
    void slotSetCounters(qint64 rxBytes, qint64 txBytes); // 更新计数标签

private slots:
    void slotSendClicked(); // 发送按钮 / 回车处理

private:
    // ---- 接收区 ----
    QPlainTextEdit *m_txtRx;     // 接收显示框
    QCheckBox *m_chkRxHex;       // HEX 显示开关
    QCheckBox *m_chkTimestamp;   // 时间戳开关
    QPushButton *m_btnClearRx;   // 清空接收
    QLabel *m_lblRxCount;        // RX 计数
    QLabel *m_lblTxCount;        // TX 计数

    // ---- 发送区 ----
    QLineEdit *m_edtTx;          // 发送输入框
    QCheckBox *m_chkTxHex;       // HEX 发送开关
    QPushButton *m_btnSend;      // 发送按钮
};

#endif // DIAGPANEL_H
