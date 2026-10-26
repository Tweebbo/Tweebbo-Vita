#include "fastfood.h"
#include "battle.h"
// Shared room renderer for the Switch and Vita clients.
#include "show.h"
#include "igloo.h"
#include "client_ui.h"
#include "feedback.h"
#include "client_snow.h"
#include "client_pool.h"
#include "client_bar.h"
#include "client_music.h"
#include "client_camera.h"
#include "client_shop.h"
#include "client_social.h"
#include "client_pets.h"
#include "client_trade.h"
#include "client_casino.h"
#include "pets.h"
#include <math.h>
#include <jansson.h>
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
  const char *platform, *hello, *offline_key;
  /* "Play offline" pill of the connection screen (touch target), in canvas pixels. */
  float pill_x, pill_y, pill_w, pill_h;
  bool quit;
  /* One-shot checkpoints: the log pins the stage that faults the GPU. */
  bool log_room_info, log_stage_world, log_stage_frame;
/* Zoom 1 on the half-resolution canvas is the classic 64×32 tile shown at 2× on screen:
   the whole lobby and everyone in it fit, which is what the cross-play photo needs.
   R zooms in to 2 for close-ups. */
} V = {.zoom = 1, .platform = "Console", .hello = "Hello from the hotel!", .offline_key = "Select"};

/* Short notice pill shown under the banner (hotel notices, kicks). */
static char toast[128];
static u64 toast_until;

static void iso(float x, float y, float *sx, float *sy) {
  *sx = (x - y) * HALF_W;
  *sy = (x + y) * HALF_H;
}

static float scr_x(float wx) { return roundf(V.cam_x * V.zoom) + wx * V.zoom; }
static float scr_y(float wy) { return roundf(V.cam_y * V.zoom) + wy * V.zoom; }

bool show_confirm_quit(const char *key, u64 now) {
  static u64 asked;
  if (asked && now - asked < 3000) return true;
  asked = now;
  char text[64];
  snprintf(text, sizeof text, "Press %s again to exit Tweebbo.", key);
  show_toast(text, now);
  return false;
}

const char *show_platform(void) { return V.platform ? V.platform : "this console"; }

void show_set_platform(const char *name, const char *hello) {
  V.platform = name;
  V.hello = hello;
}

void show_set_offline_key(const char *key) { V.offline_key = key; }

bool show_quit_requested(void) { return V.quit; }

void show_start(u64 now) {
  (void)now;
  client_ui_init();
}

// ------------------------------------------------------------------ camera

