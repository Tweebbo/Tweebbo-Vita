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
static vita2d_texture *mask_tri_up, *mask_tri_down, *mask_disc;
static bool additive;

typedef struct { u16 def; u8 page; bool used; vita2d_texture *tex; u32 stamp; } FurniTex;
static FurniTex furni_cache[FURNI_CACHE];
static u32 furni_clock;

/* Furniture queued during a frame; loaded at the start of the next one (GXM
   textures must not be created in the middle of a drawing pass). */
static u16 pend_def[64];
static int pend_n;

static struct { vita2d_texture *tex[4]; const RoomBackground *bg; int pages; } room;
static struct { vita2d_texture *tex[4]; const Scene *scene; int pages; } scene_v;

/* PNG decoding goes through the vendored stb_image (no libpng/NEON at runtime):
   the previous dump showed the fault inside png_read_filter_row_paeth3_neon. */
#include "stb_image.h"

static vita2d_texture *texture_from_rgba(const unsigned char *px, int w, int h) {
  vita2d_texture *t = vita2d_create_empty_texture((unsigned)w, (unsigned)h);
  if (!t) return NULL;
  unsigned char *dst = vita2d_texture_get_datap(t);
  unsigned stride = vita2d_texture_get_stride(t);
  for (int y = 0; y < h; y++) memcpy(dst + (size_t)y * stride, px + (size_t)y * w * 4, (size_t)w * 4);
  vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
  return t;
}

static vita2d_texture *decode_png(const u8 *data, int len) {
  int w = 0, h = 0, comp = 0;
  unsigned char *px = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
  if (!px) { dbg_log("png decode failed (%d bytes)", len); return NULL; }
  vita2d_texture *t = texture_from_rgba(px, w, h);
  stbi_image_free(px);
  return t;
}

static vita2d_texture *load_png(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { dbg_log("texture missing: %s", path); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  u8 *buf = malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
    dbg_log("texture unreadable: %s", path);
    if (buf) free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  vita2d_texture *t = decode_png(buf, (int)len);
  free(buf);
  if (!t) dbg_log("texture broken: %s", path);
  return t;
}

/* Solid shapes are drawn as one textured quad each: rasterising per scanline flooded
   GXM with tens of thousands of rectangles per frame (kernel panic on real hardware).
   The masks are 64×32 (triangles) and 64×64 (disc), white with alpha in the shape. */
static vita2d_texture *make_mask(int kind) {
  const int w = 64, h = kind == 2 ? 64 : 32;
  vita2d_texture *t = vita2d_create_empty_texture(w, h);
  if (!t) return NULL;
  u32 *px = vita2d_texture_get_datap(t);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      bool in = false;
      if (kind == 0) in = y >= (h - 1) - (x < w / 2 ? x * 2 * h / w : (w - 1 - x) * 2 * h / w);
      else if (kind == 1) in = y <= (x < w / 2 ? x * 2 * h / w : (w - 1 - x) * 2 * h / w);
      else {
        float dx = x - (w - 1) / 2.0f, dy = y - (h - 1) / 2.0f;
        in = dx * dx + dy * dy <= ((w - 1) / 2.0f) * ((w - 1) / 2.0f);
      }
      px[y * w + x] = in ? 0xffffffffu : 0x00000000u;
    }
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
      vita2d_texture *t = decode_png(buf, pak.idx[mid].size);
      free(buf);
      return t;
    }
    if (pak.idx[mid].key < key) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

bool gfx_init(void) {
  dbg_log("gfx: vita2d_init...");
  vita2d_init();
  vita2d_set_clear_color(RGBA8(0x0b, 0x0d, 0x0e, 255));
  dbg_log("gfx: vita2d ready");
  static const char *pages[ATLAS_PAGES] = {"atlas0", "atlasg0", "atlasg1", "atlasg2", "atlasg3"};
  for (int i = 0; i < ATLAS_PAGES; i++) {
    char path[64];
    snprintf(path, sizeof path, "app0:gfx/%s.png", pages[i]);
    atlas[i] = load_png(path);
    dbg_log("gfx: %s %s", pages[i], atlas[i] ? "ok" : "missing");
    if (!atlas[i] && i == 0) return false;
  }
  font_tex = load_png("app0:gfx/font0.png");
  mask_tri_up = make_mask(0);
  mask_tri_down = make_mask(1);
  mask_disc = make_mask(2);
  dbg_log("gfx: masks %s", (mask_tri_up && mask_tri_down && mask_disc) ? "ok" : "FAILED");
  dbg_log("gfx ready: vita2d, canvas %dx%d at %.0fx", SCREEN_W, SCREEN_H, S);
  return true;
}

void gfx_exit(void) {
  for (int i = 0; i < ATLAS_PAGES; i++) if (atlas[i]) vita2d_free_texture(atlas[i]);
  if (font_tex) vita2d_free_texture(font_tex);
  if (mask_tri_up) vita2d_free_texture(mask_tri_up);
  if (mask_tri_down) vita2d_free_texture(mask_tri_down);
  if (mask_disc) vita2d_free_texture(mask_disc);
  gfx_room_release();
  for (int i = 0; i < 4; i++) if (scene_v.tex[i]) { vita2d_free_texture(scene_v.tex[i]); scene_v.tex[i] = NULL; }
  vita2d_fini();
}

