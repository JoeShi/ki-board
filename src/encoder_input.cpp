/**
 * encoder_input.cpp - 旋转编码器封装实现
 *
 * RotaryEncoderPCNT.position() 返回累计计数 (int32)。
 * EC11 编码器通常每个物理 detent (档位) 产生 4 个计数,
 * 因此用 COUNTS_PER_DETENT 把原始计数压缩成档位事件。
 */

#include "encoder_input.h"
#include "pins.h"
#include "config.h"
#include <RotaryEncoderPCNT.h>

// EC11 每档计数 (常见为4, 视具体编码器可调)
#define COUNTS_PER_DETENT  4

static RotaryEncoderPCNT s_encoder(PIN_EC11_A, PIN_EC11_B);
static int32_t s_lastCount = 0;
static uint8_t s_currentMode = 0;

void encoderBegin() {
    s_lastCount = s_encoder.position();
    s_currentMode = 0;
    Serial.println("[ENC] Encoder initialized");
}

EncoderTurn encoderPoll() {
    int32_t pos = s_encoder.position();
    int32_t delta = pos - s_lastCount;

    // 未达到一个完整档位, 不触发
    if (delta <= -COUNTS_PER_DETENT) {
        s_lastCount = pos;
        return EncoderTurn::CCW;
    } else if (delta >= COUNTS_PER_DETENT) {
        s_lastCount = pos;
        return EncoderTurn::CW;
    }

    return EncoderTurn::NONE;
}

uint8_t encoderCurrentMode() {
    return s_currentMode;
}

uint8_t encoderNextMode() {
    s_currentMode = (s_currentMode + 1) % NUM_ENCODER_MODES;
    Serial.printf("[ENC] Switched to mode %d\n", s_currentMode);
    return s_currentMode;
}
