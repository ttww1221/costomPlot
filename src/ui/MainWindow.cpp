/**
 * @file    MainWindow.cpp
 * @brief   主窗口实现
 *
 * 信号总线接线关系（setupSignals）：
 *
 *  SerialPanel.sigOpenRequested ──► SerialManager.slotOpenPort
 *  SerialManager.sigDataReceived ─► ProtocolEngine.slotFeed   （协议解析）
 *                                 └► DiagPanel.slotAppendRx    （HEX 显示）
 *  ProtocolEngine.sigFrameParsed ─► DataPanel.slotUpdateData  （数值显示）
 *                                 └► MainWindow.slotSensorData （状态栏）
 *  DiagPanel.sigSendData ────────► SerialManager.slotSend
 *
 * 各模块之间互不依赖，只通过信号交互，新增面板只需"接线"。
 */

#include "MainWindow.h"
#include "AppSettings.h"
#include "ConvertUtils.h"
#include "DataPanel.h"
#include "DiagPanel.h"
#include "ProtocolEngine.h"
#include "SerialPanel.h"
#include "WavePanel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // 注册自定义类型的元类型，保证信号参数能跨线程传递
    qRegisterMetaType<SensorData>();
    qRegisterMetaType<ProtoStats>();

    m_serialManager = new SerialManager(this);
    m_protocolEngine = new ProtocolEngine(this);

    setupUi();
    setupMenuBar();
    setupStatusBar();
    setupSignals();

    setWindowTitle(QStringLiteral("串口数据采集与测控上位机"));
    resize(1200, 760);

    // 恢复上次关闭时的窗口位置与大小
    const QByteArray geometry = AppSettings::loadGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    // 恢复上次使用的串口参数
    m_serialPanel->applyConfig(AppSettings::loadSerialConfig());
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    // ===== 三个主要面板 =====
    m_serialPanel = new SerialPanel;
    m_wavePanel = new WavePanel;
    m_dataPanel = new DataPanel;
    m_diagPanel = new DiagPanel;

    // 左侧串口面板固定宽度，右侧调试面板给最小宽度
    m_serialPanel->setMinimumWidth(240);
    m_diagPanel->setMinimumWidth(340);

    // 右侧竖排：数据面板（上）+ 调试面板（下，占主要空间）
    QSplitter *rightColumn = new QSplitter(Qt::Vertical);
    rightColumn->addWidget(m_dataPanel);
    rightColumn->addWidget(m_diagPanel);
    rightColumn->setStretchFactor(0, 0);
    rightColumn->setStretchFactor(1, 1);
    rightColumn->setSizes({220, 420});

    // 整体三栏：串口 | 波形 | 右列
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_serialPanel);
    splitter->addWidget(m_wavePanel);
    splitter->addWidget(rightColumn);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({260, 560, 380});

    setCentralWidget(splitter);
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(QStringLiteral("退出(&X)"), this, &MainWindow::close);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(QStringLiteral("关于(&A)"), this, [this]() {
        QMessageBox::about(this, QStringLiteral("关于"),
                           QStringLiteral("串口数据采集与测控上位机 v0.1.0\n"
                                          "Qt %1 / C++17\n\n"
                                          "下位机: STM32F103C8T6 + DHT11 + 28BYJ-48")
                               .arg(QString::fromLatin1(qVersion())));
    });
}

void MainWindow::setupStatusBar()
{
    // 左侧：连接状态 + 串口参数 + 实时温湿度
    // 右侧（常驻）：收发计数 + 运行时间
    m_lblConn = new QLabel(QStringLiteral("● 未连接"));
    m_lblParams = new QLabel;
    m_lblTemp = new QLabel(QStringLiteral("温度: --"));
    m_lblCounters = new QLabel(QStringLiteral("RX: 0 B   TX: 0 B"));
    m_lblRuntime = new QLabel(QStringLiteral("运行时间 00:00:00"));

    statusBar()->addWidget(m_lblConn);
    statusBar()->addWidget(m_lblParams);
    statusBar()->addWidget(m_lblTemp);
    statusBar()->addPermanentWidget(m_lblCounters);
    statusBar()->addPermanentWidget(m_lblRuntime);
}

