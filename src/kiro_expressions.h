/**
 * kiro_expressions.h - Auto-generated RGB565 frame data for round LCD
 *
 * 3 expressions x 40 frames @ 4fps, 120x120 pixels
 * Total size: ~3.3MB (stored in PROGMEM/Flash)
 */

#ifndef KIRO_EXPRESSIONS_H
#define KIRO_EXPRESSIONS_H

#include <Arduino.h>

#define EXPR_FRAME_W 120
#define EXPR_FRAME_H 120
#define EXPR_FPS 4
#define EXPR_FRAME_COUNT 40
#define EXPR_COUNT 3

extern const uint16_t kiro_idle_frames[40][14400] PROGMEM;
extern const uint16_t kiro_wait_frames[40][14400] PROGMEM;
extern const uint16_t kiro_work_frames[40][14400] PROGMEM;

// Array of pointers to expression frame arrays
extern const uint16_t (*kiro_expressions[EXPR_COUNT])[14400];

#endif // KIRO_EXPRESSIONS_H
