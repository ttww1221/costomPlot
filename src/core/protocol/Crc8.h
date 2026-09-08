/**
 * @file    Crc8.h
 * @brief   CRC-8-ATM 校验算法（核心服务层）
 *
 * 参数：多项式 0x07 (x^8 + x^2 + x + 1)，初始值 0x00，无反射，无输出异或。
 * 与下位机 STM32 固件中的 CRC8 实现保持一致（校验范围：温度+湿度共 8 字节）。
 * 该参数对应 RevEng CRC 目录中的 "CRC-8"（标准检查值 "123456789" -> 0xF4）。
 *
 * 注意：设计文档 3.2 节示例帧中的 CRC 字节 0xA3 与其声明的算法参数不符
 * （独立验证正确值为 0x36），已按算法参数实现；最终以固件实测为准。
 */

#ifndef CRC8_H
#define CRC8_H

#include <QByteArray>

#include <cstddef>
#include <cstdint>

namespace Crc8 {

/**
 * @brief 计算 CRC-8-ATM 校验值
 * @param data 数据指针
 * @param len  数据长度（字节）
 * @return 8 位 CRC 校验值
 */
uint8_t compute(const uint8_t *data, size_t len);

/**
 * @brief 计算 QByteArray 指定区间的 CRC-8-ATM 校验值
 * @param data   数据
 * @param offset 起始偏移
 * @param len    长度（字节）
 * @return CRC 值；区间非法时返回 0
 */
uint8_t compute(const QByteArray &data, int offset, int len);

}

#endif // CRC8_H
