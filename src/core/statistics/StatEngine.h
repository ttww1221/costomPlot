/**
 * @file    StatEngine.h
 * @brief   统计分析引擎 —— 业务逻辑层（S7 数据统计）
 *
 * 在不缓存全量数据的前提下实时给出统计特征，是"长时间运行不涨内存"的关键：
 *
 *  1. 数值统计采用 Welford 在线算法计算均值与标准差。
 *     朴素做法是先求平方和再开方，当均值远大于标准差时会出现
 *     "大数吃小数"的 catastrophic cancellation（平方和与均值平方几乎相等，
 *     相减后有效位大量丢失，甚至得到负方差）。Welford 每次只用增量更新，
 *     数值稳定性好且只需 O(1) 内存：
 *         delta  = x - mean
 *         mean  += delta / n
 *         M2    += delta * (x - mean)      // 用更新后的 mean
 *         var    = M2 / (n - 1)            // 样本方差（贝塞尔校正）
 *
 *  2. 速率统计采用 1s 滑动窗口差值法：RX/TX bytes/s、帧/s 均由
 *     "本秒末计数 - 上秒末计数"得出，无需保存历史队列。
 *
 *  3. 丢包率 = (CRC错误帧 + 失步次数) / 总帧数，直接复用协议引擎的 ProtoStats，
 *     反映链路质量而非应用层重传。
 *
 *  4. 分布直方图需要原始样本，故单独为温度/湿度各维护一个采样向量，
 *     上限 kMaxHistSamples 条（5Hz 下约 2.8 小时），超限后直方图停止累积
 *     但 Welford 统计继续，保证内存有界。
 */

#ifndef STATENGINE_H
#define STATENGINE_H

#include <QDateTime>
#include <QObject>
#include <QVector>

#include "ProtocolEngine.h"

class QTimer;

// 统计通道
enum class StatChannel { Temperature, Humidity };

/**
 * @brief 单通道数值统计结果
 */
struct ChannelStat
{
    qint64 count = 0;        // 样本数
    double last = 0.0;       // 最新值
    double min = 0.0;        // 最小值
    double max = 0.0;        // 最大值
    double mean = 0.0;       // 算术均值
    double stddev = 0.0;     // 样本标准差（n-1）
    QDateTime minTime;       // 最小值出现时刻
    QDateTime maxTime;       // 最大值出现时刻
};

/**
 * @brief 一次统计快照（1Hz 推送给界面）
 */
struct StatSnapshot
{
    ChannelStat temp;
    ChannelStat hum;

    double rxBps = 0.0;       // 接收速率 bytes/s
    double txBps = 0.0;       // 发送速率 bytes/s
    double frameRate = 0.0;   // 有效帧率 帧/s
    double lossRate = 0.0;    // 丢包率 0~1

    qint64 totalFrames = 0;   // 累计总帧数（含坏帧）
    qint64 validFrames = 0;   // 累计有效帧数
    qint64 crcErrors = 0;     // 累计 CRC 错误
    qint64 syncLosses = 0;    // 累计失步次数
    qint64 samples = 0;       // 累计参与统计的样本数
    qint64 durationSecs = 0;  // 统计运行时长（秒）
};

Q_DECLARE_METATYPE(StatSnapshot)

class StatEngine : public QObject
{
    Q_OBJECT

public:
    explicit StatEngine(QObject *parent = nullptr);

    static constexpr int kDefaultBins = 20;        // 直方图默认分箱数
    static constexpr int kMaxHistSamples = 50000;  // 直方图采样上限（内存有界）

    StatSnapshot snapshot() const;

    /**
     * @brief 计算指定通道的等宽分布直方图
     * @param ch      通道
     * @param bins    分箱数
     * @param centers 输出：各箱中心值（用于柱状图 x 坐标）
     * @param counts  输出：各箱样本数
     * @return 分箱宽度；无样本时返回 0
     */
    double histogram(StatChannel ch, int bins, QVector<double> &centers, QVector<int> &counts) const;

public slots:
    void slotSample(const SensorData &data);            // 喂入一帧（实时或回放）
    void slotProtoStats(const ProtoStats &stats);       // 同步协议引擎统计
    void slotBytesChanged(qint64 rxBytes, qint64 txBytes); // 同步串口收发计数
    void slotReset();                                   // 一键清零

signals:
    void sigSnapshot(const StatSnapshot &s);   // 1Hz 推送统计快照

private slots:
    void slotTick();   // 每秒结算速率并推送快照

private:
    // Welford 累加器
    struct Accum
    {
        qint64 count = 0;
        double mean = 0.0;
        double m2 = 0.0;       // 平方差累加和
        double min = 0.0;
        double max = 0.0;
        double last = 0.0;
        QDateTime minTime;
        QDateTime maxTime;
        QVector<double> values;   // 直方图用原始样本（有上限）
    };

    static void push(Accum &a, double x, const QDateTime &t);
    static ChannelStat toStat(const Accum &a);
    const Accum &accum(StatChannel ch) const;

    Accum m_temp;
    Accum m_hum;
    ProtoStats m_proto;      // 最近一次协议引擎统计

    qint64 m_rxTotal = 0;    // 串口累计接收字节（由 SerialManager 提供）
    qint64 m_txTotal = 0;    // 串口累计发送字节
    qint64 m_rxLast = 0;     // 上一秒末的接收字节（差值法基准）
    qint64 m_txLast = 0;
    int m_framesInWindow = 0;   // 本秒内到达的帧数

    double m_rxBps = 0.0;
    double m_txBps = 0.0;
    double m_frameRate = 0.0;
    qint64 m_durationSecs = 0;

    QTimer *m_timer;   // 1s 结算定时器
};

#endif // STATENGINE_H
