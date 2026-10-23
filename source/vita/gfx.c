// Tweebbo Vita graphics: vita2d with nearest filtering and a 2× integer canvas.
// Implements the gfx.h contract (ABGR colors — the native vita2d format).
#include "gfx.h"
#include "audio.h"
#include "gen/snow_assets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>
#include "common.h"
#include "finishes.h"
#include "client_shop.h"
#include "photo_pixels.h"

#define ATLAS_MAX 16
/* Big enough for the busiest room (lobby_hall + players), so a frame never evicts what
   it is about to draw. Furniture lives in main memory (4 KB granularity), not CDRAM. */
#define FURNI_CACHE 512
#define FURNI_BYTES (64u * 1024 * 1024)
/* vita2d's per-frame vertex pool (default 1 MB). Every draw is skipped once it runs low:
   vita2d writes the vertices through an unchecked pointer. */
#define VERTEX_POOL (4 * 1024 * 1024)
#define POOL_RESERVE 1024
/* The Vita screen is 960×544: the 480×272 canvas fits it exactly at 2×. */
#define S 2.0f

static vita2d_texture *atlas[ATLAS_MAX];
static vita2d_texture *font_tex;
static vita2d_texture *pet_tex[32];
static vita2d_texture *mask_tri_up, *mask_tri_down, *mask_disc;
static float view_x,view_y,view_kx=1,view_ky=1;
void gfx_sprite_transform(float x,float y,float kx,float ky){view_x=x;view_y=y;view_kx=kx;view_ky=ky;}
static bool additive;
static int frame_draws, frame_skipped, max_draws;
static u8 *photo_pending;
static int photo_status;
static vita2d_texture *photo_texture;
void gfx_photo_request(u8 *gray){photo_pending=gray;photo_status=0;}
int gfx_photo_status(void){return photo_status;}
bool gfx_photo_preview(const u8 *gray) {
  if(!photo_texture)photo_texture=vita2d_create_empty_texture(322,234);
  if(!photo_texture)return false;
  u32 *pixels=vita2d_texture_get_datap(photo_texture);
  int stride=vita2d_texture_get_stride(photo_texture)/4;
  for(int y=0;y<234;y++)for(int x=0;x<322;x++)pixels[y*stride+x]=photo_duotone(gray[y*322+x]);
  return true;
}

/* True when there is room for one more quad in this frame's vertex pool. */
static bool pool_ok(void) {
  if (vita2d_pool_free_space() > POOL_RESERVE) { frame_draws++; return true; }
  frame_skipped++;
  return false;
}
void gfx_photo_draw(float x,float y,float w,float h) {
  if(photo_texture && pool_ok())vita2d_draw_texture_scale(photo_texture,x*S,y*S,w*S/322,h*S/234);
}

#include "furni-cache.h"
static FurniTex furni_cache[FURNI_CACHE];
static u32 furni_clock;
static u32 furni_bytes;

static void furni_evict(FurniTex *slot) {
  if(slot->tex)vita2d_free_texture(slot->tex);
  furni_bytes-=slot->bytes;
  memset(slot,0,sizeof *slot);
}

/* Furniture queued during a frame; loaded at the start of the next one (GXM
   textures must not be created in the middle of a drawing pass). */
static u16 pend_def[64];
static int pend_n;

static struct { vita2d_texture *tex[4]; const RoomBackground *bg; int pages; char wall[9],floor[9]; bool finish_attempted; } room;
static void room_finish(void);
static struct { vita2d_texture *tex[4]; const Scene *scene; int pages; } scene_v;

/* PNG decoding goes through the vendored stb_image (no libpng/NEON at runtime):
   the previous dump showed the fault inside png_read_filter_row_paeth3_neon. */
#include "stb_image.h"

