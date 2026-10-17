// Showcase application: room rendering and interface, ported from the 3DS scene.c
// and reduced to what the Switch/Vita proof of concept needs (no build mode, pets,
// catalogue or games). The showcase rotates the five brand models with SET_LOOK so
// every console in the shared room sees the character change.
#include "show.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avatar.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "proto.h"
#include "world.h"

#define TW 64
#define TH 32
#define HALF_W 32
#define HALF_H 16

#define BUBBLE_H 19
#define BUBBLE_ROW 21
#define BUBBLE_MAX 7
#define BUBBLE_SCROLL_MS 4000
#define BUBBLE_SPEED 0.18f
#define BUBBLE_BASE 74
#define BUBBLE_TEXT 12.0f

#define LOOK_MS 5000
#define GESTURE_MS 11000
#define WALK_MS 9000

/** ABGR like citro2d: 0xAABBGGRR. */
static u32 rgba(u32 rgb, u8 a) { return (u32)a << 24 | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff); }
static u32 rgb(u32 v, u8 a) { return rgba(v, a); }

typedef struct {
  bool used;
  char name[24], text[104];
  u32 color;
  float wx, head_wy, y;
  int row;
  u64 t0;
} Bubble;

static struct {
  float cam_x, cam_y, zoom;
  bool snapped;
  const RoomBackground *bg;
  const Scene *scene;
  float bx0, by0, bx1, by1;
  Bubble chat[BUBBLE_MAX];
  u64 last_shift, last_frame, joined_at;
  const char *platform, *hello;
  bool quit;
  /* Showcase cast. */
  int cast;
  u64 next_look, next_gesture, next_walk;
} V = {.zoom = 2, .platform = "Console", .hello = "Hello from the hotel!"};

/* Short notice pill shown under the banner (hotel notices, kicks). */
static char toast[128];
static u64 toast_until;

static void iso(float x, float y, float *sx, float *sy) {
  *sx = (x - y) * HALF_W;
  *sy = (x + y) * HALF_H;
}

static float scr_x(float wx) { return roundf(V.cam_x * V.zoom) + wx * V.zoom; }
static float scr_y(float wy) { return roundf(V.cam_y * V.zoom) + wy * V.zoom; }

void show_set_platform(const char *name, const char *hello) {
  V.platform = name;
  V.hello = hello;
}

bool show_quit_requested(void) { return V.quit; }

// ------------------------------------------------------------------ showcase

/* The five brand character models (same table as the 3DS showcase build). */
static const Look CAST[] = {
  {.skin = 0, .hair_style = 1, .hair_color = 3, .shirt_color = 0, .shirt_style = 0, .pants = 0,
   .shoes = 0, .pants_style = 0, .shoe_style = 0, .face_style = 0},                                  /* Bobba */
  {.skin = 0, .hair_style = 5, .hair_color = 0, .shirt_color = 1, .shirt_style = 3, .pants = 1,
   .shoes = 1, .pants_style = 1, .shoe_style = 1, .face_style = 9},                                  /* Cyber */
  {.skin = 1, .hair_style = 12, .hair_color = 0, .shirt_color = 1, .shirt_style = 3, .pants = 1,
   .shoes = 0, .pants_style = 1, .shoe_style = 0, .face_style = 10},                                 /* Director */
  {.skin = 0, .hair_style = 6, .hair_color = 1, .shirt_color = 5, .shirt_style = 1, .pants = 0,
   .shoes = 1, .pants_style = 0, .shoe_style = 2, .face_style = 9},                                  /* Street */
  {.skin = 1, .hair_style = 3, .hair_color = 1, .shirt_color = 9, .shirt_style = 1, .pants = 6,
   .shoes = 2, .pants_style = 6, .shoe_style = 2, .face_style = 1},                                  /* VIP Girl */
};
static const char *CAST_NAME[] = {"Bobba", "Cyber", "Director", "Street", "VIP Girl"};

void show_start(u64 now) {
  V.cast = 0;
  V.next_look = now + 2500;
  V.next_gesture = now + 7000;
  V.next_walk = now + WALK_MS;
}

static void showcase_update(u64 now) {
  if (!net_online() || !W.in_room) return;
  if (now >= V.next_look) {
    V.next_look = now + LOOK_MS;
    V.cast = (V.cast + 1) % COUNT(CAST);
    send_look(look_encode(&CAST[V.cast]));
  }
  if (now >= V.next_gesture) {
    V.next_gesture = now + GESTURE_MS;
    send_gesture(V.cast % 2 ? GESTURE_WAVE : GESTURE_DANCE);
  }
  if (now >= V.next_walk) {
    V.next_walk = now + WALK_MS;
    Ent *me = world_ent(W.me);
    if (me) {
      int x = me->x + rnd_range(-3, 4), y = me->y + rnd_range(-2, 3);
      if (x >= 0 && y >= 0 && x < W.w && y < W.h && world_tile(x, y)) send_walk(x, y);
    }
  }
}

