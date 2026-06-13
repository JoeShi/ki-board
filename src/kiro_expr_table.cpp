#include "kiro_expressions.h"

const uint16_t (*kiro_expressions[EXPR_COUNT])[14400] = {
  kiro_idle_frames,
  kiro_wait_frames,
  kiro_work_frames,
};
