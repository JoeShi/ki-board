// Kiro ghost 8 expressions 120x120 RGB565
#ifndef KIRO_LOGO_H
#define KIRO_LOGO_H

#include <Arduino.h>
#include <stdint.h>

#define LOGO_WIDTH  120
#define LOGO_HEIGHT 120
#define KIRO_FRAMES 8

extern const uint16_t kiro_idle_normal[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;
extern const uint16_t kiro_idle_blink[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;
extern const uint16_t kiro_idle_sleep[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;
extern const uint16_t kiro_work_focus[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;
extern const uint16_t kiro_work_busy[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;
extern const uint16_t kiro_wait_look[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;
extern const uint16_t kiro_wait_expect[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;
extern const uint16_t kiro_wait_listen[LOGO_WIDTH * LOGO_HEIGHT] PROGMEM;

#endif // KIRO_LOGO_H