// ------------------------------------------------------------------ camera

static void camera(u64 now) {
  float lw = SCREEN_W / V.zoom, tx, ty;
  Ent *me = world_ent(W.me);
  if (!me) {
    tx = lw / 2 - (V.bx0 + V.bx1) / 2;
    ty = lw * .5f / (SCREEN_W / (float)SCREEN_H) - (V.by0 + V.by1) / 2;
  } else {
    float px, py, sx, sy;
    ent_pos(me, now, &px, &py);
    iso(px, py, &sx, &sy);
    tx = lw * .5f - sx;
    ty = (SCREEN_H / V.zoom) * .60f - sy + ent_height(me, now);
    if (V.bx1 - V.bx0 >= lw) tx = fminf(-V.bx0, fmaxf(lw - V.bx1, tx));
    else tx = lw / 2 - (V.bx0 + V.bx1) / 2;
    float lh = SCREEN_H / V.zoom;
    if (V.by1 - V.by0 >= lh) ty = fminf(-V.by0, fmaxf(lh - V.by1, ty));
    else ty = lh / 2 - (V.by0 + V.by1) / 2;
  }
  if (!V.snapped) { V.cam_x = tx; V.cam_y = ty; V.snapped = true; }
  V.cam_x += (tx - V.cam_x) * .12f;
  V.cam_y += (ty - V.cam_y) * .12f;
}

// ------------------------------------------------------------------ poses

typedef struct { Pose pose; int frame; float lift, dx, dy, depth, anchor_y; } PoseInfo;

static PoseInfo pose_of(const Ent *e, u64 now, bool moving) {
  PoseInfo p = {POSE_STD, 0, ent_height(e, now), 0, 0, 0, ANCHOR_Y};
  if ((e->flags & FLAG_LAY) && !moving) { p.pose = POSE_LAY; return p; }
  if ((e->flags & FLAG_SIT) && !moving) {
    Item *seat = world_item_at(e->x, e->y);
    const FurniDef *d = seat ? def_get(seat->def) : NULL;
    if (d && d->kind == KIND_SIT) { p.pose = POSE_SIT; p.lift = d->seat - SEATED_HIP + ent_height(e, now); }
    else p.pose = POSE_FLOOR;
    return p;
  }
  if (moving) { p.pose = POSE_WALK; p.frame = (int)(now / 125) & 3; }
  else if (e->flags & FLAG_WAVE) { p.pose = POSE_WAVE; p.frame = (int)(now / 220) & 1; }
  else if (e->flags & FLAG_DANCE) { p.pose = POSE_DANCE; p.frame = (int)(now / 160) & 7; }
  return p;
}

static int angle_dir(float dx, float dy) {
  return ((int)lroundf(atan2f(dx, -dy) / ((float)M_PI / 4)) + 8) & 7;
}

static int body_dir(const Ent *e, bool moving) {
  if (!moving) return e->dir;
  if (e->move_dir >= 0) return e->move_dir;
  return angle_dir(e->x - e->fx, e->y - e->fy);
}

static int head_dir(const Ent *e, u64 now, bool moving) {
  if (moving) return body_dir(e, true);
  if (e->flags & (FLAG_WAVE | FLAG_DANCE)) return e->dir;
  float ex, ey, best = 9, tx = 0, ty = 0;
  bool found = false;
  ent_pos(e, now, &ex, &ey);
  for (int i = 0; i < MAX_ENTS; i++) {
    const Ent *o = &W.ents[i];
    if (!o->used || o->id == e->id) continue;
    float x, y;
    ent_pos(o, now, &x, &y);
    float d = (x - ex) * (x - ex) + (y - ey) * (y - ey);
    if (d > .1f && d < best) { best = d; tx = x; ty = y; found = true; }
  }
  if (!found) return e->dir;
  float dx = tx - ex, dy = ty - ey;
  if (hypotf(dx, dy) < .4f) return e->dir;
  int facing = angle_dir(dx, dy), turn = (facing - e->dir + 8) & 7;
  return turn == 1 || turn == 7 ? facing : e->dir;
}

// ------------------------------------------------------------------ badges

