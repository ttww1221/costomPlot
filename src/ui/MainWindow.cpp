/**
 * @file    MainWindow.cpp
 * @brief   主窗口实现
 *
 * 信号总线接线关系（setupSignals）：
 *
 *  SerialPanel.sigOpenRequested ──► SerialManager.slotOpenPort
 *  SerialManager.sigDataReceived ─► ProtocolEngine.slotFeed   （协议解析）
 *                                 └► DiagPanel.slotAppendRx    （HEX 显示）
 *
 *  ── 显示链路（S6 起经"数据源仲裁"）──────────────────────────
 *  ProtocolEngine.sigFrameParsed ─► MainWindow(仲裁) ─► dispatchToView
 *  LogReplayer.sigFrame ──────────► MainWindow(仲裁) ─┘        │
 *                                                              ├► DataPanel   数值
 *                                                              ├► WavePanel   波形
 *                                                              ├► StatEngine  统计
 *                                                              └► 状态栏      温湿度
 *  仲裁规则：回放进行中时实时帧不进显示链路（避免两路数据在波形上交错），
 *           回放帧不写 CSV、不喂报警引擎（历史数据不得污染新日志与新报警）。
 *
 *  ── 控制链路（始终只吃实时帧，回放不影响闭环安全）───────────
 *  ProtocolEngine.sigFrameParsed ─► TempController / DialController
 *  ProtocolEngine.sigAckReceived ─► ControlRouter.slotAckReceived
 *  ControlPanel.sigCommandRequested ─► ControlRouter.slotSendCommand
 *  ControlRouter.sigSendFrame ────► SerialManager.slotSend
 *  DiagPanel.sigSendData ────────► SerialManager.slotSend
 *
 *  ── S6 记录与回放 ──────────────────────────────────────────
 *  LogPanel.sigStart/StopRecording ─► CsvRecorder.start/stop
 *  LogPanel.sigLoadFile/Play/Pause/Stop/Seek/Speed ─► LogReplayer
 *
 *  ── S7 统计 ────────────────────────────────────────────────
 *  StatEngine.sigSnapshot(1Hz) ─► StatPanel.slotUpdateSnapshot
 *  ProtocolEngine.sigStatsChanged ─► StatEngine.slotProtoStats（丢包率来源）
 *  SerialManager.sigBytesChanged ─► StatEngine.slotBytesChanged（速率来源）
 *
 *  ── S8 数据库 / 权限 / 报警 ────────────────────────────────
 *  Session.sigPermissionsChanged ─► MainWindow.applyPermissions（唯一门控点）
 *  Session.sigUserChanged ─► AuditService.slotCurrentUser（审计自动带上操作者）
 *  AlarmEngine.sigAlarm ─► AlarmStore.insert ─► AlarmPanel（顺序不可颠倒：
 *                            面板重新查库刷新表格，必须先落库）
 *  AlarmPanel.sigThresholdsChanged ─► AlarmEngine.setThresholds + 配置持久化
 *  各关键操作 ─► AuditService.log（见 audit() 调用点）
 *
 * 各模块之间互不依赖，只通过信号交互，新增面板只需"接线"。
 */

