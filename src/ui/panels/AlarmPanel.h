/**
 * @file    AlarmPanel.h
 * @brief   报警面板 —— 展示层（S8）
 *
 * 三个区域：
 *  1. 当前状态：LED 指示灯 + 活动报警摘要（红/绿一目了然）
 *  2. 阈值配置：总开关、温度/湿度上下限、去抖帧数，"应用"后下发给 AlarmEngine
 *     并由主窗口写入配置文件与审计日志
 *  3. 历史报警：从 SQLite 读取的事件表格，按等级着色，支持确认与清空
 *
 * 权限：访客只能查看（阈值区与确认/清空按钮全部禁用），
 * 操作员可配置阈值与确认报警，只有管理员能清空历史（canPurge）。
 */

#ifndef ALARMPANEL_H
#define ALARMPANEL_H

#include <QGroupBox>

#include "AlarmDefs.h"
#include "AlarmEngine.h"

class AlarmStore;
class LedIndicator;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

class AlarmPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit AlarmPanel(AlarmStore *store, QWidget *parent = nullptr);

    // 依据当前登录角色调整可操作性
    void setPermissions(bool canConfigure, bool canPurge);

public slots:
    void reloadHistory();                                  // 从数据库重新拉取表格
    void slotAlarm(const AlarmEvent &ev);                  // 新事件（已入库）→ 刷新表格
    void slotActiveChanged(bool active, const QString &summary); // 活动报警状态变化
    void slotThresholdsChanged(const AlarmThresholds &t);  // 引擎阈值变化 → 回填控件

signals:
    void sigThresholdsChanged(const AlarmThresholds &t);   // 请求应用新阈值
    void sigAudit(const QString &action, const QString &detail); // 请求写审计日志

private slots:
    void slotApply();
    void slotAckSelected();
    void slotAckAll();
    void slotClear();

private:
    enum Column { ColId = 0, ColTime, ColChannel, ColLevel, ColKind, ColValue, ColThreshold, ColMessage, ColAcked, ColCount };

    void setupUi();
    void appendRow(const AlarmEvent &ev);
    void setRowWidgets(int row, const AlarmEvent &ev);
    bool collectThresholds(AlarmThresholds *out) const;

    AlarmStore *m_store;

    LedIndicator *m_led;
    QLabel *m_lblState;

    QCheckBox *m_chkEnabled;
    QCheckBox *m_chkTemp;
    QCheckBox *m_chkHum;
    QDoubleSpinBox *m_spinTempLow;
    QDoubleSpinBox *m_spinTempHigh;
    QDoubleSpinBox *m_spinHumLow;
    QDoubleSpinBox *m_spinHumHigh;
    QSpinBox *m_spinDebounce;
    QPushButton *m_btnApply;

    QTableWidget *m_table;
    QPushButton *m_btnAck;
    QPushButton *m_btnAckAll;
    QPushButton *m_btnClear;
    QLabel *m_lblCount;

    bool m_canConfigure = true;
    bool m_canPurge = false;
};

#endif // ALARMPANEL_H
