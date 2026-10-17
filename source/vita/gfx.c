// Tweebbo Vita graphics: vita2d with nearest filtering and a 2× integer canvas.
// Implements the gfx.h contract (ABGR colors — the native vita2d format).
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>
#include "common.h"

#define ATLAS_PAGES 5
#define FURNI_CACHE 64
/* The Vita screen is 960×544: the 480×272 canvas fits it exactly at 2×. */
#define S 2.0f

static vita2d_texture *atlas[ATLAS_PAGES];
static vita2d_texture *font_tex;
static bool additive;

typedef struct { u16 def; u8 page; bool used; vita2d_texture *tex; u32 stamp; } FurniTex;
static FurniTex furni_cache[FURNI_CACHE];
static u32 furni_clock;

static struct { vita2d_texture *tex[4]; const RoomBackground *bg; int pages; } room;
static struct { vita2d_texture *tex[4]; const Scene *scene; int pages; } scene_v;

static vita2d_texture *load_png(const char *path) {
  vita2d_texture *t = vita2d_load_PNG_file(path);
  if (!t) { dbg_log("texture missing: %s", path); return NULL; }
  vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
  return t;
}

/* Furniture textures travel in a single romfs/furni.pak (index at the end), so the VPK
   only carries ~35 files and VitaShell installs it as a copy instead of thousands of
   small writes. Each texture is decoded on demand, exactly like the per-file cache. */
typedef struct { u32 key, offset, size; } FurniIndex;
static struct { FILE *f; FurniIndex *idx; int count; } pak;

static void pak_open(void) {
  if (pak.f) return;
  pak.f = fopen("app0:furni.pak", "rb");
  if (!pak.f) { dbg_log("furni.pak missing: run tools/fetch-assets.sh"); return; }
  if (fseek(pak.f, -8, SEEK_END) != 0) return;
  u32 index_offset = 0, count = 0;
  if (fread(&index_offset, 4, 1, pak.f) != 1 || fread(&count, 4, 1, pak.f) != 1 || count > 16384) return;
  pak.idx = malloc(count * sizeof(FurniIndex));
  if (!pak.idx) return;
  if (fseek(pak.f, (long)index_offset, SEEK_SET) != 0 || fread(pak.idx, sizeof(FurniIndex), count, pak.f) != count) {
    free(pak.idx);
    pak.idx = NULL;
    return;
  }
  pak.count = (int)count;
}

