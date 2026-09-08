/**
 * @file    ConvertUtils.cpp
 * @brief   数据格式转换工具实现
 */

#include "ConvertUtils.h"

#include <QRegularExpression>
#include <QStringList>

namespace ConvertUtils {

QString bytesToHex(const QByteArray &data, int bytesPerLine)
{
    QString result;
    result.reserve(data.size() * 3);  // 预分配容量，减少字符串反复扩容
    for (int i = 0; i < data.size(); ++i) {
        // 字节间用空格分隔，每 16 字节换一行，便于观察帧边界
        if (i > 0)
            result += (bytesPerLine > 0 && i % bytesPerLine == 0) ? QLatin1Char('\n') : QLatin1Char(' ');
        // 补零为两位大写十六进制
        result += QString::number(static_cast<quint8>(data.at(i)), 16)
                      .rightJustified(2, QLatin1Char('0'))
                      .toUpper();
    }
    return result;
}

QByteArray hexToBytes(const QString &text, bool *ok)
{
    QByteArray result;
    bool success = true;

    // 按空白 / 逗号 / 分号切分成若干 token（"AA BB"、"AA,BB"、"0xAA 0xBB" 均可）
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        QString t = token;
        // 去掉 0x / 0X 前缀
        if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            t = t.mid(2);
        // 单个 token 长度必须为偶数（如 "AABB" 拆成 AA BB；"AAB" 非法）
        if (t.isEmpty() || t.size() % 2 != 0) {
            success = false;
            break;
        }
        // 每两个十六进制字符合成一个字节
        for (int i = 0; i < t.size(); i += 2) {
            bool pairOk = false;
            const uint value = t.mid(i, 2).toUInt(&pairOk, 16);
            if (!pairOk) {
                success = false;
                break;
            }
            result.append(static_cast<char>(value));
        }
        if (!success)
            break;
    }

    // 空输入视为失败，避免"无内容也发一帧"
    if (tokens.isEmpty())
        success = false;

    if (ok)
        *ok = success;
    return success ? result : QByteArray();
}

QString bytesToPrintable(const QByteArray &data)
{
    QString result;
    result.reserve(data.size());
    for (char c : data) {
        const uchar u = static_cast<uchar>(c);
        result += (u >= 0x20 && u < 0x7F) ? QLatin1Char(static_cast<char>(u)) : QLatin1Char('.');
    }
    return result;
}

QString formatByteSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

}