void gfx_frame(void) {
  /* Load the furniture queued during the previous frame (never mid-pass). */
  u16 defs[COUNT(pend_def)];
  int n = pend_n;
  memcpy(defs, pend_def, sizeof defs);
  pend_n = 0;
  for (int i = 0; i < n; i++) gfx_furni_preload(defs[i]);
}

void gfx_begin(void) { vita2d_start_drawing(); vita2d_clear_screen(); }
void gfx_present(void) { vita2d_end_drawing(); vita2d_swap_buffers(); }

static void draw_tex(vita2d_texture *t, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip,
                     u32 color) {
  if (!t || sw <= 0 || sh <= 0 || kx == 0 || ky == 0) return;
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

static vita2d_texture *furni_fetch(u16 def, u16 page) {
  FurniTex *slot = NULL, *victim = &furni_cache[0];
  for (int i = 0; i < FURNI_CACHE; i++) {
    FurniTex *c = &furni_cache[i];
    if (c->used && c->def == def && c->page == page) { slot = c; break; }
    if (!victim->used || victim->stamp > c->stamp) victim = c;
  }
  if (!slot) {
    slot = victim;
    if (slot->tex) vita2d_free_texture(slot->tex);
    slot->tex = furni_blob(def, page);
    slot->def = def;
    slot->page = (u8)page;
    slot->used = true;
  }
  slot->stamp = ++furni_clock;
  return slot->tex;
}

static vita2d_texture *cache_lookup(u16 def, u16 page) {
  for (int i = 0; i < FURNI_CACHE; i++) {
    FurniTex *c = &furni_cache[i];
    if (c->used && c->def == def && c->page == page) {
      c->stamp = ++furni_clock;
      return c->tex;
    }
  }
  return NULL;
}

/* GXM textures must not be created in the middle of a drawing pass: a missing
   texture is queued and loaded at the start of the next frame (gfx_frame). */
static void pend_push(u16 def) {
  for (int i = 0; i < pend_n; i++) if (pend_def[i] == def) return;
  if (pend_n < COUNT(pend_def)) pend_def[pend_n++] = def;
}

void gfx_furni(u16 def, const FurniPart *part, float x, float y, float scale, u32 color) {
  vita2d_texture *t = cache_lookup(def, (u16)part->page);
  if (!t) {
    pend_push(def);
    return;
  }
  draw_tex(t, part->x, part->y, part->w, part->h, x, y, scale, scale, false, color);
}

void gfx_furni_preload(u16 def) {
  /* Outside the draw pass (room change): load every page of the definition now. */
  for (int i = 0; i < furni_texture_count; i++)
    if (furni_textures[i].def == def) furni_fetch(def, furni_textures[i].page);
}

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
  if (w <= 0 || h <= 0) return;
  vita2d_draw_rectangle(x * S, y * S, w * S, h * S, color);
}

void fill_circle(float cx, float cy, float r, u32 color) {
  if (!mask_disc) return;
  vita2d_draw_texture_tint_scale(mask_disc, (cx - r) * S, (cy - r) * S, (2 * r / 64.0f) * S, (2 * r / 64.0f) * S, color);
}

void fill_triangle(float x1, float y1, float x2, float y2, float x3, float y3, u32 color) {
  /* Isometric tile halves: one textured mask per shape, never per scanline. */
  const float ys[3] = {y1, y2, y3}, xs[3] = {x1, x2, x3};
  int at_min = 0, at_max = 0;
  float ymin = ys[0], ymax = ys[0], xmin = xs[0], xmax = xs[0];
  for (int i = 0; i < 3; i++) {
    if (ys[i] < ymin) ymin = ys[i];
    if (ys[i] > ymax) ymax = ys[i];
    if (xs[i] < xmin) xmin = xs[i];
    if (xs[i] > xmax) xmax = xs[i];
  }
  for (int i = 0; i < 3; i++) {
    if (ys[i] == ymin) at_min++;
    if (ys[i] == ymax) at_max++;
  }
  float w = xmax - xmin, h = ymax - ymin;
  if (w <= 0 || h <= 0) return;
  vita2d_texture *mask = at_min == 1 && at_max == 2 ? mask_tri_up : at_max == 1 && at_min == 2 ? mask_tri_down : mask_tri_up;
  if (!mask) return;
  vita2d_draw_texture_tint_scale(mask, xmin * S, ymin * S, (w / 64.0f) * S, (h / 32.0f) * S, color);
}

void fill_round(float x, float y, float w, float h, float r, u32 color) {
  fill_rect(x + r, y, w - 2 * r, h, color);
  fill_rect(x, y + r, w, h - 2 * r, color);
  fill_circle(x + r, y + r, r, color);
  fill_circle(x + w - r, y + r, r, color);
  fill_circle(x + r, y + h - r, r, color);
  fill_circle(x + w - r, y + h - r, r, color);
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