static const char *const BADGE_3DS[] = {
  ".KKKKKKKKKK.", "K##########K", "K#SSSSSSSS#K", "K#SLSSSSSS#K", "K#SSSSSSSS#K", "K##########K", ".KHHHHHHHHK.",
  "K##########K", "K#c#ssss#b#K", "Kccc#ss#b#bK", "K#c#ssss#b#K", "K##########K", ".KKKKKKKKKK.",
};
static const char *const BADGE_SWITCH[] = {
  ".KKKKKKKKKKKK.", "KBBKSSSSSSKRRK", "KBcKSLSSSSKRbK", "KBBKSSSSSSKbRK", "KBbKSSSSSSKRRK",
  "KBBKSSSSSSKRcK", "KBBKSSSSSSKRRK", "KBBKKKKKKKKRRK", ".KKKKKKKKKKKK.",
};
static const char *const BADGE_VITA[] = {
  ".KKKKKKKKKK.", "KWWKBBBBKWWK", "KWcKBBBBKcWK", "KWWKBBBBKWWK", "KwWKBLLBKWWK",
  "KWWKBBBBKWWK", "KWWKBBBBKWWK", "KWWKKKKKKWWK", ".KKKKKKKKKK.",
};
typedef struct { char ch; u8 r, g, b; } BadgeColor;
static const BadgeColor PAL_3DS[] = {{'K',0x14,0x18,0x1c},{'#',0x3f,0xa6,0xe0},{'H',0x23,0x66,0x8f},{'S',0x8f,0xd9,0xff},{'L',0xe8,0xf8,0xff},{'s',0xd7,0xee,0xf8},{'c',0x2b,0x31,0x38},{'b',0x2b,0x31,0x38}};
static const BadgeColor PAL_SWITCH[] = {{'K',0x14,0x18,0x1c},{'B',0x00,0xc3,0xe3},{'R',0xff,0x45,0x54},{'S',0x23,0x2a,0x30},{'L',0x8f,0xd9,0xff},{'c',0x14,0x18,0x1c},{'b',0xf6,0xf6,0xef}};
static const BadgeColor PAL_VITA[] = {{'K',0x14,0x18,0x1c},{'W',0x2c,0x35,0x3d},{'B',0x1b,0x5b,0x91},{'L',0x2d,0x7f,0xc4},{'c',0x0f,0x14,0x17},{'w',0x8f,0xd9,0xff}};

static void badge_draw(const char *const *art, int rows, const BadgeColor *pal, int pal_n, float x, float y, float k) {
  for (int r = 0; r < rows; r++)
    for (int c = 0; art[r][c]; c++) {
      if (art[r][c] == '.') continue;
      for (int p = 0; p < pal_n; p++)
        if (pal[p].ch == art[r][c]) {
          fill_rect(x + c * k, y + r * k, k, k, rgba(((u32)pal[p].r << 16) | ((u32)pal[p].g << 8) | pal[p].b, 255));
          break;
        }
    }
}

static void device_badge(int device, float x, float y, float k) {
  if (device == DEVICE_SWITCH) badge_draw(BADGE_SWITCH, 9, PAL_SWITCH, COUNT(PAL_SWITCH), x, y, k);
  else if (device == DEVICE_VITA) badge_draw(BADGE_VITA, 9, PAL_VITA, COUNT(PAL_VITA), x, y, k);
  else badge_draw(BADGE_3DS, 13, PAL_3DS, COUNT(PAL_3DS), x, y, k);
}

static void device_badge_size(int device, int *w, int *h) {
  if (device == DEVICE_SWITCH) { *w = 14; *h = 9; }
  else if (device == DEVICE_VITA) { *w = 12; *h = 9; }
  else { *w = 12; *h = 13; }
}

// ------------------------------------------------------------------ drawing

static void fallback_floor(void) {
  u32 a = rgb(0xc5ae88, 255), b = rgb(0xc8b28d, 255);
  for (int y = 0; y < W.h; y++)
    for (int x = 0; x < W.w; x++) {
      if (!world_tile(x, y)) continue;
      float sx, sy, z = V.zoom;
      iso(x, y, &sx, &sy);
      float cx = scr_x(sx), top = scr_y(sy);
      u32 c = (x + y) % 2 ? a : b;
      fill_triangle(cx, top, cx + HALF_W * z, top + HALF_H * z, cx - HALF_W * z, top + HALF_H * z, c);
      fill_triangle(cx - HALF_W * z, top + HALF_H * z, cx + HALF_W * z, top + HALF_H * z, cx, top + TH * z, c);
    }
}

typedef struct { float depth; int seq; const Item *item; const FurniPart *part; const Ent *ent; const SceneSprite *layer; } Drawable;

