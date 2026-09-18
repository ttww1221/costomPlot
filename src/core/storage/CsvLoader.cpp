/**
 * @file    CsvLoader.cpp
 * @brief   CSV 历史数据装载器实现
 */

#include "CsvLoader.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace {
// 支持的时间戳格式，按优先级依次尝试
const char *const kTimeFormats[] = {
    "yyyy-MM-dd hh:mm:ss.zzz",
    "yyyy-MM-dd hh:mm:ss",
};
}

namespace CsvLoader {

bool parseLine(const QString &line, SensorData *out)
{
    if (!out)
        return false;

    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return false;

    // 表头行直接判定为非法（调用方计入 badLines 或跳过）
    if (trimmed.startsWith(QLatin1String("timestamp")))
        return false;

    const QStringList f = trimmed.split(QLatin1Char(','));
    QString tsText, tempText, humText, idxText;

    if (f.size() >= 4) {          // timestamp,frame_index,temperature,humidity
        tsText = f.at(0);
        idxText = f.at(1);
        tempText = f.at(2);
        humText = f.at(3);
    } else if (f.size() == 3) {   // timestamp,temperature,humidity
        tsText = f.at(0);
        tempText = f.at(1);
        humText = f.at(2);
    } else {
        return false;
    }

    bool okT = false, okH = false;
    const double temp = tempText.toDouble(&okT);
    const double hum = humText.toDouble(&okH);
    if (!okT || !okH)
        return false;

    QDateTime ts;
    for (const char *fmt : kTimeFormats) {
        ts = QDateTime::fromString(tsText, QString::fromLatin1(fmt));
        if (ts.isValid())
            break;
    }
    if (!ts.isValid())
        ts = QDateTime::fromString(tsText, Qt::ISODateWithMs);
    if (!ts.isValid())
        return false;

    out->temperature = temp;
    out->humidity = hum;
    out->timestamp = ts;
    out->frameIndex = idxText.isEmpty() ? 0 : idxText.toLongLong();
    return true;
}

Result load(const QString &filePath, int maxFrames)
{
    Result res;

    QFile file(filePath);
    if (!file.exists()) {
        res.error = QStringLiteral("文件不存在: %1").arg(filePath);
        return res;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        res.error = QStringLiteral("无法打开文件: %1 (%2)").arg(filePath, file.errorString());
        return res;
    }

    QTextStream in(&file);
    SensorData d;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty() || line.startsWith(QLatin1String("timestamp")))
            continue;   // 空行与表头不计入错误
        if (parseLine(line, &d)) {
            res.frames.append(d);
            if (maxFrames > 0 && res.frames.size() >= maxFrames)
                break;
        } else {
            ++res.badLines;
        }
    }
    file.close();

    res.ok = true;
    return res;
}

}
