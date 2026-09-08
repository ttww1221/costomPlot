/**
 * @file    ConvertUtils.h
 * @brief   数据格式转换工具集
 *
 * 提供上位机常用的格式转换函数：
 *  - 字节流 <-> HEX 字符串（调试面板显示 / 用户输入解析）
 *  - 二进制数据 -> 可打印 ASCII（不可见字符用 '.' 占位）
 *  - 字节数 -> 人类可读大小（B / KB / MB）
 */

#ifndef CONVERTUTILS_H
#define CONVERTUTILS_H

#include <QByteArray>
#include <QString>

namespace ConvertUtils {

/**
 * @brief 字节数组转 HEX 字符串
 * @param data         原始字节
 * @param bytesPerLine 每行显示字节数（0 表示不换行），默认 16
 * @return 形如 "AA BB CC ..." 的大写 HEX 字符串
 */
QString bytesToHex(const QByteArray &data, int bytesPerLine = 16);

/**
 * @brief HEX 文本转字节数组
 * @param text 用户输入，支持 "AA BB" / "AABB" / "0xAA 0xBB" / 逗号分号分隔等形式
 * @param ok   输出参数：解析是否成功
 * @return 解析成功的字节数组；失败时返回空数组且 *ok == false
 */
QByteArray hexToBytes(const QString &text, bool *ok = nullptr);

/**
 * @brief 字节数组转可打印字符串
 * @details 可见字符(0x20~0x7E)原样输出，其余用 '.' 占位，用于 ASCII 模式显示
 */
QString bytesToPrintable(const QByteArray &data);

/**
 * @brief 字节数格式化为人类可读字符串（B / KB / MB）
 */
QString formatByteSize(qint64 bytes);

}

#endif // CONVERTUTILS_H