static int by_depth(const void *a, const void *b) {
  const Drawable *x = a, *y = b;
  if (x->depth != y->depth) return x->depth < y->depth ? -1 : 1;
  return x->seq - y->seq;
}

static void draw_item_part(const Item *it, const FurniPart *p) {
  float sx, sy;
  iso(it->x, it->y, &sx, &sy);
  bool add = p->flags & PART_ADD;
  if (add) gfx_additive(true);
  gfx_furni(it->def, p, scr_x(sx + p->ox), scr_y(sy + p->oy - it->z), V.zoom, rgba(0xffffff, p->alpha));
  if (add) gfx_additive(false);
}

static void draw_ent(const Ent *e, u64 now) {
  float px, py, sx, sy;
  ent_pos(e, now, &px, &py);
  iso(px, py, &sx, &sy);
  bool moving = ent_moving(e, now);
  PoseInfo p = pose_of(e, now, moving);
  float h = ent_height(e, now);
  if (p.pose != POSE_LAY)
    fill_circle(scr_x(roundf(sx)), scr_y(roundf(sy + HALF_H - h)), 12 * V.zoom, rgba(0x000000, 64));
  bool talking = now < e->talk_until && (now / 140) % 2 == 0;
  bool blink = (now + e->id * 1777) % 5200 < 130;
  int drink = now < e->drink_until ? e->drink : 0;
  bool sip = !moving && (now + e->id * 177) % 6000 < 900;
  u64 look = e->look | ((e->flags & 128) ? AV_FRANK : 0);
  avatar_draw(look, body_dir(e, moving), p.pose, p.frame, (talking ? EXPR_TALK : 0) | (blink ? EXPR_BLINK : 0),
              head_dir(e, now, moving), drink, sip,
              scr_x(roundf(sx) + p.dx - ANCHOR_X), scr_y(roundf(sy + HALF_H) + p.dy - p.anchor_y - p.lift), V.zoom, 255);
  /* Badge of the native device above the head (devices come from the S_DEVICE message). */
  int device = e->device ? e->device : ((e->flags & FLAG_3DS) ? DEVICE_3DS : 0);
  if (device) {
    int bw = 12, bh = 13;
    device_badge_size(device, &bw, &bh);
    float k = V.zoom < 1 ? 1 : V.zoom;
    float bx = scr_x(sx + p.dx), by = scr_y(sy + HALF_H + p.dy - p.anchor_y + 16 - p.lift);
    device_badge(device, bx - bw * 0.5f * k, by - bh * k, k);
  }
}

static void draw_bubbles(u64 now) {
  float dt = V.last_frame ? fminf(100, (float)(now - V.last_frame)) : 0;
  bool any = false;
  for (int i = 0; i < BUBBLE_MAX; i++) any |= V.chat[i].used;
  if (any && now - V.last_shift > BUBBLE_SCROLL_MS) {
    for (int i = 0; i < BUBBLE_MAX; i++) V.chat[i].row++;
    V.last_shift = now;
  }
  for (int i = 0; i < BUBBLE_MAX; i++) {
    Bubble *b = &V.chat[i];
    if (!b->used) continue;
    float target = BUBBLE_BASE - b->row * BUBBLE_ROW;
    b->y = isnan(b->y) ? target : fmaxf(target, b->y - dt * BUBBLE_SPEED);
    if (b->y < -BUBBLE_H || b->row >= BUBBLE_MAX) { b->used = false; continue; }
    float cx = scr_x(b->wx);
    float t = (now - b->t0) / 220.0f;
    if (t < 1) {
      float hy = scr_y(b->head_wy);
      fill_circle(cx, hy + (b->y - hy) * t, 3.5f, rgba(b->color, 255));
    }
    char name[32], text[112];
    snprintf(name, sizeof name, "%s: ", b->name);
    snprintf(text, sizeof text, "%s", b->text);
    float nw = text_width(name, BUBBLE_TEXT) + 1;
    text_fit(text, BUBBLE_TEXT, SCREEN_W - 30 - nw);
    float w = nw + text_width(text, BUBBLE_TEXT) + 18;
    float left = roundf(fmaxf(2, fminf(SCREEN_W - w - 2, cx - w / 2)));
    float top = roundf(b->y - BUBBLE_H);
    fill_round(left, top, w, BUBBLE_H, BUBBLE_H / 2.0f, rgba(b->color, 255));
    fill_round(left + 2, top + 2, w - 4, BUBBLE_H - 4, (BUBBLE_H - 4) / 2.0f, rgb(0xffffff, 255));
    float ty = top + 3;
    text_draw(name, left + 9, ty, BUBBLE_TEXT, rgb(0x000000, 255), 0);
    text_draw(name, left + 9.6f, ty, BUBBLE_TEXT, rgb(0x000000, 255), 0);
    text_draw(text, left + 9 + nw, ty, BUBBLE_TEXT, rgb(0x000000, 255), 0);
  }
}

