// Bitmap text over gfx_font_sprite (ported from the 3DS gfx.c text path).
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float legible(float size) { return size < 11 ? 11 : size; }
static int bitmap_size(float size) {
  int px = (int)lroundf(legible(size));
  return px > 16 ? 16 : px;
}

static unsigned utf8_next(const char **text) {
  const u8 *p = (const u8 *)*text;
  unsigned c = *p++, n = c < 128 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : 0;
  if (n) c &= (1u << (6 - n)) - 1;
  while (n-- && *p && (*p & 0xc0) == 0x80) c = c << 6 | (*p++ & 63);
  *text = (const char *)p;
  return c;
}

static const FontGlyph *glyph(unsigned code, int size) {
  if (code > 0xffff) return NULL;
  int lo = 0, hi = font_glyph_count - 1;
  unsigned key = (unsigned)size << 16 | code;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const FontGlyph *g = &font_glyphs[mid];
    unsigned k = (unsigned)g->size << 16 | g->code;
    if (key == k) return g;
    if (k < key) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

static float bitmap_width(const char *s, int size) {
  float width = 0;
  while (*s) {
    const FontGlyph *g = glyph(utf8_next(&s), size);
    if (!g) return -1;
    width += g->advance;
  }
  return width;
}

void text_begin(void) {}

float text_draw(const char *s, float x, float y, float size, u32 color, int align) {
  int px = bitmap_size(size);
  float width = bitmap_width(s, px);
  if (width < 0) return 0;
  if (align == 1) x -= width / 2; else if (align == 2) x -= width;
  x = roundf(x); y = roundf(y);
  while (*s) {
    const FontGlyph *g = glyph(utf8_next(&s), px);
    if (!g) continue;
    gfx_font_sprite(g->x, g->y, g->w, g->h, x + g->left, y + px + g->top, false, color);
    x += g->advance;
  }
  return width;
}

float text_width(const char *s, float size) {
  float width = bitmap_width(s, bitmap_size(size));
  return width < 0 ? 0 : width;
}

void text_fit(char *s, float size, float max) {
  if (text_width(s, size) <= max) return;
  int original = (int)strlen(s), n = original;
  if (original < 3 || text_width("…", size) > max) { s[0] = 0; return; }
  while (n > 0) {
    n--;
    while (n > 0 && ((u8)s[n] & 0xC0) == 0x80) n--;
    if (n + 3 > original) continue;
    memcpy(s + n, "\xE2\x80\xA6", 4);
    if (text_width(s, size) <= max) return;
  }
  s[0] = 0;
}
