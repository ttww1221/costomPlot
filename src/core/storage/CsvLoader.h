/**
 * @file    CsvLoader.h
 * @brief   CSV 历史数据装载器 —— 基础设施层（S6 数据回放）
 *
 * 把 CsvRecorder 写出的 CSV 文件还原为 QVector<SensorData>，
 * 供 LogReplayer 重放、StatEngine 离线统计使用。
 *
 * 容错设计（历史文件可能被手工编辑或写入中断）：
 *  - 自动跳过表头行与空行；
 *  - 字段数兼容 4 列（含帧序号）与 3 列（无帧序号）两种写法；
 *  - 时间戳兼容 "yyyy-MM-dd hh:mm:ss.zzz" / ISO8601 / "yyyy-MM-dd hh:mm:ss"；
 *  - 单行解析失败只累加 badLines 计数并跳过，不中断整个文件装载。
 */

#ifndef CSVLOADER_H
#define CSVLOADER_H

#include <QString>
#include <QVector>

#include "ProtocolEngine.h"

namespace CsvLoader {

/**
 * @brief 装载结果
 */
struct Result
{
    bool ok = false;              // 文件是否成功打开并解析
    QString error;                // 失败原因（成功时为空）
    QVector<SensorData> frames;   // 解析出的数据帧（按文件顺序）
    int badLines = 0;             // 被跳过的非法行数
};

/**
 * @brief 装载一个 CSV 日志文件
 * @param filePath  文件全路径
 * @param maxFrames 最多装载帧数，-1 表示全部（超大文件可限量装载）
 */
Result load(const QString &filePath, int maxFrames = -1);

/**
 * @brief 解析一行 CSV 记录
 * @return 解析成功返回 true，字段缺失或数值非法返回 false
 */
bool parseLine(const QString &line, SensorData *out);

}

#endif // CSVLOADER_H
