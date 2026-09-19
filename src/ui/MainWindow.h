/**
 * @file    MainWindow.h
 * @brief   主窗口 —— 展示层
 *
 * 布局（三栏 Splitter，参照设计文档 6.1 节）：
 *   ┌──────────┬──────────────────────┬──────────────────────────┐
 *   │ 串口面板  │                      │ [实时数据|统计分析|报警]   │
 *   │ 控制面板  │      波形显示区        ├──────────────────────────┤
 *   │ 记录回放  │                      │      调试收发面板         │
 *   ├──────────┴──────────────────────┴──────────────────────────┤
 *   │ 状态栏：用户 | 连接状态 | 串口参数 | 温度 | RX/TX | 运行时间   │
 *   └────────────────────────────────────────────────────────────┘
 *
 * 职责：创建各面板与核心服务，用信号槽把它们"接线"（信号总线模式），
 * 自身不实现业务逻辑。三项例外职责：
 *
 *  1. 数据源仲裁（dispatchToView，S6）——显示与统计链路的数据可能来自
 *     "实时串口"或"历史回放"，二者必须互斥，否则历史帧与实时帧会在波形上
 *     交错成锯齿。回放期间丢弃实时帧的显示请求；而控制链路与报警链路
 *     始终只吃实时帧，保证回放期间闭环保护与越限监测不失效。
 *
 *  2. 权限门控（applyPermissions，S8）——登录角色变化的唯一响应点。
 *     面板自身不含 if (role == Admin) 判断，统一由主窗口按 Session 角色
 *     设置可用性，权限规则集中在 Permission 命名空间。
 *
 *  3. 审计接线——把串口开关、控制指令、记录回放、报警、统计复位等
 *     关键操作统一写入 audit_log 表，界面层不直接接触数据库写入。
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>

#include "ProtocolEngine.h"
#include "SerialManager.h"

class SerialPanel;
class WavePanel;
class DataPanel;
class DiagPanel;
class ControlPanel;
class StatPanel;
class LogPanel;
class AlarmPanel;
class SerialManager;
class ProtocolEngine;
class ControlRouter;
class DialController;
class TempController;
class CsvRecorder;
class LogReplayer;
class StatEngine;
class Database;
class Session;
class UserService;
class AuditService;
class AlarmStore;
class AlarmEngine;
class QAction;
class QLabel;
class QTimer;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // session 与 db 由 main() 创建后注入（登录成功后才会构造主窗口）
    explicit MainWindow(Session *session, Database *db, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // 关闭窗口时：停止记录与回放 + 关闭串口 + 保存参数与窗口几何
    void closeEvent(QCloseEvent *event) override;

private slots:
    void slotPortOpened(const SerialConfig &cfg);  // 串口打开成功：刷新面板与状态栏，启动计时
    void slotPortClosed();                         // 串口关闭：复位各显示
    void slotSerialError(const QString &msg);      // 串口错误：状态栏提示
    void slotBytesChanged(qint64 rxBytes, qint64 txBytes); // 收发计数变化
    void slotSensorData(double temperature, double humidity); // 收到解析后的温湿度
    void slotUpdateRuntime();                      // 每秒刷新运行时间显示
    void slotApplyPermissions();                   // 角色/登录状态变化 → 刷新权限
    void slotUserManager();                        // 打开用户管理（仅管理员）
    void slotChangeOwnPassword();                  // 修改当前用户口令
    void slotLogout();                             // 注销并回到登录框

private:
    void setupUi();          // 构建三栏布局
    void setupMenuBar();     // 菜单栏（文件/用户/帮助）
    void setupStatusBar();   // 状态栏各标签
    void setupSignals();     // 核心信号槽接线（信号总线）
    void applyPermissions(); // 按当前角色设置各面板与菜单项可用性

    // 数据源仲裁出口：一帧数据同时送往 数据面板 / 波形 / 统计引擎 / 状态栏
    void dispatchToView(const SensorData &data);
    // 审计写入的简写（自动补当前用户，失败不打断业务）
    void audit(const QString &action, const QString &detail = QString(), bool success = true);

    // 把串口配置格式化为 "115200bps 8N1" 形式的状态栏文本
    static QString formatConfig(const SerialConfig &cfg);

    // ---- 基础设施与核心服务 ----
    Database *m_db;                   // SQLite 连接（S8）
    Session *m_session;               // 登录会话（S8）
    UserService *m_userService;       // 账号服务（S8）
    AuditService *m_auditService;     // 操作审计（S8）
    AlarmStore *m_alarmStore;         // 报警事件持久化（S8）
    AlarmEngine *m_alarmEngine;       // 阈值报警引擎（S8）
    SerialManager *m_serialManager;   // 串口管理器
    ProtocolEngine *m_protocolEngine; // 协议解析引擎
    ControlRouter *m_controlRouter;   // 指令路由：ACK匹配+超时重发
    DialController *m_dialController; // 仪表盘温度跟随
    TempController *m_tempController; // 温度闭环：滞环单边控制（S5）
    CsvRecorder *m_csvRecorder;       // CSV 数据记录器（S6）
    LogReplayer *m_logReplayer;       // 历史数据回放器（S6）
    StatEngine *m_statEngine;         // 统计分析引擎（S7）

    // ---- 界面 ----
    SerialPanel *m_serialPanel;       // 串口面板（左上）
    ControlPanel *m_controlPanel;     // 控制面板（左中）
    LogPanel *m_logPanel;             // 记录与回放面板（左下，S6）
    WavePanel *m_wavePanel;           // 波形面板（中）
    DataPanel *m_dataPanel;           // 数据面板（右上选项卡）
    StatPanel *m_statPanel;           // 统计面板（右上选项卡，S7）
    AlarmPanel *m_alarmPanel;         // 报警面板（右上选项卡，S8）
    DiagPanel *m_diagPanel;           // 调试收发面板（右下）

    QAction *m_actUserManage;   // 用户管理菜单项（仅管理员可用）

    bool m_replayActive = false;   // 回放进行中：屏蔽实时帧进入显示链路
    bool m_portOpen = false;       // 串口是否已打开（与角色共同决定控制面板可用性）

    QLabel *m_lblUser;      // 状态栏：当前用户与角色
    QLabel *m_lblConn;      // 状态栏：连接状态
    QLabel *m_lblParams;    // 状态栏：串口参数
    QLabel *m_lblTemp;      // 状态栏：实时温湿度
    QLabel *m_lblCounters;  // 状态栏：RX/TX 计数
    QLabel *m_lblRuntime;   // 状态栏：运行时间

    QTimer *m_runtimeTimer;  // 运行时间刷新定时器（1s）
    QElapsedTimer m_elapsed; // 运行计时器
};

#endif // MAINWINDOW_H