#include "MainWindow.h"
#include "AlarmEngine.h"
#include "AlarmPanel.h"
#include "AlarmStore.h"
#include "AppSettings.h"
#include "AuditService.h"
#include "ControlPanel.h"
#include "ControlRouter.h"
#include "ConvertUtils.h"
#include "CsvRecorder.h"
#include "Database.h"
#include "DataPanel.h"
#include "DiagPanel.h"
#include "DialController.h"
#include "LogPanel.h"
#include "LogReplayer.h"
#include "LoginDialog.h"
#include "ProtocolDefs.h"
#include "ProtocolEngine.h"
#include "SerialPanel.h"
#include "Session.h"
#include "StatEngine.h"
#include "StatPanel.h"
#include "TempController.h"
#include "UserManagerDialog.h"
#include "UserService.h"
#include "WavePanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(Session *session, Database *db, QWidget *parent)
    : QMainWindow(parent)
    , m_db(db)
    , m_session(session)
{
    // 注册自定义类型的元类型，保证信号参数能跨线程传递
    qRegisterMetaType<SensorData>();
    qRegisterMetaType<ProtoStats>();
    qRegisterMetaType<StatSnapshot>();
    qRegisterMetaType<AlarmEvent>();
    qRegisterMetaType<AlarmThresholds>();

    // ---- 数据访问层（S8）----
    m_userService = new UserService(m_db, this);
    m_auditService = new AuditService(m_db, this);
    m_alarmStore = new AlarmStore(m_db, this);

    // ---- 业务与核心服务 ----
    m_alarmEngine = new AlarmEngine(this);        // S8 阈值报警
    m_serialManager = new SerialManager(this);
    m_protocolEngine = new ProtocolEngine(this);
    m_controlRouter = new ControlRouter(this);
    m_dialController = new DialController(this);
    m_tempController = new TempController(this);
    m_csvRecorder = new CsvRecorder(this);        // S6 数据持久化
    m_logReplayer = new LogReplayer(this);        // S6 历史回放
    m_statEngine = new StatEngine(this);          // S7 统计分析

    setupUi();
    setupMenuBar();
    setupStatusBar();
    setupSignals();

    // 恢复上次保存的报警阈值，并回填到面板控件
    if (m_alarmEngine->setThresholds(AppSettings::loadAlarmThresholds()))
        m_alarmPanel->slotThresholdsChanged(m_alarmEngine->thresholds());

    setWindowTitle(QStringLiteral("串口数据采集与测控上位机"));
    resize(1320, 840);

    // 恢复上次关闭时的窗口位置与大小
    const QByteArray geometry = AppSettings::loadGeometry();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    // 恢复上次使用的串口参数
    m_serialPanel->applyConfig(AppSettings::loadSerialConfig());

    applyPermissions();   // 按登录角色初始化各面板可用性
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    // ===== 各面板 =====
    m_serialPanel = new SerialPanel;
    m_wavePanel = new WavePanel;
    m_dataPanel = new DataPanel;
    m_statPanel = new StatPanel(m_statEngine);
    m_alarmPanel = new AlarmPanel(m_alarmStore);
    m_diagPanel = new DiagPanel;
    m_controlPanel = new ControlPanel;
    m_controlPanel->setEnabled(false);   // 未连接串口时禁用
    m_logPanel = new LogPanel;

    // 左侧串口面板固定宽度，右侧调试面板给最小宽度
    m_serialPanel->setMinimumWidth(250);
    m_controlPanel->setMinimumWidth(250);
    m_logPanel->setMinimumWidth(250);
    m_diagPanel->setMinimumWidth(340);

    // 左侧竖排：串口面板 / 控制面板 / 记录回放面板（参照设计文档 6.1 布局）
    QWidget *leftColumn = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);
    leftLayout->addWidget(m_serialPanel);
    leftLayout->addWidget(m_controlPanel);
    leftLayout->addWidget(m_logPanel);
    leftLayout->addStretch(1);

    // 右上：实时数据 / 统计分析 / 报警记录 三个选项卡
    // 用 Tab 聚合而不是继续竖排，是为了在有限窗口高度内
    // 同时容纳 S7 统计面板与 S8 报警面板而不挤压波形与调试区
    QTabWidget *rightTabs = new QTabWidget;
    rightTabs->addTab(m_dataPanel, QStringLiteral("实时数据"));
    rightTabs->addTab(m_statPanel, QStringLiteral("统计分析"));
    rightTabs->addTab(m_alarmPanel, QStringLiteral("报警记录"));

    // 右侧竖排：选项卡（上）+ 调试面板（下）
    QSplitter *rightColumn = new QSplitter(Qt::Vertical);
    rightColumn->addWidget(rightTabs);
    rightColumn->addWidget(m_diagPanel);
    rightColumn->setStretchFactor(0, 1);
    rightColumn->setStretchFactor(1, 1);
    rightColumn->setSizes({430, 320});

    // 整体三栏：左列 | 波形 | 右列
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(leftColumn);
    splitter->addWidget(m_wavePanel);
    splitter->addWidget(rightColumn);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({270, 540, 460});

    setCentralWidget(splitter);
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    // 记录：菜单与 LogPanel 按钮共用同一个记录器，状态经信号双向同步
    fileMenu->addAction(QStringLiteral("开始记录(&R)"), this, [this]() {
        if (!Permission::canRecord(m_session->role())) {
            statusBar()->showMessage(QStringLiteral("当前角色无数据记录权限"), 3000);
            return;
        }
        if (m_csvRecorder->isRecording()) {
            statusBar()->showMessage(QStringLiteral("已在记录中"), 3000);
            return;
        }
        m_csvRecorder->start();
    });
    fileMenu->addAction(QStringLiteral("停止记录(&T)"), this, [this]() {
        if (!m_csvRecorder->isRecording()) {
            statusBar()->showMessage(QStringLiteral("当前未在记录"), 3000);
            return;
        }
        m_csvRecorder->stop();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("打开日志回放(&O)…"), m_logPanel, &LogPanel::slotOpenFile);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出(&X)"), this, &MainWindow::close);

    // ---- 用户菜单（S8）----
    QMenu *userMenu = menuBar()->addMenu(QStringLiteral("用户(&U)"));
    userMenu->addAction(QStringLiteral("修改我的密码(&P)…"), this, &MainWindow::slotChangeOwnPassword);
    m_actUserManage = userMenu->addAction(QStringLiteral("用户管理(&M)…"), this, &MainWindow::slotUserManager);
    userMenu->addSeparator();
    userMenu->addAction(QStringLiteral("注销(&L)"), this, &MainWindow::slotLogout);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(QStringLiteral("关于(&A)"), this, [this]() {
        QMessageBox::about(this, QStringLiteral("关于"),
                           QStringLiteral("串口数据采集与测控上位机 v0.1.0\n"
                                          "Qt %1 / C++17\n\n"
                                          "下位机: STM32F103C8T6 + DHT11 + 28BYJ-48\n"
                                          "数据库: SQLite（用户 / 审计 / 报警）")
                               .arg(QString::fromLatin1(qVersion())));
    });
}

