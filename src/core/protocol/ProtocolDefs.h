/**
 * @file    ProtocolDefs.h
 * @brief   协议常量统一定义（与固件 User/Protocol.h 一一对应）
 *
 * 注意：这里的常量与 STM32 固件端必须严格一致，
 * 任何一边改动都要同步另一边，否则通信会失败。
 */
#ifndef PROTOCOLDEFS_H
#define PROTOCOLDEFS_H

#include <QtGlobal>

namespace Proto {

// ===== 下行指令码（上位机 → 下位机，2字节帧 = 指令码 + 参数）=====
constexpr quint8 CMD_MOTOR_ANGLE = 0x10;  // 仪表盘角度：参数 = 0~180
constexpr quint8 CMD_MOTOR_HOME  = 0x11;  // 仪表盘回零：参数 = 0x00
constexpr quint8 CMD_FAN_CTRL    = 0x12;  // 风扇开关：0=关 1=开
constexpr quint8 CMD_AUTO_MODE   = 0x20;  // 自动模式标志：0=手动 1=自动
constexpr quint8 CMD_HEARTBEAT   = 0x30;  // 心跳/状态查询

// ===== 帧格式常量 =====
constexpr quint8 FRAME_HEAD1    = 0xAA;   // 数据帧帧头1
constexpr quint8 FRAME_HEAD2    = 0xBB;   // 数据帧帧头2
constexpr quint8 FRAME_TAIL1    = 0x0D;   // 数据帧帧尾1
constexpr quint8 FRAME_TAIL2    = 0x0A;   // 数据帧帧尾2
constexpr quint8 FRAME_ACK_HEAD = 0xBB;   // ACK 帧头（应答帧 = BB + 指令码 + 状态）

// ===== 心跳应答状态字节位定义（与固件一致）=====
constexpr quint8 STATUS_BIT_FAN   = 0x01; // bit0: 风扇运行中
constexpr quint8 STATUS_BIT_AUTO  = 0x02; // bit1: 自动模式开启
constexpr quint8 STATUS_BIT_DHT11 = 0x04; // bit2: DHT11 正常

}

#endif // PROTOCOLDEFS_H