void show_chat(u32 eid, const char *text, u64 now) {
  Ent *e = world_ent(eid);
  if (!e) return;
  float px, py, wx, wy;
  ent_pos(e, now, &px, &py);
  iso(px, py, &wx, &wy);
  u64 talk = 500 + strlen(text) * 70;
  e->talk_until = now + (talk > 3500 ? 3500 : talk);
  u32 c = look_shirt_rgb(e->look);
  if ((c >> 16 & 255) + (c >> 8 & 255) + (c & 255) >= 600)
    c = (u32)((c >> 16 & 255) * .9f) << 16 | (u32)((c >> 8 & 255) * .9f) << 8 | (u32)((c & 255) * .9f);
  int slot = -1;
  for (int i = 0; i < BUBBLE_MAX; i++) {
    if (!V.chat[i].used) { if (slot < 0) slot = i; continue; }
    V.chat[i].row++;
  }
  if (slot < 0) for (int i = 0; i < BUBBLE_MAX; i++) if (slot < 0 || V.chat[i].row > V.chat[slot].row) slot = i;
  Bubble *b = &V.chat[slot];
  memset(b, 0, sizeof *b);
  b->used = true;
  snprintf(b->name, sizeof b->name, "%s", e->name);
  snprintf(b->text, sizeof b->text, "%s", text);
  b->color = c;
  b->wx = wx;
  b->head_wy = wy + HALF_H - 70 - ent_height(e, now);
  b->y = NAN;
  b->t0 = now;
  V.last_shift = now;
}

void show_room_changed(u64 now) {
  static char last_slug[sizeof W.slug];
  if (strcmp(last_slug, W.slug)) memset(V.chat, 0, sizeof V.chat);
  snprintf(last_slug, sizeof last_slug, "%s", W.slug);
  V.scene = gfx_scene(W.model);
  V.bg = V.scene ? NULL : gfx_room(W.model);
  if (V.scene) gfx_room_release();
  if (!V.bg && !V.scene) {
    for (int i = 0; i < room_background_count && !V.bg; i++) {
      const RoomBackground *bg = &room_backgrounds[i];
      if (bg->door_x != W.door_x || bg->door_y != W.door_y) continue;
      bool same = true;
      const char *p = bg->rows;
      for (int y = 0; y < W.h && same; y++) {
        for (int x = 0; x < W.w; x++, p++) if (!*p || *p == '\n' || ((*p != 'x') != world_tile(x, y))) { same = false; break; }
        if (*p == '\n') p++; else if (*p) same = false;
      }
      if (same) V.bg = gfx_room(bg->model);
    }
  }
  if (V.scene) {
    for (int i = 0; i < V.scene->count; i++) {
      const SceneSprite *sp = &V.scene->sprites[i];
      if (!sp->background) continue;
      V.bx0 = sp->x; V.by0 = sp->y; V.bx1 = sp->x + sp->dw; V.by1 = sp->y + sp->dh;
    }
  } else if (V.bg) {
    V.bx0 = V.bg->x; V.by0 = V.bg->y; V.bx1 = V.bg->x + V.bg->w; V.by1 = V.bg->y + V.bg->h;
  } else {
    V.bx0 = V.by0 = 1e9f; V.bx1 = V.by1 = -1e9f;
    for (int y = 0; y < W.h; y++)
      for (int x = 0; x < W.w; x++) {
        if (!world_tile(x, y)) continue;
        float sx, sy;
        iso(x, y, &sx, &sy);
        V.bx0 = fminf(V.bx0, sx - HALF_W - 14); V.bx1 = fmaxf(V.bx1, sx + HALF_W + 14);
        V.by0 = fminf(V.by0, sy - 130); V.by1 = fmaxf(V.by1, sy + TH + 10);
      }
  }
  for (int i = 0; i < MAX_ITEMS; i++) if (W.items[i].used) gfx_furni_preload(W.items[i].def);
  V.snapped = false;
  V.joined_at = now;
}

// ------------------------------------------------------------------ interface