void MainWindow::setupStatusBar()
{
    // 左侧：当前用户 + 连接状态 + 串口参数 + 实时温湿度
    // 右侧（常驻）：收发计数 + 运行时间
    m_lblUser = new QLabel(QStringLiteral("未登录"));
    m_lblUser->setStyleSheet(QStringLiteral("font-weight: bold; color: #2c3e50;"));
    m_lblConn = new QLabel(QStringLiteral("● 未连接"));
    m_lblParams = new QLabel;
    m_lblTemp = new QLabel(QStringLiteral("温度: --"));
    m_lblCounters = new QLabel(QStringLiteral("RX: 0 B   TX: 0 B"));
    m_lblRuntime = new QLabel(QStringLiteral("运行时间 00:00:00"));

    statusBar()->addWidget(m_lblUser);
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

    // ---------- 数据源仲裁（S6）：实时帧 与 回放帧 互斥进入显示链路 ----------
    connect(m_protocolEngine, &ProtocolEngine::sigFrameParsed, this,
            [this](const SensorData &d) {
        if (m_replayActive)
            return;                    // 回放期间实时帧不进显示/统计/记录/报警
        m_csvRecorder->slotRecord(d);  // 记录器内部自判是否处于记录状态
        m_alarmEngine->slotSensorData(d);   // 报警只针对实时数据
        dispatchToView(d);
    });
    // 回放帧只驱动显示与统计
    connect(m_logReplayer, &LogReplayer::sigFrame, this,
            [this](const SensorData &d) { dispatchToView(d); });
    // 回放状态 → 仲裁开关
    connect(m_logReplayer, &LogReplayer::sigPlayingChanged, this,
            [this](bool playing) { m_replayActive = playing; });

    // ---------- 协议统计：数据面板（链路健康度） + 统计引擎（丢包率） ----------
    connect(m_protocolEngine, &ProtocolEngine::sigStatsChanged,
            m_dataPanel, &DataPanel::slotUpdateStats);
    connect(m_protocolEngine, &ProtocolEngine::sigStatsChanged,
            m_statEngine, &StatEngine::slotProtoStats);

    // ---------- 清零统计：协议引擎 + 面板联动复位 ----------
    connect(m_dataPanel, &DataPanel::sigResetRequested, m_protocolEngine, &ProtocolEngine::reset);
    connect(m_dataPanel, &DataPanel::sigResetRequested, m_dataPanel, &DataPanel::slotReset);

    // ---------- S6 记录：面板/菜单 → 记录器 → 面板回显 ----------
    connect(m_logPanel, &LogPanel::sigStartRecording, this,
            [this]() { m_csvRecorder->start(); });
    connect(m_logPanel, &LogPanel::sigStopRecording, m_csvRecorder, &CsvRecorder::stop);
    connect(m_csvRecorder, &CsvRecorder::sigStarted, m_logPanel, &LogPanel::slotRecorderStarted);
    connect(m_csvRecorder, &CsvRecorder::sigStopped, m_logPanel, &LogPanel::slotRecorderStopped);
    connect(m_csvRecorder, &CsvRecorder::sigProgress, m_logPanel, &LogPanel::slotRecorderProgress);
    connect(m_csvRecorder, &CsvRecorder::sigRotated, m_logPanel, &LogPanel::slotRecorderRotated);
    connect(m_csvRecorder, &CsvRecorder::sigStarted, this, [this](const QString &path) {
        statusBar()->showMessage(QStringLiteral("开始记录: %1").arg(path), 5000);
    });
    connect(m_csvRecorder, &CsvRecorder::sigStopped, this,
            [this](const QString &path, qint64 rows) {
        statusBar()->showMessage(QStringLiteral("记录已停止: %1 共 %2 行").arg(path).arg(rows), 5000);
    });
    connect(m_csvRecorder, &CsvRecorder::sigError, this, [this](const QString &msg) {
        statusBar()->showMessage(QStringLiteral("记录错误: %1").arg(msg), 5000);
    });

    // ---------- S6 回放：面板 → 回放器 → 面板回显 ----------
    connect(m_logPanel, &LogPanel::sigLoadFile, m_logReplayer, &LogReplayer::load);
    connect(m_logPanel, &LogPanel::sigReplayPlay, m_logReplayer, &LogReplayer::slotPlay);
    connect(m_logPanel, &LogPanel::sigReplayPause, m_logReplayer, &LogReplayer::slotPause);
    connect(m_logPanel, &LogPanel::sigReplayStop, m_logReplayer, &LogReplayer::slotStop);
    connect(m_logPanel, &LogPanel::sigReplaySeek, m_logReplayer, &LogReplayer::slotSeek);
    connect(m_logPanel, &LogPanel::sigReplaySpeed, m_logReplayer, &LogReplayer::setSpeed);
    connect(m_logReplayer, &LogReplayer::sigLoaded, m_logPanel, &LogPanel::slotReplayLoaded);
    connect(m_logReplayer, &LogReplayer::sigProgress, m_logPanel, &LogPanel::slotReplayProgress);
    connect(m_logReplayer, &LogReplayer::sigPlayingChanged,
            m_logPanel, &LogPanel::slotReplayPlayingChanged);
    connect(m_logReplayer, &LogReplayer::sigFinished, m_logPanel, &LogPanel::slotReplayFinished);

    // ---------- S7 统计：引擎 → 面板，面板复位 → 引擎 ----------
    connect(m_serialManager, &SerialManager::sigBytesChanged,
            m_statEngine, &StatEngine::slotBytesChanged);
    connect(m_statEngine, &StatEngine::sigSnapshot,
            m_statPanel, &StatPanel::slotUpdateSnapshot);
    connect(m_statPanel, &StatPanel::sigResetRequested,
            m_statEngine, &StatEngine::slotReset);

    // ---------- S8 报警：先入库，再刷新面板（顺序不可颠倒） ----------
    connect(m_alarmEngine, &AlarmEngine::sigAlarm, m_alarmStore, &AlarmStore::insert);
    connect(m_alarmEngine, &AlarmEngine::sigAlarm, m_alarmPanel, &AlarmPanel::slotAlarm);
    connect(m_alarmEngine, &AlarmEngine::sigAlarm, this, [this](const AlarmEvent &e) {
        audit(QLatin1String(AuditAction::kAlarm), e.message);
        statusBar()->showMessage(QStringLiteral("【报警】%1").arg(e.message), 6000);
    });
    connect(m_alarmEngine, &AlarmEngine::sigActiveChanged,
            m_alarmPanel, &AlarmPanel::slotActiveChanged);
    // 阈值应用：引擎接受后才落盘配置，避免把非法值写进 ini
    connect(m_alarmPanel, &AlarmPanel::sigThresholdsChanged, this,
            [this](const AlarmThresholds &t) {
        if (!m_alarmEngine->setThresholds(t)) {
            statusBar()->showMessage(QStringLiteral("阈值非法，未生效"), 4000);
            return;
        }
        AppSettings::saveAlarmThresholds(t);
        m_alarmPanel->slotThresholdsChanged(m_alarmEngine->thresholds());
    });
    connect(m_alarmPanel, &AlarmPanel::sigAudit, this,
            [this](const QString &action, const QString &detail) { audit(action, detail); });

    // ---------- S8 会话与权限 ----------
    connect(m_session, &Session::sigUserChanged, m_auditService, &AuditService::slotCurrentUser);
    connect(m_session, &Session::sigPermissionsChanged, this, &MainWindow::slotApplyPermissions);
    connect(m_session, &Session::sigLoggedIn, this, [this](const UserInfo &u) {
        AppSettings::saveLastUserName(u.username);
        statusBar()->showMessage(QStringLiteral("欢迎，%1（%2）")
                                     .arg(u.displayName.isEmpty() ? u.username : u.displayName,
                                          UserText::role(u.role)), 5000);
    });

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
    connect(m_logPanel, &LogPanel::sigMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
    connect(m_logReplayer, &LogReplayer::sigMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });

    // ---------- 运行时间每秒刷新 ----------
    m_runtimeTimer = new QTimer(this);
    m_runtimeTimer->setInterval(1000);
    connect(m_runtimeTimer, &QTimer::timeout, this, &MainWindow::slotUpdateRuntime);

    // ---------- S4 控制链路 ----------
    // ACK 匹配：协议引擎 → 指令路由
    connect(m_protocolEngine, &ProtocolEngine::sigAckReceived,
            m_controlRouter, &ControlRouter::slotAckReceived);
    // 指令发送：路由 → 串口
    connect(m_controlRouter, &ControlRouter::sigSendFrame,
            m_serialManager, &SerialManager::slotSend);
    // 面板指令请求 → 路由排队（同时留审计痕迹）
    connect(m_controlPanel, &ControlPanel::sigCommandRequested,
            m_controlRouter, &ControlRouter::slotSendCommand);
    connect(m_controlPanel, &ControlPanel::sigCommandRequested, this,
            [this](quint8 cmd, quint8 param) {
        audit(QLatin1String(AuditAction::kMotorCommand),
              QStringLiteral("cmd=0x%1 param=%2")
                  .arg(cmd, 2, 16, QLatin1Char('0'))
                  .arg(param));
    });
    // 过程状态（已发送/已确认/重试/超时）→ 面板显示
    connect(m_controlRouter, &ControlRouter::sigStatusText,
            m_controlPanel, &ControlPanel::slotShowStatus);
    // 风扇 ACK → 同步按钮真实状态 + 同步温控器内部状态（S5）
    connect(m_controlRouter, &ControlRouter::sigAcked, this,
            [this](quint8 cmd, quint8 param) {
        if (cmd == Proto::CMD_FAN_CTRL) {
            m_controlPanel->slotFanStateConfirmed(param != 0);
            m_tempController->slotFanStateConfirmed(param != 0);
        }
    });
    // 指令最终超时 → 状态栏警示
    connect(m_controlRouter, &ControlRouter::sigTimeout, this,
            [this](quint8 cmd) {
        statusBar()->showMessage(QStringLiteral("指令 0x%1 超时：下位机无应答")
                                     .arg(cmd, 2, 16, QLatin1Char('0')).toUpper(), 5000);
    });
    // 仪表自动跟随：传感器帧 → 角度映射 → 发指令 / 更新波形和滑块
    // 注意：控制链路直连协议引擎，不经仲裁，回放期间闭环保护依然有效
    connect(m_protocolEngine, &ProtocolEngine::sigFrameParsed,
            m_dialController, &DialController::slotSensorData);
    connect(m_dialController, &DialController::sigSendAngleCommand, this,
            [this](int angle) {
        m_controlRouter->slotSendCommand(Proto::CMD_MOTOR_ANGLE, static_cast<quint8>(angle));
    });
    connect(m_dialController, &DialController::sigAngleChanged,
            m_wavePanel, &WavePanel::slotSetAngle);
    connect(m_dialController, &DialController::sigAngleChanged,
            m_controlPanel, &ControlPanel::slotUpdateAngle);
    // 面板开关 → 跟随控制器 / 波形设定值曲线
    connect(m_controlPanel, &ControlPanel::sigAutoFollowChanged,
            m_dialController, &DialController::setAutoFollow);
    connect(m_controlPanel, &ControlPanel::sigTargetChanged,
            m_wavePanel, &WavePanel::slotSetSetpoint);
    // 串口关闭 → 清空指令队列、停止重发
    connect(m_serialManager, &SerialManager::sigPortClosed,
            m_controlRouter, &ControlRouter::slotPortClosed);

    // ---------- S5 温度闭环 ----------
    // 模式/设定值 → 温控器
    connect(m_controlPanel, &ControlPanel::sigModeChanged,
            m_tempController, &TempController::setEnabled);
    connect(m_controlPanel, &ControlPanel::sigTargetChanged,
            m_tempController, &TempController::setTarget);
    // 传感器帧 → 温控器滞环决策
    connect(m_protocolEngine, &ProtocolEngine::sigFrameParsed,
            m_tempController, &TempController::slotSensorData);
    // 温控器风扇指令 → 指令路由（发0x12）
    connect(m_tempController, &TempController::sigFanCommand, this, [this](bool on) {
        m_controlRouter->slotSendCommand(Proto::CMD_FAN_CTRL, on ? 1 : 0);
    });
    // 温控状态 → 面板显示（含偏差值，带符号）
    connect(m_tempController, &TempController::sigStateChanged, this,
            [this](bool fanOn, double error) {
        m_controlPanel->slotShowAutoState(
            QStringLiteral("自动控温: 风扇%1 (偏差 %2%3°C)")
                .arg(fanOn ? QStringLiteral("开") : QStringLiteral("关"))
                .arg(error >= 0 ? QStringLiteral("+") : QString())
                .arg(error, 0, 'f', 1));
    });
    // 切回手动 → 状态标签复位
    connect(m_controlPanel, &ControlPanel::sigModeChanged, this, [this](bool autoOn) {
        if (!autoOn)
            m_controlPanel->slotShowAutoState(QStringLiteral("手动模式"));
    });

    // ---------- 审计埋点：关键操作全部留痕 ----------
    connect(m_serialManager, &SerialManager::sigPortOpened, this, [this](const SerialConfig &cfg) {
        audit(QLatin1String(AuditAction::kPortOpen),
              QStringLiteral("%1 %2").arg(cfg.portName, formatConfig(cfg)));
    });
    connect(m_serialManager, &SerialManager::sigPortClosed, this,
            [this]() { audit(QLatin1String(AuditAction::kPortClose)); });
    connect(m_controlPanel, &ControlPanel::sigModeChanged, this, [this](bool autoOn) {
        audit(QLatin1String(AuditAction::kModeChange),
              autoOn ? QStringLiteral("自动") : QStringLiteral("手动"));
    });
    connect(m_controlPanel, &ControlPanel::sigTargetChanged, this, [this](double t) {
        audit(QLatin1String(AuditAction::kTargetChange), QStringLiteral("%1°C").arg(t, 0, 'f', 1));
    });
    connect(m_tempController, &TempController::sigFanCommand, this, [this](bool on) {
        audit(QLatin1String(AuditAction::kFanCommand), on ? QStringLiteral("开") : QStringLiteral("关"));
    });
    connect(m_csvRecorder, &CsvRecorder::sigStarted, this, [this](const QString &path) {
        audit(QLatin1String(AuditAction::kRecordStart), path);
    });
    connect(m_csvRecorder, &CsvRecorder::sigStopped, this, [this](const QString &path, qint64 rows) {
        audit(QLatin1String(AuditAction::kRecordStop), QStringLiteral("%1 共 %2 行").arg(path).arg(rows));
    });
    connect(m_logReplayer, &LogReplayer::sigLoaded, this,
            [this](const QString &path, int frames, int badLines) {
        audit(QLatin1String(AuditAction::kReplayLoad),
              QStringLiteral("%1 帧数=%2 非法行=%3").arg(path).arg(frames).arg(badLines));
    });
    connect(m_statPanel, &StatPanel::sigResetRequested, this,
            [this]() { audit(QLatin1String(AuditAction::kStatReset)); });
}