static vita2d_texture *texture_from_rgba(const unsigned char *px, int w, int h) {
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return NULL;
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
      /* Many small textures: main memory (4 KB granularity) instead of 256 KB CDRAM blocks. */
      vita2d_texture_set_alloc_memblock_type(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE);
      vita2d_texture *t = decode_png(buf, pak.idx[mid].size);
      vita2d_texture_set_alloc_memblock_type(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
      free(buf);
      return t;
    }
    if (pak.idx[mid].key < key) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

bool gfx_init(void) {
  dbg_log("gfx: vita2d_init...");
  vita2d_init_advanced(VERTEX_POOL);
  vita2d_set_clear_color(RGBA8(0x0b, 0x0d, 0x0e, 255));
  dbg_log("gfx: vita2d ready");
  // The export decides the atlas pages (atlas0..N and atlasg0..N).
  for (int i = 0; i < atlas_page_count && i < ATLAS_MAX; i++) {
    char path[64];
    snprintf(path, sizeof path, "app0:gfx/%s.png", atlas_page_files[i]);
    atlas[i] = load_png(path);
    dbg_log("gfx: %s %s", atlas_page_files[i], atlas[i] ? "ok" : "missing");
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
  vita2d_wait_rendering_done();
  if(photo_texture){vita2d_free_texture(photo_texture);photo_texture=NULL;}
  audio_exit();
  vita2d_wait_rendering_done();
  gfx_snow_free();
  for(int i=0;i<COUNT(pet_tex);i++)if(pet_tex[i]){vita2d_free_texture(pet_tex[i]);pet_tex[i]=NULL;}
  for (int i = 0; i < ATLAS_MAX; i++) if (atlas[i]) vita2d_free_texture(atlas[i]);
  if (font_tex) vita2d_free_texture(font_tex);
  if (mask_tri_up) vita2d_free_texture(mask_tri_up);
  if (mask_tri_down) vita2d_free_texture(mask_tri_down);
  if (mask_disc) vita2d_free_texture(mask_disc);
  gfx_room_release();
  for(int i=0;i<FURNI_CACHE;i++)furni_evict(&furni_cache[i]);
  if(pak.f)fclose(pak.f);
  free(pak.idx);memset(&pak,0,sizeof pak);
  for (int i = 0; i < 4; i++) if (scene_v.tex[i]) { vita2d_free_texture(scene_v.tex[i]); scene_v.tex[i] = NULL; }
  vita2d_fini();
}

void gfx_frame(void) {
  room_finish();
  /* Load the furniture queued during the previous frame (never mid-pass). */
  u16 defs[COUNT(pend_def)];
  int n = pend_n;
  memcpy(defs, pend_def, sizeof defs);
  pend_n = 0;
  for (int i = 0; i < n; i++) gfx_furni_preload(defs[i]);
}

void gfx_begin(void) {
  frame_draws = frame_skipped = 0;
  vita2d_start_drawing();
  vita2d_clear_screen();
}

/* The GPU finishes every frame before the CPU continues. Between two frames the app frees
   textures (room change, furniture cache) and vita2d rewinds its vertex pool: doing either
   while the GPU still reads them faults the GPU (the GPUCRASH dumps). The scene is light,
   so waiting costs nothing visible at 60 fps. */
void gfx_present(void) {
#ifdef TWEEBBO_UI_TEST
  static unsigned frames;
  if(frames%60==0)dbg_log("native frame %u: %d draws, %u bytes cached, pool %u",frames,frame_draws,furni_bytes,vita2d_pool_free_space());
  frames++;
#endif
  vita2d_end_drawing();
  if(photo_pending) {
    vita2d_wait_rendering_done();
    const u32 *frame=vita2d_get_current_fb();
    photo_status=frame?1:-1;
    if(frame)photo_sample(photo_pending,frame,1024,(960-322*2)/2,(544-234*2)/2,2);
    photo_pending=NULL;
  }
  vita2d_swap_buffers();
  vita2d_wait_rendering_done();
  if (frame_skipped || frame_draws > max_draws + 1000) {
    if (frame_draws > max_draws) max_draws = frame_draws;
    dbg_log("gfx: %d draws this frame, %d skipped (vertex pool)", frame_draws, frame_skipped);
  }
}

static void draw_tex(vita2d_texture *t, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip,
                     u32 color) {
  if (!t || sw <= 0 || sh <= 0 || kx == 0 || ky == 0 || !pool_ok()) return;
  x=view_x+x*view_kx;y=view_y+y*view_ky;kx*=view_kx;ky*=view_ky;
  float dx = x * S, dy = y * S, ax = kx * S, ay = ky * S;
  if (flip) {
    /* Negative x scale mirrors the quad; adjust the origin so the sprite stays in place. */
    dx += sw * ax;
    ax = -ax;
  }
  vita2d_draw_texture_tint_part_scale(t, dx, dy, sx, sy, sw, sh, ax, ay, color);
}

void gfx_sprite(int page, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color) {
  if (page < 0 || page >= atlas_page_count || !atlas[page]) return;
  draw_tex(atlas[page], sx, sy, sw, sh, x, y, scale, scale, flip, color);
}

void gfx_sprite_xy(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  if (page < 0 || page >= atlas_page_count || !atlas[page]) return;
  draw_tex(atlas[page], sx, sy, sw, sh, x, y, kx, ky, flip, color);
}

void gfx_sprite_solid(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color) {
  /* Approximate silhouette: the sprite multiplied by the veil color. */
  if (page < 0 || page >= atlas_page_count || !atlas[page]) return;
  draw_tex(atlas[page], sx, sy, sw, sh, x, y, kx, ky, flip, color);
}

void gfx_font_sprite(int sx, int sy, int sw, int sh, float x, float y, bool flip, u32 color) {
  draw_tex(font_tex, sx, sy, sw, sh, x, y, 1, 1, flip, color);
}
void gfx_pet_preload(int palette) {
  if(palette<0 || palette>=pet_palette_count || palette>=COUNT(pet_tex) || pet_tex[palette])return;
  char path[64];snprintf(path,sizeof path,"app0:gfx/pets_%d.png",palette);
  vita2d_texture_set_alloc_memblock_type(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE);
  pet_tex[palette]=load_png(path);
  vita2d_texture_set_alloc_memblock_type(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
}
void gfx_pet(int palette,const PetPart *part,float x,float y,float scale,bool flip,u32 color,bool solid) {
  (void)solid;
  if(palette<0 || palette>=COUNT(pet_tex))return;
  draw_tex(pet_tex[palette],part->x,part->y,part->w,part->h,x,y,scale,scale,flip,color);
}

static vita2d_texture *furni_fetch(u16 def, u16 page) {
  FurniTex *slot = furni_slot(furni_cache,FURNI_CACHE,def,page);
  if (!slot->used || slot->def!=def || slot->page!=page) {
    furni_evict(slot);
    u32 bytes=0;
    for(int i=0;i<furni_texture_count;i++)if(furni_textures[i].def==def && furni_textures[i].page==page)bytes=furni_textures[i].bytes;
    if(bytes>FURNI_BYTES)return NULL;
    while(furni_bytes+bytes>FURNI_BYTES) {
      FurniTex *oldest=furni_oldest(furni_cache,FURNI_CACHE,slot);
      if(!oldest)break;
      furni_evict(oldest);
    }
    slot->tex = furni_blob(def, page);
    if(!slot->tex)dbg_log("furniture texture unavailable: %u page %u",def,page);
    slot->bytes=slot->tex?bytes:0;furni_bytes+=slot->bytes;
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
  room.finish_attempted=false;
}

static void room_finish(void) {
  const char *wall=client_shop_wall(),*floor=client_shop_floor();
  if(!room.bg || (room.finish_attempted && !strcmp(room.wall,wall) && !strcmp(room.floor,floor)))return;
  vita2d_texture *next[4]={0};bool ok=true;
  for(int i=0;i<room.pages && ok;i++) {
    char path[96];snprintf(path,sizeof path,"app0:gfx/%s.png",room.bg->tiles[i].file);
    next[i]=load_png(path);
    ok=next[i] && finishes_paint("app0:spaces",room.bg,i,wall,floor,
      vita2d_texture_get_datap(next[i]),vita2d_texture_get_width(next[i]),
      vita2d_texture_get_height(next[i]),vita2d_texture_get_stride(next[i]));
  }
  for(int i=0;i<room.pages;i++) {
    if(ok) {vita2d_free_texture(room.tex[i]);room.tex[i]=next[i];}
    else if(next[i])vita2d_free_texture(next[i]);
  }
  if(!ok)dbg_log("room finishes unavailable: %s",room.bg->model);
  snprintf(room.wall,sizeof room.wall,"%s",wall);snprintf(room.floor,sizeof room.floor,"%s",floor);
  room.finish_attempted=true;
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
void gfx_additive(bool on) {
  if (on == additive) return;
  additive = on;
  vita2d_set_blend_mode_add(on);
}

// ------------------------------------------------------------------ primitives

void fill_rect(float x, float y, float w, float h, u32 color) {
  if (w <= 0 || h <= 0 || !pool_ok()) return;
  vita2d_draw_rectangle(x * S, y * S, w * S, h * S, color);
}

void fill_circle(float cx, float cy, float r, u32 color) {
  if (!mask_disc || r <= 0 || !pool_ok()) return;
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
  if (!mask || !pool_ok()) return;
  vita2d_draw_texture_tint_scale(mask, xmin * S, ymin * S, (w / 64.0f) * S, (h / 32.0f) * S, color);
}

void fill_round(float x, float y, float w, float h, float r, u32 color) {
  x=roundf(x); y=roundf(y); w=roundf(w); h=roundf(h);
  if(w<=0 || h<=0)return;
  r=fmaxf(0,fminf(r,fminf(w,h)/2));
  int band=(int)ceilf(r);
  for(int i=0;i<band && i<h/2;i++) {
    float dy=fmaxf(0,r-(i+.5f));
    float inset=roundf(r-sqrtf(fmaxf(0,r*r-dy*dy)));
    fill_rect(x+inset,y+i,w-2*inset,1,color);
    if(h-1-i!=i)fill_rect(x+inset,y+h-1-i,w-2*inset,1,color);
  }
  if(h>2*band)fill_rect(x,y+band,w,h-2*band,color);
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

static vita2d_texture *snow_pages[12];
static unsigned snow_loaded;
void gfx_snow_free(void) {
  while(snow_loaded) vita2d_free_texture(snow_pages[--snow_loaded]);
}
bool gfx_snow_load(void) {
  if(snow_loaded==snow_page_count)return true;
  gfx_snow_free();
  if(snow_page_count>COUNT(snow_pages))return false;
  for(unsigned i=0;i<snow_page_count;i++){
    char path[128];snprintf(path,sizeof path,"app0:snow/%s.png",snow_page_files[i]);
    vita2d_texture *p=load_png(path);
    if(!p){gfx_snow_free();return false;}
    snow_pages[snow_loaded++]=p;
  }
  return true;
}
#include "../snow_draw.inc"

const SceneArt *gfx_art(const char *name){
  if(!scene_v.scene)return NULL;
  for(int i=0;i<scene_v.scene->art_count;i++)if(!strcmp(scene_v.scene->art[i].name,name))return &scene_v.scene->art[i];
  return NULL;
}
void gfx_art_draw(const SceneArt *a,float x,float y,float kx,float ky,u32 color){
  if(a && scene_v.scene && a->page<scene_v.pages && a->page<4)
    draw_tex(scene_v.tex[a->page],a->sx,a->sy,a->w,a->h,x,y,kx,ky,false,color);
}