static vita2d_texture *furni_blob(u16 def, u16 page) {
  pak_open();
  if (!pak.f || !pak.idx) return NULL;
  u32 key = (u32)def | ((u32)page << 16);
  int lo = 0, hi = pak.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (pak.idx[mid].key == key) {
      u8 *buf = malloc(pak.idx[mid].size);
      if (!buf) return NULL;
      if (fseek(pak.f, (long)pak.idx[mid].offset, SEEK_SET) != 0 || fread(buf, 1, pak.idx[mid].size, pak.f) != pak.idx[mid].size) {
        free(buf);
        return NULL;
      }
      vita2d_texture *t = vita2d_load_PNG_buffer(buf);
      free(buf);
      if (t) vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
      return t;
    }
    if (pak.idx[mid].key < key) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

bool gfx_init(void) {
  vita2d_init();
  vita2d_set_clear_color(RGBA8(0x0b, 0x0d, 0x0e, 255));
  static const char *pages[ATLAS_PAGES] = {"atlas0", "atlasg0", "atlasg1", "atlasg2", "atlasg3"};
  for (int i = 0; i < ATLAS_PAGES; i++) {
    char path[64];
    snprintf(path, sizeof path, "app0:gfx/%s.png", pages[i]);
    atlas[i] = load_png(path);
    if (!atlas[i] && i == 0) return false;
  }
  font_tex = load_png("app0:gfx/font0.png");
  dbg_log("gfx ready: vita2d, canvas %dx%d at %.0fx", SCREEN_W, SCREEN_H, S);
  return true;
}

void gfx_exit(void) {
  for (int i = 0; i < ATLAS_PAGES; i++) if (atlas[i]) vita2d_free_texture(atlas[i]);
  if (font_tex) vita2d_free_texture(font_tex);
  gfx_room_release();
  for (int i = 0; i < 4; i++) if (scene_v.tex[i]) { vita2d_free_texture(scene_v.tex[i]); scene_v.tex[i] = NULL; }
  vita2d_fini();
}

void gfx_frame(void) {}

void gfx_begin(void) { vita2d_start_drawing(); vita2d_clear_screen(); }
void gfx_present(void) { vita2d_end_drawing(); vita2d_swap_buffers(); }

static void draw_tex(vita2d_texture *t, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip,
                     u32 color) {
  if (!t) return;
  float dx = x * S, dy = y * S, ax = kx * S, ay = ky * S;
  if (flip) {
    /* Negative x scale mirrors the quad; adjust the origin so the sprite stays in place. */
    dx += sw * ax;
    ax = -ax;
  }
  if (additive) {
    vita2d_draw_texture_tint_part_scale(t, dx, dy, sx, sy, sw, sh, ax, ay, color);
  } else {
    vita2d_draw_texture_tint_part_scale(t, dx, dy, sx, sy, sw, sh, ax, ay, color);
  }
}

void gfx_sprite(int page, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color) {
  if (page < 0 || page >= ATLAS_PAGES) return;
  draw_tex(atlas[page], sx, sy, sw, sh, x, y, scale, scale, flip, color);
}

void gfx_sprite_xy(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  if (page < 0 || page >= ATLAS_PAGES) return;
  draw_tex(atlas[page], sx, sy, sw, sh, x, y, kx, ky, flip, color);
}

void gfx_sprite_solid(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  /* Approximate silhouette: the sprite multiplied by the veil color. */
  if (page < 0 || page >= ATLAS_PAGES) return;
  draw_tex(atlas[page], sx, sy, sw, sh, x, y, kx, ky, flip, color);
}

void gfx_font_sprite(int sx, int sy, int sw, int sh, float x, float y, bool flip, u32 color) {
  draw_tex(font_tex, sx, sy, sw, sh, x, y, 1, 1, flip, color);
}

static vita2d_texture *furni_get(u16 def, int page) {
  FurniTex *slot = NULL, *victim = &furni_cache[0];
  for (int i = 0; i < FURNI_CACHE; i++) {
    FurniTex *c = &furni_cache[i];
    if (c->used && c->def == def && c->page == page) { slot = c; break; }
    if (!victim->used || victim->stamp > c->stamp) victim = c;
  }
  if (!slot) {
    slot = victim;
    if (slot->tex) vita2d_free_texture(slot->tex);
    slot->tex = furni_blob(def, (u16)page);
    slot->def = def;
    slot->page = (u8)page;
    slot->used = true;
  }
  slot->stamp = ++furni_clock;
  return slot->tex;
}

void gfx_furni(u16 def, const FurniPart *part, float x, float y, float scale, u32 color) {
  vita2d_texture *t = furni_get(def, part->page);
  if (!t) return;
  draw_tex(t, part->x, part->y, part->w, part->h, x, y, scale, scale, false, color);
}

void gfx_furni_preload(u16 def) { (void)def; }

const RoomBackground *gfx_room(const char *model) {
  for (int i = 0; i < room_background_count; i++) {
    const RoomBackground *bg = &room_backgrounds[i];
    if (strcmp(bg->model, model)) continue;
    if (room.bg == bg) return bg;
    gfx_room_release();
    if (bg->tile_count > 4) return NULL;
    for (int j = 0; j < bg->tile_count; j++) {
      char path[64];
      snprintf(path, sizeof path, "app0:gfx/%s.png", bg->tiles[j].file);
      room.tex[j] = load_png(path);
      if (!room.tex[j]) { gfx_room_release(); return NULL; }
    }
    room.bg = bg;
    room.pages = bg->tile_count;
    return bg;
  }
  return NULL;
}

void gfx_room_release(void) {
  for (int i = 0; i < 4; i++) if (room.tex[i]) { vita2d_free_texture(room.tex[i]); room.tex[i] = NULL; }
  room.bg = NULL;
  room.pages = 0;
}

void gfx_room_draw(const RoomBackground *bg, float x, float y, float scale) {
  if (!bg || room.bg != bg) return;
  for (int j = 0; j < room.pages; j++) {
    const RoomTile *t = &bg->tiles[j];
    draw_tex(room.tex[j], 0, 0, t->w, t->h, x + t->x * scale, y + t->y * scale, scale, scale, false, 0xffffffffu);
  }
}

const Scene *gfx_scene(const char *model) {
  const Scene *want = NULL;
  for (int i = 0; i < scene_count; i++) if (!strcmp(scenes[i].model, model)) want = &scenes[i];
  if (want == scene_v.scene) return want;
  for (int i = 0; i < 4; i++) if (scene_v.tex[i]) { vita2d_free_texture(scene_v.tex[i]); scene_v.tex[i] = NULL; }
  scene_v.scene = NULL;
  if (!want) return NULL;
  for (int i = 0; i < want->pages && i < 4; i++) {
    char path[64];
    snprintf(path, sizeof path, "app0:gfx/scene_%s_%d.png", want->model, i);
    scene_v.tex[i] = load_png(path);
    if (!scene_v.tex[i]) return NULL;
  }
  scene_v.scene = want;
  scene_v.pages = want->pages;
  return want;
}

void gfx_scene_sprite(const SceneSprite *s, float x, float y, float zoom, u8 alpha) {
  if (!scene_v.scene || s->page >= scene_v.pages) return;
  draw_tex(scene_v.tex[s->page], s->sx, s->sy, s->w, s->h, x, y, zoom * s->dw / (float)s->w, zoom * s->dh / (float)s->h,
           false, ((u32)alpha << 24) | 0xffffff);
}

void gfx_filter(bool linear) { (void)linear; }
void gfx_additive(bool on) { additive = on; }

// ------------------------------------------------------------------ primitives

void fill_rect(float x, float y, float w, float h, u32 color) {
  vita2d_draw_rectangle(x * S, y * S, w * S, h * S, color);
}

void fill_circle(float cx, float cy, float r, u32 color) {
  vita2d_draw_fill_circle(cx * S, cy * S, r * S, color);
}

void fill_round(float x, float y, float w, float h, float r, u32 color) {
  fill_rect(x + r, y, w - 2 * r, h, color);
  fill_rect(x, y + r, w, h - 2 * r, color);
  fill_circle(x + r, y + r, r, color);
  fill_circle(x + w - r, y + r, r, color);
  fill_circle(x + r, y + h - r, r, color);
  fill_circle(x + w - r, y + h - r, r, color);
}

static void rect_line(float x, float y, float w, float h, u32 color) {
  fill_rect(x, y, w, 1, color);
}

void fill_triangle(float x1, float y1, float x2, float y2, float x3, float y3, u32 color) {
  /* vita2d has no triangles: approximate the isometric tile with two rows of pixels. */
  float miny = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
  float maxy = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);
  for (float y = miny; y <= maxy; y++) {
    /* Walk the two edges of the diamond at this scanline. */
    float xs[3];
    xs[0] = x1 + (x2 - x1) * ((y - y1) / (y2 - y1 + 1e-6f));
    xs[1] = x2 + (x3 - x2) * ((y - y2) / (y3 - y2 + 1e-6f));
    xs[2] = x3 + (x1 - x3) * ((y - y3) / (y1 - y3 + 1e-6f));
    float lo = xs[0], hi = xs[0];
    for (int i = 1; i < 3; i++) { if (xs[i] < lo) lo = xs[i]; if (xs[i] > hi) hi = xs[i]; }
    if (hi > lo) rect_line(lo, y, hi - lo + 1, 1, color);
  }
}

void draw_line(float x1, float y1, float x2, float y2, u32 color) {
  /* Thin line drawn as 1-pixel steps; only used for accents in the PoC. */
  float dx = x2 - x1, dy = y2 - y1;
  int steps = (int)(fmaxf(fabsf(dx), fabsf(dy)) * S) + 1;
  for (int i = 0; i <= steps; i++) {
    float t = steps ? (float)i / steps : 0;
    fill_rect(x1 + dx * t, y1 + dy * t, 1, 1, color);
  }
}