static void camera(u64 now) {
  float lw = SCREEN_W / V.zoom, tx, ty;
  float left = client_ui_room_left() / V.zoom;
  float visible = lw - left;
  Ent *me = world_ent(W.me);
  const BuildGhost *ghost=client_shop_ghost();
  if(ghost->on) {
    float sx,sy;iso(ghost->x,ghost->y,&sx,&sy);
    tx=left+visible*.5f-sx;ty=SCREEN_H/V.zoom*.60f-sy+ghost->z;
    V.cam_x+=(tx-V.cam_x)*.12f;V.cam_y+=(ty-V.cam_y)*.12f;return;
  }
  float activity_x,activity_y;
  if(pool_camera(&activity_x,&activity_y,now)){
    tx=left+visible/2-activity_x;ty=SCREEN_H/V.zoom*.5f-activity_y;
    V.cam_x+=(tx-V.cam_x)*.12f;V.cam_y+=(ty-V.cam_y)*.12f;return;
  }
  if (!me) {
    tx = left + visible / 2 - (V.bx0 + V.bx1) / 2;
    ty = lw * .5f / (SCREEN_W / (float)SCREEN_H) - (V.by0 + V.by1) / 2;
  } else {
    float px, py, sx, sy;
    ent_pos(me, now, &px, &py);
    iso(px, py, &sx, &sy);
    tx = left + visible * .5f - sx;
    ty = (SCREEN_H / V.zoom) * .60f - sy + ent_height(me, now);
    if (V.bx1 - V.bx0 >= visible) tx = fminf(left - V.bx0, fmaxf(lw - V.bx1, tx));
    else tx = left + visible / 2 - (V.bx0 + V.bx1) / 2;
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

/** An expression from the server's ACTION is shown until its time runs out. */
static bool expression_on(const Ent *e, u64 now) { return e->action && (!e->action_until || now < e->action_until); }

/** Sit height of a seat drawn in this public room's image (pack.py DRAWN_SEAT_ROOMS), or 0. */
static int public_seat_height(int x, int y) {
  for (int i = 0; i < public_seat_count; i++)
    if (public_seats[i].x == x && public_seats[i].y == y && !strcmp(public_seats[i].room, W.model)) return public_seats[i].height;
  return 0;
}

static PoseInfo pose_of(const Ent *e, u64 now, bool moving) {
  PoseInfo p = {POSE_STD, 0, ent_height(e, now), 0, 0, 0, ANCHOR_Y};
  if ((e->flags & FLAG_LAY) && (!moving || (e->flags & FLAG_ROLL))) {
    Item *bed = world_item_at(e->x, e->y);
    const FurniDef *d = bed ? def_get(bed->def) : NULL;
    p.pose = POSE_LAY;
    p.lift += (d ? d->seat : 32) - 32;
    p.dx = e->dir == 0 ? -47 : 10;
    p.dy = e->dir == 0 ? 32 : 30;
    p.depth = 2;
    p.anchor_y = 62;
    return p;
  }
  if ((e->flags & FLAG_SIT) && (!moving || (e->flags & FLAG_ROLL))) {
    Item *seat = world_item_at(e->x, e->y);
    const FurniDef *d = seat ? def_get(seat->def) : NULL;
    if (d && d->kind == KIND_SIT) { p.pose = POSE_SIT; p.lift = d->seat - SEATED_HIP + ent_height(e, now); }
    else if (!seat && public_seat_height(e->x, e->y) > 0) {
      // Seats drawn in the lobby image: the sitter goes between the seat back and its near arm.
      p.pose = POSE_SIT; p.lift = public_seat_height(e->x, e->y) - SEATED_HIP + ent_height(e, now); p.depth = .6f;
    }
    else p.pose = POSE_FLOOR;
    return p;
  }
  if (moving) { p.pose = POSE_WALK; p.frame = (int)(now / 125) & 3; }
  else if (e->action == 4 && expression_on(e, now)) { p.pose = POSE_KISS; p.frame = (int)(now / 220) & 1; }
  else if (e->action == 5 && expression_on(e, now)) { p.pose = POSE_LAUGH; p.frame = (int)(now / 160) & 3; p.dy = (now / 70) % 2 ? 1 : 0; }
  else if (e->action == 7 && expression_on(e, now)) p.pose = POSE_THUMB;
  else if (e->action == 8 && expression_on(e, now)) p.pose = POSE_SIGN;
  else if (e->flags & FLAG_WAVE) { p.pose = POSE_WAVE; p.frame = (int)(now / 220) & 1; }
  else if (e->flags & FLAG_DANCE) {
    p.pose = POSE_DANCE;
    p.frame = (int)(now / 160) & 7;
    if (e->dance >= 1 && e->dance <= 4) p.frame += DANCE_STYLE_STEP * (e->dance - 1);
  }
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

void show_device_badge(int device, float x, float y, float k) { device_badge(device, x, y, k); }

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

typedef struct { float depth; int seq; const Item *item; const FurniPart *part; const Ent *ent; const SceneSprite *layer; const Pet *pet; } Drawable;

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

/** The character skin a player wears (owner snapshot "skins"), 0 for none. */
static int character_skin(u32 guest) {
  static int rev = -1;
  static json_t *skins;
  if (rev != (int)W.owners_rev) {
    rev = (int)W.owners_rev;
    if (skins) json_decref(skins);
    json_t *owners = W.owners_json ? json_loads(W.owners_json, 0, NULL) : NULL;
    skins = owners ? json_incref(json_object_get(owners, "skins")) : NULL;
    if (owners) json_decref(owners);
  }
  size_t i; json_t *entry;
  json_array_foreach(skins, i, entry) {
    int skin = (int)json_integer_value(json_object_get(entry, "skin"));
    if ((u32)json_integer_value(json_object_get(entry, "guest")) == guest && skin >= 1 && skin <= 7) return skin;
  }
  return 0;
}

static void draw_ent(const Ent *e, u64 now) {
  float px, py, sx, sy;
  ent_pos(e, now, &px, &py);
  iso(px, py, &sx, &sy);
  bool moving = ent_moving(e, now);
  PoseInfo p = pose_of(e, now, moving);
  float h = ent_height(e, now);
  if (igloo_draw(e, body_dir(e, moving), p.pose, now, scr_x(roundf(sx)), scr_y(roundf(sy + HALF_H) - h), V.zoom)) return;
  if (p.pose != POSE_LAY)
    fill_circle(scr_x(roundf(sx)), scr_y(roundf(sy + HALF_H - h)), 12 * V.zoom, rgba(0x000000, 64));
  bool talking = now < e->talk_until && (now / 140) % 2 == 0;
  // Sleep (action 6) keeps the eyes closed.
  bool blink = (now + e->id * 1777) % 5200 < 130 || (e->action == 6 && expression_on(e, now));
  int drink = now < e->drink_until ? e->drink : 0;
  bool sip = !moving && (now + e->id * 177) % 6000 < 900;
  int bar_pose=client_bar_sip(e,now,moving);if(bar_pose>=0)sip=bar_pose!=0;
  int skin = character_skin(e->guest);
  u64 look = skin ? (e->look | ((u64)skin << 48)) : pool_look(e,e->look) | ((e->flags & 128) ? AV_FRANK : 0);
  if(V.scene && V.scene->water>=0 && e->x<W.w && e->y<W.h && W.rows[e->y*W.w+e->x]>='0' && W.rows[e->y*W.w+e->x]<=(char)('0'+V.scene->water)){
    avatar_draw_swim(look,body_dir(e,moving),moving?POSE_SWIM:POSE_FLOAT,(now/160)&3,(talking?EXPR_TALK:0)|(blink?EXPR_BLINK:0),head_dir(e,now,moving),scr_x(roundf(sx)-SWIM_ANCHOR_X),scr_y(roundf(sy+HALF_H)-ANCHOR_Y),V.zoom);return;
  }
  int upper = (p.pose == POSE_SIT || p.pose == POSE_FLOOR)
    ? avatar_seated_expr(expression_on(e, now) ? e->action : 0, (e->flags & FLAG_WAVE) != 0) : 0;
  if (upper) p.frame = (int)(now / (e->action == 5 ? 160 : 220)) & 3;
  avatar_draw(look, body_dir(e, moving), p.pose, p.frame, (talking ? EXPR_TALK : 0) | (blink ? EXPR_BLINK : 0) | upper,
              head_dir(e, now, moving), drink, sip,
              scr_x(roundf(sx) + p.dx - ANCHOR_X), scr_y(roundf(sy + HALF_H) + p.dy - p.anchor_y - p.lift), V.zoom, 255);
}

static void draw_ent_badge(const Ent *e, u64 now) {
  float px, py, sx, sy;
  ent_pos(e, now, &px, &py);
  iso(px, py, &sx, &sy);
  PoseInfo p = pose_of(e, now, ent_moving(e, now));
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

static void bubble_add(u32 eid, const char *text, u64 now) {
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

/** Long messages become several bubbles (one per line that fits), so nothing is cut off. */
void show_chat(u32 eid, const char *text, u64 now) {
  Ent *e = world_ent(eid);
  if (!e) return;
  char prefix[32];
  snprintf(prefix, sizeof prefix, "%s: ", e->name);
  float room = SCREEN_W - 30 - (text_width(prefix, BUBBLE_TEXT) + 1);
  const char *p = text;
  while (*p == ' ') p++;
  for (int chunks = 0; *p && chunks < BUBBLE_MAX; chunks++) {
    char line[104];
    size_t used = 0, cut = 0;
    const char *q = p;
    while (*q) {
      size_t n = 1;
      while (((unsigned char)q[n] & 0xc0) == 0x80) n++;
      if (used + n >= sizeof line) break;
      memcpy(line + used, q, n); line[used + n] = 0;
      if (used && text_width(line, BUBBLE_TEXT) > room) break;
      used += n; q += n;
      if (*q == ' ') cut = used;
    }
    // Break at the last space when the line is full and a word would be split.
    if (*q && cut > 0) { used = cut; q = p + cut; }
    line[used] = 0;
    bubble_add(eid, line, now);
    p = q;
    while (*p == ' ') p++;
  }
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
  V.log_room_info = V.log_stage_world = V.log_stage_frame = true;
}

// ------------------------------------------------------------------ interface

/** First interactive furniture next to the avatar (for the "Press A" prompt). */
static bool interactive(const FurniDef *d) {
  if (!d || d->kind == KIND_WALL) return false;
  return d->kind == KIND_SIT || d->kind == KIND_TOGGLE || d->kind == KIND_DICE || d->casino ||
         (d->flags & (DEF_LAY | DEF_TELEPORTER | DEF_GATE)) || (d->drinks && *d->drinks);
}
static const Item *nearby_furniture(void) {
  Ent *me = world_ent(W.me);
  if (!me) return NULL;
  const Item *best = NULL;
  int best_d = 4;
  for (int i = 0; i < MAX_ITEMS; i++) {
    const Item *it = &W.items[i];
    if (!it->used) continue;
    int dx = (int)it->x - me->x, dy = (int)it->y - me->y, d2 = dx * dx + dy * dy;
    if (d2 > 2 || d2 >= best_d) continue;
    const FurniDef *d = def_get(it->def);
    if (!interactive(d)) continue;
    best = it; best_d = d2;
    if (d2 == 0) break;
  }
  return best;
}
/** Human verb for the contextual prompt, like the 3DS player's card. */
static void interaction_label(const Item *it, const FurniDef *d, char *out, size_t cap) {
  const char *name = d && d->name && *d->name ? d->name : "furniture";
  if (d->casino) snprintf(out, cap, "Play %s", name);
  else if (d->drinks && *d->drinks) snprintf(out, cap, "Get a drink");
  else if (d->kind == KIND_DICE) snprintf(out, cap, "Roll the dice");
  else if (d->flags & DEF_TELEPORTER) snprintf(out, cap, "Enter the teleporter");
  else if (d->flags & DEF_GATE) snprintf(out, cap, it->state ? "Close the gate" : "Open the gate");
  else if (d->kind == KIND_TOGGLE) snprintf(out, cap, it->state ? "Switch off %s" : "Switch on %s", name);
  else if (d->flags & DEF_LAY) snprintf(out, cap, "Lie down");
  else if (d->kind == KIND_SIT) snprintf(out, cap, "Sit on %s", name);
  else snprintf(out, cap, "Use %s", name);
}


static void draw_ui(u64 now) {
  client_ui_draw();
  if (toast[0] && now < toast_until) {
    char t[128];
    snprintf(t, sizeof t, "%s", toast);
    text_fit(t, 11, SCREEN_W - 40);
    float tw = text_width(t, 11);
    // Classic floating notice under the top edge (original content inset, blue rim).
    client_ui_panel(SCREEN_W / 2 - tw / 2 - 11, 6, tw + 22, 21);
    text_draw(t, SCREEN_W / 2, 10, 11, rgb(0x18272a, 255), 1);
  }
  // Contextual "Ⓐ Use <furniture>" prompt beside the room, like the console editions.
  if (W.in_room && !client_ui_room_left()) {
    const Item *it = nearby_furniture();
    if (it) {
      const FurniDef *d = def_get(it->def);
      char use[96];
      interaction_label(it, d, use, sizeof use);
      text_fit(use, 10, SCREEN_W - 130);
#if TWEEBBO_DEVICE == 3
      const char *key = "Cross";
#else
      const char *key = "A";
#endif
      client_ui_prompt(key, use, SCREEN_W - 10, SCREEN_H - 58);
    }
  }
}

// ------------------------------------------------------------------ frame

void show_frame(u64 now) {
  gfx_filter(false);
  if(fastfood_active()){fastfood_draw(0,0,SCREEN_W,SCREEN_H);client_ui_draw();return;}
  if(battle_active()){battle_draw(SCREEN_W,SCREEN_H,now);client_ui_draw();return;}
  if(client_snow_scene(now)){client_ui_draw();return;}
  if (!W.in_room) {
    NetState state = net_state();
    const char *status = state == NET_FULL ? "Tweebbo is at capacity"
                        : state == NET_LOCKED ? "This room is locked"
                        : state == NET_DENIED ? "Connection paused"
                        : net_status()[0] ? net_status() : "Connecting...";
    float w = SCREEN_W >= 600 ? 340 : 300, h = 150;
    float x = (SCREEN_W - w) / 2, y = SCREEN_H / 2 - h / 2 - 18;
    client_ui_window(x, y, w, h, "Tweebbo");
    // Original white content inset inside the window, like every hotel panel.
    client_ui_panel(x + 8, y + 28, w - 16, h - 56);
    char line[160];
    snprintf(line, sizeof line, "%s", status);
    u32 ink = state == NET_FULL || state == NET_LOCKED || state == NET_DENIED ? 0xc1272d : 0x18272a;
    client_ui_paragraph(line, x + 14, y + 34, w - 28, 2, ink);
    V.pill_w = 0;
    if (state != NET_OFFLINE) {
      snprintf(line, sizeof line, "Hotel: %s", net_server());
      text_fit(line, 10, w - 28);
      text_draw(line, x + 14, y + 70, 10, rgb(0x4d5b61, 255), 0);
      /* No hotel on this Wi-Fi: the same build plays offline (the local simulation with bots).
         Classic capsule, like every other button in the hotel. */
      snprintf(line, sizeof line, "%s - play offline", V.offline_key);
      float bw = text_width(line, 12) + 30, bh = 24;
      float bx = SCREEN_W / 2 - bw / 2, by = y + 92;
      V.pill_x = bx; V.pill_y = by; V.pill_w = bw; V.pill_h = bh;
      client_ui_capsule(bx, by, bw, bh, line);
    }
    text_draw(V.platform, SCREEN_W / 2, y + h - 19, 10, rgb(0x4d5b61, 255), 1);
    client_ui_draw();
    return;
  }
  if(pool_tower_view()){pool_draw_tower(now);client_ui_draw();return;}
  camera(now);
  if (V.scene) {
    for (int pass=0;pass<2;pass++) for (int i = 0; i < V.scene->count; i++)
      if (V.scene->sprites[i].background && (V.scene->sprites[i].role==1)==(pass==0)) {
        const SceneSprite *sp = &V.scene->sprites[i];
        gfx_scene_sprite(sp, scr_x(sp->x), scr_y(sp->y), V.zoom, sp->alpha);
      }
  } else if (V.bg) gfx_room_draw(V.bg, scr_x(V.bg->x), scr_y(V.bg->y), V.zoom);
  else fallback_floor();
  if (V.log_room_info) {
    V.log_room_info = false;
    int items = 0, ents = 0;
    for (int i = 0; i < MAX_ITEMS; i++) items += W.items[i].used;
    for (int i = 0; i < MAX_ENTS; i++) ents += W.ents[i].used;
    dbg_log("room: %s model=%s %dx%d bg=%s items=%d ents=%d", W.slug, W.model, W.w, W.h,
            V.scene ? "scene" : V.bg ? "background" : "fallback-floor", items, ents);
  }

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
      if(sp->role>=10 && ((sp->role&1)!=pool_switch_closed((sp->role-10)/2)))continue;
      if (sp->frames > 1 && sp->frame != (now / 120) % sp->frames) continue;
      list[n] = (Drawable){sp->depth, n, NULL, NULL, NULL, sp};
      n++;
    }
  }
  for (int i = 0; i < MAX_ENTS && n < COUNT(list); i++) {
    const Ent *e = &W.ents[i];
    if (!e->used || pool_hides(e)) continue;
    float px, py;
    ent_pos(e, now, &px, &py);
    PoseInfo p = pose_of(e, now, ent_moving(e, now));
    list[n] = (Drawable){px + py + .002f + p.depth + ent_height(e, now) / 1024, n, NULL, NULL, e, NULL};
    n++;
  }
  for(int i=0;i<pets_count() && n<COUNT(list);i++) {
    const Pet *pet=pet_at(i);float px,py;pet_pos(pet,now,&px,&py);
    list[n]=(Drawable){.depth=px+py+.001f+pet->z/1024.f,.seq=n,.pet=pet};n++;
  }
  qsort(list, (size_t)n, sizeof *list, by_depth);
  for (int i = 0; i < n; i++) {
    if (list[i].ent) draw_ent(list[i].ent, now);
    else if(list[i].pet) {
      float px,py,sx,sy;pet_pos(list[i].pet,now,&px,&py);iso(px,py,&sx,&sy);
      pet_draw(list[i].pet,now,scr_x(sx),scr_y(sy),V.zoom);
    }
    else if (list[i].layer) gfx_scene_sprite(list[i].layer, scr_x(list[i].layer->x), scr_y(list[i].layer->y), V.zoom, list[i].layer->alpha);
    else draw_item_part(list[i].item, list[i].part);
  }
  if (V.log_stage_world) { V.log_stage_world = false; dbg_log("room: world drawn (%d pieces)", n); }
  const BuildGhost *ghost=client_shop_ghost();
  if(ghost->on) {
    float sx,sy;iso(ghost->x,ghost->y,&sx,&sy);
    u32 ink=rgba(ghost->ok?0x4bc780:0xec655b,180);
    int w,h;def_size(ghost->def,ghost->rot,&w,&h);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
      float px,py;iso(ghost->x+x,ghost->y+y,&px,&py);
      float cx=scr_x(px),cy=scr_y(py),hw=HALF_W*V.zoom,hh=HALF_H*V.zoom;
      draw_line(cx-hw,cy,cx,cy-hh,ink);draw_line(cx,cy-hh,cx+hw,cy,ink);
      draw_line(cx+hw,cy,cx,cy+hh,ink);draw_line(cx,cy+hh,cx-hw,cy,ink);
    }
    const FurniVariant *variant=variant_get(ghost->def,ghost->rot,0);
    if(variant)for(int i=0;i<variant->count;i++) {
      const FurniPart *p=&variant->parts[i];
      gfx_furni(ghost->def,p,scr_x(sx+p->ox),scr_y(sy+p->oy-ghost->z),V.zoom,rgba(0xffffff,150));
    }
  }
  if(client_camera_capturing()){client_camera_scene();V.last_frame=now;return;}
  /* Like 3DS, device markers overlay furniture and foreground room layers. */
  for (int i = 0; i < MAX_ENTS; i++)
    if (W.ents[i].used && !pool_hides(&W.ents[i])) draw_ent_badge(&W.ents[i], now);
  pool_draw_room(now);
  draw_bubbles(now);
  draw_ui(now);
  if (V.log_stage_frame) { V.log_stage_frame = false; dbg_log("room: first frame drawn"); }
  V.last_frame = now;
}

// ------------------------------------------------------------------ input

void show_walk(int dx, int dy) {
  if(dx || dy)net_activity();
  if (client_ui_move(dx, dy)) return;
  Ent *me = world_ent(W.me);
  if (!me || !W.in_room || (dx == 0 && dy == 0)) return;
  int x = me->x + dx, y = me->y + dy;
  if (x >= 0 && y >= 0 && x < W.w && y < W.h && world_tile(x, y)) send_walk(x, y);
}

void show_tap(float x, float y) {
  net_activity();
  if (client_ui_tap(x, y)) return;
  if (!W.in_room) {
    if (net_state() != NET_OFFLINE && V.pill_w > 0 && x >= V.pill_x - 6 && x <= V.pill_x + V.pill_w + 6 &&
        y >= V.pill_y - 6 && y <= V.pill_y + V.pill_h + 6)
      show_action(SHOW_OFFLINE);
    return;
  }
  float sx = x / V.zoom - roundf(V.cam_x), sy = y / V.zoom - roundf(V.cam_y);
  int tx = (int)lroundf((sx / HALF_W + sy / HALF_H) / 2);
  int ty = (int)lroundf((sy / HALF_H - sx / HALF_W) / 2);
  if(client_shop_building()) {client_shop_tile(tx,ty);return;}
  for(int i=0;i<MAX_ENTS;i++) {
    const Ent *e=&W.ents[i];
    if(!e->used || !feedback_receptionist(e->guest,W.model))continue;
    float ex,ey;iso(e->x,e->y,&ex,&ey);
    ex=scr_x(ex);ey=scr_y(ey+HALF_H-ent_height(e,now_ms()));
    if(x>=ex-22*V.zoom && x<=ex+22*V.zoom && y>=ey-90*V.zoom && y<=ey-15*V.zoom){client_ui_person(e->id);return;}
  }
  if (tx >= 0 && ty >= 0 && tx < W.w && ty < W.h && world_tile(tx, ty)) send_walk(tx, ty);
}

// ------------------------------------------------------------------ app callbacks
// world.c delivers events through these; the platform main only needs the loop.

void show_toast(const char *text, u64 now) {
  snprintf(toast, sizeof toast, "%s", text ? text : "");
  toast_until = now + 6000;
}

void on_room(void) { show_room_changed(now_ms()); client_ui_room(); }
void on_chat(u32 eid, const char *text) {
  Ent *e=world_ent(eid);if(e && client_social_blocked(e->guest))return;
  show_chat(eid, text, now_ms()); client_ui_chat(eid, text);
}
void on_notice(const char *text) {client_shop_notice(text);client_pets_notice(text);client_trade_notice(text); show_toast(text, now_ms()); }
void on_kicked(const char *text) { show_toast(text, now_ms()); net_goto("lobby", ""); }
void on_transfer(const char *slug, const char *arrival) { net_goto(slug, arrival); }
void on_alert(const char *json) { client_ui_alert(json); }
void on_casino(const char *json) {client_casino_receive(json);}
void on_snow(const u8 *data, int len) { client_snow_receive(data,len); }
void on_social_changed(void) {client_social_push();}
void on_name_changed(u32 eid) { (void)eid; }
void on_item(u32 id) {client_shop_item(id);}

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
  net_activity();
  if (client_ui_action(action)) return;
  switch (action) {
    case SHOW_A: {
      char context[100];
      if(net_online()&&pool_context(context,sizeof context,now_ms())){pool_context_run(now_ms());break;}
      if(net_online()&&client_bar_context(context,sizeof context,now_ms())){client_ui_bar();break;}
      if(net_online()&&client_music_context(context,sizeof context,now_ms())){client_ui_music();break;}
      const Item *near = nearby_furniture();
      const FurniDef *nd = near ? def_get(near->def) : NULL;
      bool direct = net_online() && nd && (nd->kind == KIND_SIT || (nd->flags & (DEF_LAY | DEF_GATE | DEF_TELEPORTER)) || nd->kind == KIND_TOGGLE);
      if (near && direct) send_use_op(near->id, 0);
      else if (near) client_ui_inspect(near->id);
      else {
        send_gesture(GESTURE_WAVE);
      }
      break;
    }
    case SHOW_B:
      break;
    case SHOW_X: send_gesture(GESTURE_WAVE); break;
    case SHOW_Y: send_chat(V.hello); break;
    case SHOW_L: V.zoom = 1; V.snapped = false; break;
    case SHOW_R: V.zoom = 2; V.snapped = false; break;
    case SHOW_OFFLINE: {
      static char hotel[128];
      if (!strcmp(net_server(), "offline")) net_set_server(hotel[0] ? hotel : DEFAULT_SERVER);
      else {
        snprintf(hotel, sizeof hotel, "%s", net_server());
        net_set_server("offline");
      }
      client_ui_reset();
      break;
    }
  }
}

float show_zoom(void){return V.zoom;}
void show_to_screen(float wx,float wy,float *x,float *y){*x=scr_x(wx);*y=scr_y(wy);}
void on_activity(const char *json){battle_receive(json,now_ms());if(!client_bar_receive(json,now_ms()))pool_receive(json,now_ms());}

void on_music(const char *json){client_music_receive(json,now_ms());}
