/**
 * encoder_input.h - 旋转编码器封装
 *
 * 基于 RotaryEncoderPCNT (硬件PCNT后台计数), 检测旋转方向,
 * 并管理当前工作模式 (在多个 EncoderMode 间切换)。
 */

#ifndef ENCODER_INPUT_H
#define ENCODER_INPUT_H

#include <Arduino.h>

// 旋转方向
enum class EncoderTurn : int8_t {
    NONE = 0,
    CW   = 1,    // 顺时针
    CCW  = -1,   // 逆时针
};

// 初始化编码器
void encoderBegin();

// 轮询编码器, 返回本次的旋转方向 (累计跨过的步数会被压缩为方向事件,
// 每个 detent 触发一次)
EncoderTurn encoderPoll();

// 当前工作模式索引
uint8_t encoderCurrentMode();

// 切换到下一个工作模式 (循环), 返回新的模式索引
uint8_t encoderNextMode();

#endif // ENCODER_INPUT_H
