/**
 * @file    Crc8.cpp
 * @brief   CRC-8-ATM 校验算法实现（查表法）
 *
 * 查表法原理：把每个字节的 8 次移位异或运算预先算好存入 256 项表，
 * 计算时每个字节只需一次查表 + 一次异或，速度约为逐位计算的 8 倍。
 * 5Hz × 8 字节的负载下性能绰绰有余，但查表实现更规范、易讲解。
 */

#include "Crc8.h"

namespace {

// 256 项查找表，程序启动时由 TableInitializer 一次性生成
uint8_t s_table[256];

/**
 * @brief 静态初始化器：构造时生成 CRC 查找表
 */
struct TableInitializer
{
    TableInitializer()
    {
        for (int i = 0; i < 256; ++i) {
            uint8_t c = static_cast<uint8_t>(i);
            // 逐位处理：最高位为 1 则左移后异或多项式 0x07
            for (int k = 0; k < 8; ++k)
                c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ 0x07)
                               : static_cast<uint8_t>(c << 1);
            s_table[i] = c;
        }
    }
} s_tableInitializer;

}

uint8_t Crc8::compute(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;  // 初始值 0x00
    for (size_t i = 0; i < len; ++i)
        crc = s_table[crc ^ data[i]];
    return crc;
}

uint8_t Crc8::compute(const QByteArray &data, int offset, int len)
{
    // 区间合法性检查
    if (offset < 0 || len < 0 || offset + len > data.size())
        return 0;
    return compute(reinterpret_cast<const uint8_t *>(data.constData() + offset),
                   static_cast<size_t>(len));
}