/** First interactive furniture next to the avatar (for the "Press A" prompt). */
static const Item *nearby_furniture(void) {
  Ent *me = world_ent(W.me);
  if (!me) return NULL;
  const Item *best = NULL;
  for (int i = 0; i < MAX_ITEMS; i++) {
    const Item *it = &W.items[i];
    if (!it->used) continue;
    int dx = (int)it->x - me->x, dy = (int)it->y - me->y;
    if (dx * dx + dy * dy > 2) continue;
    const FurniDef *d = def_get(it->def);
    if (!d || d->kind == KIND_BLOCK) continue;
    best = it;
    if (dx == 0 && dy == 0) break;
  }
  return best;
}

static const char *prompt_text(const Item *it) {
  const FurniDef *d = def_get(it->def);
  if (!d) return "Use";
  switch (d->kind) {
    case KIND_SIT: return "Sit";
    case KIND_FLOOR: return "Lie down";
    case KIND_TOGGLE: return "Switch on/off";
    case KIND_DICE: return "Roll the dice";
    default: return "Use";
  }
}

static void draw_ui(u64 now) {
  /* Short notice pill under the banner. */
  if (toast[0] && now < toast_until) {
    char t[110];
    snprintf(t, sizeof t, "%s", toast);
    text_fit(t, 11, SCREEN_W - 40);
    float tw = text_width(t, 11);
    fill_round(SCREEN_W / 2 - tw / 2 - 8, 44, tw + 16, 18, 9, rgba(0x26373a, 220));
    text_draw(t, SCREEN_W / 2, 47, 11, rgb(0xeceae2, 255), 1);
  }
  /* Showcase banner: gold over the classic chrome. */
  char line[80];
  snprintf(line, sizeof line, "TWEEBBO SHOWCASE - %s", V.platform);
  float tw = text_width(line, 13);
  fill_round(SCREEN_W / 2 - tw / 2 - 8, 6, tw + 16, 20, 4, rgba(0x0b0d0e, 200));
  text_draw(line, SCREEN_W / 2, 9, 13, rgb(0xffd166, 255), 1);
  snprintf(line, sizeof line, "Model: %s", CAST_NAME[V.cast]);
  text_draw(line, SCREEN_W / 2, 28, 11, rgb(0x8fd9ff, 255), 1);

  /* Classic bottom bar. */
  float bar_y = SCREEN_H - 34;
  fill_rect(0, bar_y, SCREEN_W, 34, rgba(0x0b0d0e, 220));
  fill_rect(0, bar_y, SCREEN_W, 1, rgb(0x526d76, 255));
  const char *labels[] = {"Me", "Rooms", "Shop", "Hand", "Friends", "Chat", "Help"};
  for (int i = 0; i < COUNT(labels); i++) {
    float w = SCREEN_W / (float)COUNT(labels);
    float x = i * w;
    fill_rect(x + 4, bar_y + 5, w - 8, 23, rgb(0x232a30, 255));
    fill_rect(x + 5, bar_y + 6, w - 10, 21, rgb(0x33475f, 255));
    text_draw(labels[i], x + w / 2, bar_y + 12, 11, rgb(0xffffff, 255), 1);
  }
  /* Room, presence and credits. */
  snprintf(line, sizeof line, "%s", W.in_room ? W.title : net_status());
  fill_round(8, bar_y - 24, text_width(line, 11) + 14, 18, 4, rgba(0x0b0d0e, 190));
  text_draw(line, 15, bar_y - 21, 11, rgb(0xeceae2, 255), 0);
  if (W.in_room) {
    snprintf(line, sizeof line, "%d online", W.online);
    fill_round(SCREEN_W - 190, bar_y - 24, 82, 18, 4, rgba(0x0b0d0e, 190));
    text_draw(line, SCREEN_W - 176, bar_y - 21, 11, rgb(0x8fd9ff, 255), 0);
    snprintf(line, sizeof line, "%u credits", W.credits);
    fill_round(SCREEN_W - 104, bar_y - 24, 96, 18, 4, rgba(0x0b0d0e, 190));
    text_draw(line, SCREEN_W - 90, bar_y - 21, 11, rgb(0xffd166, 255), 0);
  }

  /* Press-A prompt of the nearest interactive furniture or player. */
  const Item *near = W.in_room ? nearby_furniture() : NULL;
  if (near) {
    snprintf(line, sizeof line, "A - %s", prompt_text(near));
    float w = text_width(line, 11) + 16;
    fill_round(SCREEN_W / 2 - w / 2, bar_y - 26, w, 20, 10, rgba(0x0b0d0e, 210));
    fill_round(SCREEN_W / 2 - w / 2, bar_y - 26, 20, 20, 10, rgb(0x7fcf55, 255));
    text_draw("A", SCREEN_W / 2 - w / 2 + 10, bar_y - 23, 12, rgb(0x000000, 255), 1);
    text_draw(line + 3, SCREEN_W / 2 - w / 2 + 24, bar_y - 23, 11, rgb(0xffffff, 255), 0);
  }

  /* Classic window with the current character (the "changing models" card). */
  float wx = 10, wy = 60;
  fill_rect(wx, wy, 150, 92, rgba(0x000000, 190));
  fill_rect(wx + 1, wy + 1, 148, 90, rgb(0xdfdfd3, 255));
  fill_rect(wx + 3, wy + 3, 144, 16, rgb(0x526d76, 255));
  text_draw("Me", wx + 8, wy + 5, 11, rgb(0xffffff, 255), 0);
  Ent *me = world_ent(W.me);
  if (me) {
    avatar_draw_head(me->look, 2, wx + 14, wy + 30, 0.7f);
    char name[24];
    snprintf(name, sizeof name, "%s", me->name);
    text_fit(name, 12, 96);
    text_draw(name, wx + 56, wy + 30, 12, rgb(0x11181c, 255), 0);
    text_draw(V.platform, wx + 56, wy + 46, 11, rgb(0x1b5b91, 255), 0);
    text_draw("X wave - B model - Y hello", wx + 8, wy + 76, 10, rgb(0x444444, 255), 0);
  }
}

