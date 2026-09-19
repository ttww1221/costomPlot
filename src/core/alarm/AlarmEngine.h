/**
 * @file    AlarmEngine.h
 * @brief   阈值报警引擎 —— 业务逻辑层（S8）
 *
 * 纯逻辑模块：不碰数据库、不碰界面，只吃 SensorData、吐 AlarmEvent，
 * 因此可以完全离线单元测试。事件由 MainWindow 接线到 AlarmStore（入库）
 * 与 AlarmPanel（显示）。
 *
 * 两个工程要点：
 *  1. 去抖（debounce）—— DHT11 单次读数存在 ±2°C 级抖动，串口链路也可能
 *     出现个别坏帧。若"一帧越限就报警"，现场会产生大量误报。这里要求
 *     连续 N 帧（默认 3 帧，即 0.6s）都越限才触发，恢复同样需要连续 N 帧
 *     回到区间内，形成一个带滞环的双稳态判别，等价于软件施密特触发器。
 *  2. 分级 —— 按越限幅度分级（温度偏差 >5°C 为严重、>2°C 为警告、其余提示），
 *     界面可据等级着色，论文里可给出分级判据表。
 */

#ifndef ALARMENGINE_H
#define ALARMENGINE_H

#include <QObject>
#include <QVector>

#include "AlarmDefs.h"
#include "ProtocolEngine.h"

/**
 * @brief 报警阈值配置
 */
struct AlarmThresholds
{
    bool enabled = true;          // 报警总开关

    bool tempEnabled = true;      // 温度通道使能
    double tempLow = 5.0;         // 温度下限 °C
    double tempHigh = 35.0;       // 温度上限 °C

    bool humEnabled = false;      // 湿度通道使能（DHT11 湿度波动大，默认关闭）
    double humLow = 20.0;         // 湿度下限 %
    double humHigh = 90.0;        // 湿度上限 %

    int debounceFrames = 3;       // 连续多少帧越限才触发/恢复

    bool isValid() const { return tempLow < tempHigh && humLow < humHigh && debounceFrames >= 1; }
};

Q_DECLARE_METATYPE(AlarmThresholds)

class AlarmEngine : public QObject
{
    Q_OBJECT

public:
    explicit AlarmEngine(QObject *parent = nullptr);

    AlarmThresholds thresholds() const { return m_th; }
    bool hasActiveAlarm() const { return !m_active.isEmpty(); }
    QVector<AlarmEvent> activeAlarms() const { return m_active; }
    QString activeSummary() const;   // 活动报警的文字摘要（状态栏/面板显示）

public slots:
    // 设置阈值；非法配置（下限≥上限、去抖<1）被拒绝并保持原值
    bool setThresholds(const AlarmThresholds &t);
    void slotSensorData(const SensorData &data);
    void slotClearActive();   // 清除当前活动报警（历史事件不受影响）

signals:
    void sigAlarm(const AlarmEvent &ev);                    // 新事件（触发或恢复）
    void sigActiveChanged(bool active, const QString &summary);
    void sigThresholdsChanged(const AlarmThresholds &t);

private:
    // 单通道去抖状态机
    struct ChannelState
    {
        bool highActive = false;   // 当前处于"超上限"报警中
        bool lowActive = false;    // 当前处于"低于下限"报警中
        int highCount = 0;         // 连续越上限帧数
        int lowCount = 0;          // 连续越下限帧数
        int recoverCount = 0;      // 连续回到正常区间帧数
    };

    void evaluate(AlarmChannel ch, double value, const QDateTime &ts);
    void trigger(AlarmChannel ch, AlarmKind kind, double value, double threshold, const QDateTime &ts);
    void recover(AlarmChannel ch, double value, const QDateTime &ts);
    // 按越限幅度定级
    static AlarmLevel levelFor(AlarmChannel ch, double value, double threshold);
    void refreshActiveSignal();

    AlarmThresholds m_th;
    ChannelState m_temp;
    ChannelState m_hum;
    QVector<AlarmEvent> m_active;   // 当前未恢复的报警
    bool m_lastActiveState = false;
    QString m_lastSummary;
};

#endif // ALARMENGINE_H
