#pragma once
#include "assets.h"

/* Paint a freshly decoded RGBA room page. The caller discards it on failure. */
bool finishes_paint(const char *root, const RoomBackground *bg, int page,
                    const char *wall, const char *floor, u8 *rgba,
                    int width, int height, size_t stride);
