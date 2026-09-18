/**
 * @file    LogReplayer.h
 * @brief   历史数据回放器 —— 业务逻辑层（S6 数据回放）
 *
 * 把 CsvLoader 装载的历史帧按"原始采样节奏"重新注入显示链路，
 * 效果等同于下位机正在实时上报，可用于离线复现现场、答辩演示与算法验证。
 *
 * 关键设计：
 *  1. 定时间隔取自相邻两帧的真实时间差（而非固定 200ms），
 *     忠实还原当时的采样节奏；差值被夹在 [5ms, 2000ms] 内防止异常值卡死；
 *  2. 倍速 1x/2x/5x/10x 通过把间隔除以倍速实现，不改变数据本身；
 *  3. 发出的帧会把 timestamp 重写为"当前时刻"—— 波形面板的 x 轴是实时滚动窗口
 *     （只保留 now-window 内的点），若沿用历史时间戳，回放数据会被立刻裁掉；
 *     原始录制时间通过 sigProgress 单独上报给面板显示；
 *  4. 回放帧只流向显示与统计，不流向控制链路（见 MainWindow 的数据源仲裁），
 *     避免历史数据误触发风扇/电机指令。
 */

#ifndef LOGREPLAYER_H
#define LOGREPLAYER_H

#include <QObject>
#include <QVector>

#include "ProtocolEngine.h"

class QTimer;

class LogReplayer : public QObject
{
    Q_OBJECT

public:
    explicit LogReplayer(QObject *parent = nullptr);

    bool isPlaying() const { return m_playing; }
    int frameCount() const { return m_frames.size(); }
    int currentIndex() const { return m_index; }
    double speed() const { return m_speed; }
    const QVector<SensorData> &frames() const { return m_frames; }

    // 取某帧的原始录制时间（越界返回无效 QDateTime）
    QDateTime originalTimeAt(int index) const;

public slots:
    // 从 CSV 文件装载数据（成功返回 true，失败原因经 sigMessage 上报）
    bool load(const QString &filePath);
    // 直接注入帧序列（供离线分析/单元测试使用）
    void setFrames(const QVector<SensorData> &frames);
    void clear();

    void setSpeed(double multiplier);   // 回放倍速
    void slotPlay();
    void slotPause();
    void slotStop();
    void slotSeek(int index);           // 跳转到指定帧（进度条拖动）

signals:
    void sigFrame(const SensorData &data);   // 重放一帧（timestamp 已重写为当前时刻）
    void sigProgress(int index, int total, const QDateTime &originalTime);
    void sigPlayingChanged(bool playing);
    void sigLoaded(const QString &path, int frames, int badLines);
    void sigFinished();                      // 播放到文件末尾
    void sigMessage(const QString &msg);

private slots:
    void slotTick();

private:
    void emitCurrent();      // 发出当前帧（重写时间戳）
    void scheduleNext();     // 按真实帧间隔 / 倍速 设定下次触发
    void setPlaying(bool on);

    QVector<SensorData> m_frames;
    QTimer *m_timer;
    int m_index = 0;         // 下一帧待播放的下标
    double m_speed = 1.0;
    bool m_playing = false;
};

#endif // LOGREPLAYER_H