void MainWindow::dispatchToView(const SensorData &data)
{
    // 显示链路的唯一出口：实时与回放两条通路在此汇合，保证下游面板
    // 无需关心数据来源（面向"数据帧"而非"数据来源"编程）
    m_dataPanel->slotUpdateData(data);
    m_wavePanel->slotAppendData(data);
    m_statEngine->slotSample(data);
    slotSensorData(data.temperature, data.humidity);
}

void MainWindow::audit(const QString &action, const QString &detail, bool success)
{
    if (m_auditService)
        m_auditService->log(action, detail, success);
}

void MainWindow::applyPermissions()
{
    const bool online = m_session && m_session->isLoggedIn();
    const UserRole role = online ? m_session->role() : UserRole::Guest;

    // 状态栏显示当前操作者，让"谁在操作"始终可见
    if (online) {
        const UserInfo u = m_session->user();
        const QString name = u.displayName.isEmpty() ? u.username : u.displayName;
        m_lblUser->setText(QStringLiteral("用户: %1 [%2]").arg(name, m_session->roleText()));
    } else {
        m_lblUser->setText(QStringLiteral("未登录"));
    }

    // 访客只读：串口、控制、记录回放全部禁用，只能看波形与统计
    m_serialPanel->setEnabled(online && Permission::canConnectSerial(role));
    m_controlPanel->setEnabled(online && Permission::canControl(role) && m_portOpen);
    m_logPanel->setEnabled(online && Permission::canRecord(role));
    m_alarmPanel->setPermissions(online && Permission::canConfigureAlarm(role),
                                 online && Permission::canPurgeData(role));
    if (m_actUserManage)
        m_actUserManage->setEnabled(online && Permission::canManageUsers(role));
}

