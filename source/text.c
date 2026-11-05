// Bitmap text over gfx_font_sprite (ported from the 3DS gfx.c text path).
#include "gfx.h"
#include "language.h"
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

/** Binary search of the bitmap font; ASCII results are memoized per size because text is
 *  measured and drawn glyph by glyph every frame (labels, fitting, paragraph wrapping). */
static const FontGlyph *glyph_search(unsigned code, int size) {
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

#define GLYPH_SIZES 17
static const FontGlyph *ascii_glyphs[GLYPH_SIZES][128];
static bool ascii_ready[GLYPH_SIZES];

static const FontGlyph *glyph(unsigned code, int size) {
  if (code > 0xffff) return NULL;
  if (code < 128 && size >= 0 && size < GLYPH_SIZES) {
    if (!ascii_ready[size]) {
      for (unsigned c = 0; c < 128; c++) ascii_glyphs[size][c] = glyph_search(c, size);
      ascii_ready[size] = true;
    }
    if (ascii_glyphs[size][code]) return ascii_glyphs[size][code];
  } else {
    const FontGlyph *g = glyph_search(code, size);
    if (g) return g;
  }
  return (tw_language()==TW_ZH||tw_language()==TW_JA)?tw_cjk_glyph(code):NULL;
}

/** Width of one code point as bitmap_width counts it (missing glyphs measure as '?'). */
static float advance(unsigned code, int size) {
  const FontGlyph *g = glyph(code, size);
  if (!g) g = glyph('?', size);
  return g ? g->advance : 0;
}

static float bitmap_width(const char *s, int size) {
  float width = 0;
  while (*s) {
    const FontGlyph *g = glyph(utf8_next(&s), size);
    if (!g) g = glyph('?', size);
    if (g) width += g->advance;
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
    if (!g) g = glyph('?', px);
    if (!g) continue;
    if(g->code>255&&g==tw_cjk_glyph(g->code))gfx_cjk_sprite(g->x,g->y,g->w,g->h,x+g->left,y+px+g->top,color);
    else gfx_font_sprite(g->x, g->y, g->w, g->h, x + g->left, y + px + g->top, false, color);
    x += g->advance;
  }
  return width;
}

float text_width(const char *s, float size) {
  float width = bitmap_width(s, bitmap_size(size));
  return width < 0 ? 0 : width;
}

/** One pass: the longest whole-character prefix whose width plus the ellipsis fits. Same
 *  result as trimming and re-measuring each prefix, which was quadratic per label per frame. */
void text_fit(char *s, float size, float max) {
  int px = bitmap_size(size), original = (int)strlen(s);
  if (bitmap_width(s, px) <= max) return;
  float dots = bitmap_width("…", px);
  if (original < 3 || dots > max) { s[0] = 0; return; }
  const char *p = s;
  int best = -1;
  float width = 0;
  while (*p) {
    int at = (int)(p - s);
    if (at + 3 <= original && width + dots <= max) best = at;
    width += advance(utf8_next(&p), px);
  }
  if (best < 0) { s[0] = 0; return; }
  memcpy(s + best, "\xE2\x80\xA6", 4);
}
