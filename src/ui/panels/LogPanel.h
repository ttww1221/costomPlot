/**
 * @file    LogPanel.h
 * @brief   数据记录与回放面板 —— 展示层（S6）
 *
 * 两块功能区：
 *  1. 数据记录：一键开始/停止把实时帧写入 CSV，显示当前文件、行数与体积；
 *  2. 历史回放：打开任意历史 CSV，按倍速重放到波形/数据/统计面板，
 *     支持进度条拖动定位、播放中暂停与停止。
 *
 * 面板只做交互，不直接操作文件：所有请求以信号发出，
 * 由 MainWindow 接线到 CsvRecorder / LogReplayer（与 ControlPanel→ControlRouter 一致）。
 */

#ifndef LOGPANEL_H
#define LOGPANEL_H

#include <QDateTime>
#include <QGroupBox>

class QLabel;
class QPushButton;
class QSlider;
class QComboBox;

class LogPanel : public QGroupBox
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);

public slots:
    // ---- 记录器状态回显 ----
    void slotRecorderStarted(const QString &path);
    void slotRecorderStopped(const QString &path, qint64 rows);
    void slotRecorderProgress(const QString &path, qint64 rows, qint64 bytes);
    void slotRecorderRotated(const QString &newPath);

    // ---- 回放器状态回显 ----
    void slotReplayLoaded(const QString &path, int frames, int badLines);
    void slotReplayProgress(int index, int total, const QDateTime &originalTime);
    void slotReplayPlayingChanged(bool playing);
    void slotReplayFinished();

    // 供菜单调用的"打开日志文件"入口
    void slotOpenFile();

signals:
    void sigStartRecording();
    void sigStopRecording();

    void sigLoadFile(const QString &path);
    void sigReplayPlay();
    void sigReplayPause();
    void sigReplayStop();
    void sigReplaySeek(int index);
    void sigReplaySpeed(double multiplier);

    void sigMessage(const QString &msg);

private slots:
    void slotToggleRecord();     // 记录按钮切换
    void slotTogglePlay();       // 播放/暂停按钮切换
    void slotSpeedChanged(int index);
    void slotSliderMoved(int value);

private:
    // ---- 记录区 ----
    QPushButton *m_btnRecord;   // checkable：开始/停止记录
    QLabel *m_lblFile;          // 当前记录文件名
    QLabel *m_lblRecInfo;       // 已记录行数 / 体积

    // ---- 回放区 ----
    QPushButton *m_btnOpen;     // 打开日志文件
    QLabel *m_lblReplayInfo;    // 已载入帧数
    QPushButton *m_btnPlay;     // checkable：播放/暂停
    QPushButton *m_btnStop;     // 停止并回到起点
    QComboBox *m_cmbSpeed;      // 倍速 1x/2x/5x/10x
    QSlider *m_sliderProgress;  // 回放进度
    QLabel *m_lblPosition;      // 当前位置 / 原始录制时间

    int m_totalFrames = 0;      // 已载入帧数（用于进度条范围）
};

#endif // LOGPANEL_H
