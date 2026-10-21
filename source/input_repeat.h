#pragma once
#include "common.h"

typedef struct { int dx, dy; u64 next; } InputRepeat;

static inline bool input_repeat(InputRepeat *r, int dx, int dy, u64 now, unsigned interval) {
  if (!dx && !dy) { r->dx = r->dy = 0; r->next = 0; return false; }
  bool changed = dx != r->dx || dy != r->dy;
  if (!changed && now < r->next) return false;
  r->dx = dx; r->dy = dy; r->next = now + interval;
  return true;
}