void MainWindow::slotApplyPermissions()
{
    applyPermissions();
}

void MainWindow::slotUserManager()
{
    if (!Permission::canManageUsers(m_session->role())) {
        statusBar()->showMessage(QStringLiteral("只有管理员可以管理用户"), 3000);
        return;
    }
    UserManagerDialog dlg(m_userService, m_auditService, m_session, this);
    dlg.exec();
}

void MainWindow::slotChangeOwnPassword()
{
    if (!m_session->isLoggedIn())
        return;

    bool ok = false;
    const QString p1 = QInputDialog::getText(this, QStringLiteral("修改我的密码"),
                                             QStringLiteral("新密码（至少 %1 位）:")
                                                 .arg(UserService::kMinPasswordLength),
                                             QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;
    const QString p2 = QInputDialog::getText(this, QStringLiteral("修改我的密码"),
                                             QStringLiteral("确认新密码:"),
                                             QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;
    if (p1 != p2) {
        QMessageBox::warning(this, QStringLiteral("修改失败"), QStringLiteral("两次输入的密码不一致"));
        return;
    }

    QString err;
    if (!m_userService->changePassword(m_session->userId(), p1, &err)) {
        QMessageBox::warning(this, QStringLiteral("修改失败"), err);
        audit(QLatin1String(AuditAction::kUserPassword), err, false);
        return;
    }

    audit(QLatin1String(AuditAction::kUserPassword), QStringLiteral("用户=%1").arg(m_session->userName()));
    QMessageBox::information(this, QStringLiteral("修改成功"), QStringLiteral("密码已更新，下次登录请使用新密码"));
}

void MainWindow::slotLogout()
{
    audit(QLatin1String(AuditAction::kLogout));

    // 注销前先停掉记录、回放与串口，避免会话空窗期仍有数据写盘或指令下发
    m_logReplayer->slotStop();
    m_csvRecorder->stop();
    m_serialManager->slotClosePort();

    m_session->logout();   // 触发 sigPermissionsChanged → 全局置灰

    LoginDialog dlg(m_userService, m_auditService, m_session, this);
    if (dlg.exec() != QDialog::Accepted) {
        close();           // 取消登录即退出程序
        return;
    }
    applyPermissions();
}

void MainWindow::slotPortOpened(const SerialConfig &cfg)
{
    m_portOpen = true;
    m_serialPanel->setConnected(true);
    applyPermissions();   // 控制面板可用性 = 角色允许控制 且 串口已打开
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
    m_portOpen = false;
    m_serialPanel->setConnected(false);
    applyPermissions();
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
    // 退出前：停止回放 + 停止记录(强制刷盘) + 关闭串口 + 保存参数与窗口几何
    m_logReplayer->slotStop();
    m_csvRecorder->stop();
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