// ------------------------------------------------------------------ frame

void show_frame(u64 now) {
  showcase_update(now);
  gfx_filter(false);
  if (!W.in_room) {
    NetState state = net_state();
    const char *status = state == NET_FULL ? "Tweebbo is at capacity"
                        : state == NET_LOCKED ? "This room is locked"
                        : state == NET_DENIED ? "Connection paused"
                        : net_status()[0] ? net_status() : "Connecting...";
    text_draw("Tweebbo", SCREEN_W / 2, SCREEN_H / 2 - 34, 16, rgb(0xffd166, 255), 1);
    text_draw(status, SCREEN_W / 2, SCREEN_H / 2 - 10, 13, rgb(0xeceae2, 255), 1);
    text_draw(V.platform, SCREEN_W / 2, SCREEN_H / 2 + 14, 11, rgb(0x8fd9ff, 255), 1);
    return;
  }
  camera(now);
  if (V.scene) {
    for (int i = 0; i < V.scene->count; i++)
      if (V.scene->sprites[i].background) {
        const SceneSprite *sp = &V.scene->sprites[i];
        gfx_scene_sprite(sp, scr_x(sp->x), scr_y(sp->y), V.zoom, sp->alpha);
      }
  } else if (V.bg) gfx_room_draw(V.bg, scr_x(V.bg->x), scr_y(V.bg->y), V.zoom);
  else fallback_floor();

  static Drawable list[4096];
  int n = 0;
  for (int i = 0; i < MAX_ITEMS && n < COUNT(list); i++) {
    const Item *it = &W.items[i];
    if (!it->used) continue;
    const FurniVariant *v = variant_frame(it->def, it->rot, it->state, furni_frame(it->def, it->state, now));
    if (!v) continue;
    for (int k = 0; k < v->count && n < COUNT(list); k++) {
      const FurniPart *p = &v->parts[k];
      float sx, sy;
      iso(it->x, it->y, &sx, &sy);
      float left = scr_x(sx + p->ox), top = scr_y(sy + p->oy - it->z);
      if (left >= SCREEN_W || top >= SCREEN_H || left + p->w * V.zoom <= 0 || top + p->h * V.zoom <= 0) continue;
      bool shadow = p->flags & PART_SHADOW;
      if (shadow && it->z % 32) continue;
      list[n] = (Drawable){shadow ? -10000 : it->x + it->y + p->depth + it->z / 1024.0f, n, it, p, NULL, NULL};
      n++;
    }
  }
  if (V.scene) {
    for (int i = 0; i < V.scene->count && n < COUNT(list); i++) {
      const SceneSprite *sp = &V.scene->sprites[i];
      if (sp->background) continue;
      if (sp->frames > 1 && sp->frame != (now / 120) % sp->frames) continue;
      list[n] = (Drawable){sp->depth, n, NULL, NULL, NULL, sp};
      n++;
    }
  }
  for (int i = 0; i < MAX_ENTS && n < COUNT(list); i++) {
    const Ent *e = &W.ents[i];
    if (!e->used) continue;
    float px, py;
    ent_pos(e, now, &px, &py);
    PoseInfo p = pose_of(e, now, ent_moving(e, now));
    list[n] = (Drawable){px + py + .002f + p.depth + ent_height(e, now) / 1024, n, NULL, NULL, e, NULL};
    n++;
  }
  qsort(list, (size_t)n, sizeof *list, by_depth);
  for (int i = 0; i < n; i++) {
    if (list[i].ent) draw_ent(list[i].ent, now);
    else if (list[i].layer) gfx_scene_sprite(list[i].layer, scr_x(list[i].layer->x), scr_y(list[i].layer->y), V.zoom, list[i].layer->alpha);
    else draw_item_part(list[i].item, list[i].part);
  }
  draw_bubbles(now);
  draw_ui(now);
  V.last_frame = now;
}