void MainWindow::setupSignals()
{
    // ---------- 串口开关 ----------
    connect(m_serialPanel, &SerialPanel::sigOpenRequested,
            m_serialManager, &SerialManager::slotOpenPort);
    connect(m_serialPanel, &SerialPanel::sigCloseRequested,
            m_serialManager, &SerialManager::slotClosePort);

    // ---------- 数据流：串口 → 协议解析 / HEX 显示 ----------
    connect(m_serialManager, &SerialManager::sigDataReceived,
            m_protocolEngine, &ProtocolEngine::slotFeed);
    connect(m_serialManager, &SerialManager::sigDataReceived,
            m_diagPanel, &DiagPanel::slotAppendRx);

    // ---------- 解析结果：数据面板 / 状态栏 ----------
    connect(m_protocolEngine, &ProtocolEngine::sigFrameParsed,
            m_dataPanel, &DataPanel::slotUpdateData);
    connect(m_protocolEngine, &ProtocolEngine::sigFrameParsed, this,
            [this](const SensorData &d) { slotSensorData(d.temperature, d.humidity); });
    connect(m_protocolEngine, &ProtocolEngine::sigStatsChanged,
            m_dataPanel, &DataPanel::slotUpdateStats);

    // ---------- 清零统计：协议引擎 + 面板联动复位 ----------
    connect(m_dataPanel, &DataPanel::sigResetRequested, m_protocolEngine, &ProtocolEngine::reset);
    connect(m_dataPanel, &DataPanel::sigResetRequested, m_dataPanel, &DataPanel::slotReset);

    // ---------- 串口状态 → 主窗口 ----------
    connect(m_serialManager, &SerialManager::sigPortOpened, this, &MainWindow::slotPortOpened);
    connect(m_serialManager, &SerialManager::sigPortClosed, this, &MainWindow::slotPortClosed);
    connect(m_serialManager, &SerialManager::sigError, this, &MainWindow::slotSerialError);
    connect(m_serialManager, &SerialManager::sigBytesChanged, this, &MainWindow::slotBytesChanged);
    connect(m_serialManager, &SerialManager::sigBytesChanged, m_diagPanel, &DiagPanel::slotSetCounters);

    // ---------- 手动发送 ----------
    connect(m_diagPanel, &DiagPanel::sigSendData, m_serialManager, &SerialManager::slotSend);

    // ---------- 面板消息 → 状态栏短暂提示 ----------
    connect(m_diagPanel, &DiagPanel::sigMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    connect(m_serialPanel, &SerialPanel::sigMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });

    // ---------- 运行时间每秒刷新 ----------
    m_runtimeTimer = new QTimer(this);
    m_runtimeTimer->setInterval(1000);
    connect(m_runtimeTimer, &QTimer::timeout, this, &MainWindow::slotUpdateRuntime);
}

void MainWindow::slotPortOpened(const SerialConfig &cfg)
{
    // 面板切到"已连接"状态，状态栏显示串口名与参数
    m_serialPanel->setConnected(true);
    m_lblConn->setText(QStringLiteral("● %1 已连接").arg(cfg.portName));
    m_lblParams->setText(QStringLiteral("  |  %1").arg(formatConfig(cfg)));
    statusBar()->showMessage(QStringLiteral("串口 %1 已打开").arg(cfg.portName), 3000);

    // 启动运行计时
    m_elapsed.restart();
    m_runtimeTimer->start();
    slotUpdateRuntime();
}

void MainWindow::slotPortClosed()
{
    m_serialPanel->setConnected(false);
    m_lblConn->setText(QStringLiteral("● 未连接"));
    m_lblParams->clear();
    m_runtimeTimer->stop();
}

void MainWindow::slotSerialError(const QString &msg)
{
    statusBar()->showMessage(QStringLiteral("串口错误: %1").arg(msg), 5000);
}

void MainWindow::slotBytesChanged(qint64 rxBytes, qint64 txBytes)
{
    m_lblCounters->setText(QStringLiteral("RX: %1   TX: %2")
                               .arg(ConvertUtils::formatByteSize(rxBytes),
                                    ConvertUtils::formatByteSize(txBytes)));
}

void MainWindow::slotSensorData(double temperature, double humidity)
{
    m_lblTemp->setText(QStringLiteral("温度: %1°C  湿度: %2%")
                           .arg(temperature, 0, 'f', 1)
                           .arg(humidity, 0, 'f', 1));
}

void MainWindow::slotUpdateRuntime()
{
    // 把累计毫秒数格式化为 时:分:秒
    const qint64 secs = m_elapsed.elapsed() / 1000;
    m_lblRuntime->setText(QStringLiteral("运行时间 %1:%2:%3")
                              .arg(secs / 3600, 2, 10, QLatin1Char('0'))
                              .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
                              .arg(secs % 60, 2, 10, QLatin1Char('0')));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 退出前：关闭串口 + 保存串口参数 + 保存窗口几何信息
    m_serialManager->slotClosePort();
    AppSettings::saveSerialConfig(m_serialPanel->currentConfig());
    AppSettings::saveGeometry(saveGeometry());
    event->accept();
}

QString MainWindow::formatConfig(const SerialConfig &cfg)
{
    // 校验位 → 单字符表示（None=N, Even=E, Odd=O, Mark=M, Space=S）
    QChar parityChar = QLatin1Char('N');
    switch (cfg.parity) {
    case QSerialPort::EvenParity:
        parityChar = QLatin1Char('E');
        break;
    case QSerialPort::OddParity:
        parityChar = QLatin1Char('O');
        break;
    case QSerialPort::MarkParity:
        parityChar = QLatin1Char('M');
        break;
    case QSerialPort::SpaceParity:
        parityChar = QLatin1Char('S');
        break;
    default:
        break;
    }

    // 停止位文本（1 / 1.5 / 2）
    QString stopText = QStringLiteral("1");
    if (cfg.stopBits == QSerialPort::OneAndHalfStop)
        stopText = QStringLiteral("1.5");
    else if (cfg.stopBits == QSerialPort::TwoStop)
        stopText = QStringLiteral("2");

    // 例：115200bps 8N1
    return QStringLiteral("%1bps %2%3%4").arg(cfg.baudRate).arg(cfg.dataBits).arg(parityChar).arg(stopText);
}