// ------------------------------------------------------------------ input

void show_walk(int dx, int dy) {
  Ent *me = world_ent(W.me);
  if (!me || !W.in_room || (dx == 0 && dy == 0)) return;
  int x = me->x + dx, y = me->y + dy;
  if (x >= 0 && y >= 0 && x < W.w && y < W.h && world_tile(x, y)) send_walk(x, y);
}

void show_tap(float x, float y) {
  if (!W.in_room) return;
  float sx = x / V.zoom - roundf(V.cam_x), sy = y / V.zoom - roundf(V.cam_y);
  int tx = (int)lroundf((sx / HALF_W + sy / HALF_H) / 2);
  int ty = (int)lroundf((sy / HALF_H - sx / HALF_W) / 2);
  if (tx >= 0 && ty >= 0 && tx < W.w && ty < W.h && world_tile(tx, ty)) send_walk(tx, ty);
}

// ------------------------------------------------------------------ app callbacks
// world.c delivers events through these; the platform main only needs the loop.

void show_toast(const char *text, u64 now) {
  snprintf(toast, sizeof toast, "%s", text ? text : "");
  toast_until = now + 6000;
}

void on_room(void) { show_room_changed(now_ms()); }
void on_chat(u32 eid, const char *text) { show_chat(eid, text, now_ms()); }
void on_notice(const char *text) { show_toast(text, now_ms()); }
void on_kicked(const char *text) { show_toast(text, now_ms()); net_goto("lobby", ""); }
void on_transfer(const char *slug, const char *arrival) { net_goto(slug, arrival); }
void on_alert(const char *json) { (void)json; }
void on_casino(const char *json) { (void)json; }
void on_snow(const u8 *data, int len) { (void)data; (void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 eid) { (void)eid; }
void on_item(u32 id) { (void)id; }

/* Offline profile for the local simulation (sim.c). */
void profile_load(char *name, int cap, u64 *look, u32 *credits) {
  name[0] = 0; *look = 0; *credits = 200;
  char line[128];
  if (!store_load("offline-profile", line, sizeof line)) return;
  char *save = NULL;
  for (char *t = strtok_r(line, ";", &save); t; t = strtok_r(NULL, ";", &save)) {
    if (!strncmp(t, "name=", 5)) snprintf(name, (size_t)cap, "%s", t + 5);
    else if (!strncmp(t, "look=", 5)) *look = strtoull(t + 5, NULL, 10);
    else if (!strncmp(t, "credits=", 8)) *credits = (u32)strtoul(t + 8, NULL, 10);
  }
}

void profile_save(const char *name, u64 look, u32 credits) {
  char line[192];
  snprintf(line, sizeof line, "name=%s;look=%llu;credits=%u", name, (unsigned long long)look, (unsigned)credits);
  store_save("offline-profile", line);
}

void show_action(int action) {
  switch (action) {
    case SHOW_A: {
      const Item *near = nearby_furniture();
      if (near) {
        const FurniDef *d = def_get(near->def);
        if (d && (d->kind == KIND_SIT || d->kind == KIND_FLOOR)) send_gesture(GESTURE_SIT);
        else send_use(near->id);
      } else {
        Ent *me = world_ent(W.me);
        send_gesture((me && (me->flags & FLAG_SIT)) ? 0 : GESTURE_WAVE);
      }
      break;
    }
    case SHOW_B:
      V.cast = (V.cast + 1) % COUNT(CAST);
      V.next_look = now_ms() + LOOK_MS;
      send_look(look_encode(&CAST[V.cast]));
      break;
    case SHOW_X: send_gesture(GESTURE_WAVE); break;
    case SHOW_Y: send_chat(V.hello); break;
    case SHOW_L: V.zoom = 1; V.snapped = false; break;
    case SHOW_R: V.zoom = 2; V.snapped = false; break;
    case SHOW_OFFLINE:
      net_set_server(strcmp(net_server(), "offline") ? "offline" : PROD_SERVER);
      break;
  }
}
