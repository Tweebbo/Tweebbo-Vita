// Screen-relative pad walking shared by the console clients.
// Pure logic: no platform headers, explicit time and callbacks.
#pragma once
#include "common.h"
#include <math.h>

#define WALK_PAD_PI 3.14159265358979323846f
#define WALK_PAD_45 (WALK_PAD_PI / 4.0f)

// Sector = round(atan2(ax, ay) / 45 deg); the same eight directions as the 3DS Circle Pad.
static inline void walk_pad_dir(int sector, int *dx, int *dy) {
  static const s8 step[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};
  *dx = step[sector & 7][0];
  *dy = step[sector & 7][1];
}

enum { WALK_PAD_UP = 1, WALK_PAD_DOWN = 2, WALK_PAD_LEFT = 4, WALK_PAD_RIGHT = 8 };

typedef struct {
  bool active;      // above the radial deadzone, with hysteresis
  int sector;       // 0..7 while active
  bool sent;        // sent_x/sent_y hold the last C_WALK target
  int sent_x, sent_y;
  u64 last_send;    // ms of the last C_WALK
  u64 released_at;  // ms the stick entered the deadzone; 0 once handled
  bool stop_sent;   // one stop request per release
} WalkPad;

typedef struct { int x, y, next_x, next_y; } WalkPadPos;
typedef bool (*WalkPadPass)(void *ctx, int x, int y, bool goal);
typedef void (*WalkPadSend)(void *ctx, int x, int y);

// 0.30/0.22: wide enough for Circle Pad drift, 0.08 apart so a stick resting on the edge does not chatter.
#define WALK_PAD_ENTER 0.30f
#define WALK_PAD_LEAVE 0.22f
// 150/156: the 3DS circle range, so the D-pad keeps its old bias over the stick.
#define WALK_PAD_BIAS (150.0f / 156.0f)
// One C_WALK per 100 ms at most: MOVE_MS is 500, so this never floods the room.
#define WALK_PAD_SEND_MS 100u
// An unanswered identical target waits for the server, like the old 3DS 350 ms resend.
#define WALK_PAD_RETRY_MS 350u
// 90 ms covers a direction change through the deadzone without letting go of the path.
#define WALK_PAD_GRACE_MS 90u
// 12 degrees past the 22.5-degree sector boundary: the stick must move clearly before a new sector.
#define WALK_PAD_HYST (12.0f * WALK_PAD_PI / 180.0f)

static inline void walk_pad_vector(float ax, float ay, unsigned dpad, float *vx, float *vy) {
  *vx = ax + ((dpad & WALK_PAD_RIGHT) ? WALK_PAD_BIAS : 0.0f) - ((dpad & WALK_PAD_LEFT) ? WALK_PAD_BIAS : 0.0f);
  *vy = ay + ((dpad & WALK_PAD_UP) ? WALK_PAD_BIAS : 0.0f) - ((dpad & WALK_PAD_DOWN) ? WALK_PAD_BIAS : 0.0f);
}

static inline int walk_pad_sector_of(float angle) {
  return ((int)lroundf(angle / WALK_PAD_45) + 8) & 7;
}

static inline bool walk_pad_sector_holds(int sector, float angle) {
  float d = angle - (float)sector * WALK_PAD_45;
  while (d > WALK_PAD_PI) d -= 2.0f * WALK_PAD_PI;
  while (d < -WALK_PAD_PI) d += 2.0f * WALK_PAD_PI;
  return fabsf(d) <= 0.5f * WALK_PAD_45 + WALK_PAD_HYST;
}

// Projection of the stick onto the screen direction of a tile step.
static inline float walk_pad_projection(float ax, float ay, int dx, int dy) {
  float sx = (float)(dx - dy), sy = -(float)(dx + dy) * 0.5f;
  float n = sqrtf(sx * sx + sy * sy);
  return n > 0.0f ? (ax * sx + ay * sy) / n : 0.0f;
}

static inline bool walk_pad_corner(WalkPadPass pass, void *ctx, int x, int y, int dx, int dy) {
  return pass(ctx, x + dx, y, false) && pass(ctx, x, y + dy, false);
}

// Two tiles ahead, then one; a diagonal step is allowed only when both orthogonal neighbours are.
static inline bool walk_pad_ahead(WalkPadPass pass, void *ctx, int x, int y, int dx, int dy, int *tx, int *ty) {
  bool diagonal = dx && dy;
  bool step = pass(ctx, x + dx, y + dy, false);
  if (diagonal) step = step && walk_pad_corner(pass, ctx, x, y, dx, dy);
  bool two = step && pass(ctx, x + 2 * dx, y + 2 * dy, true);
  if (diagonal) two = two && walk_pad_corner(pass, ctx, x + dx, y + dy, dx, dy);
  if (two) { *tx = x + 2 * dx; *ty = y + 2 * dy; return true; }
  bool one = pass(ctx, x + dx, y + dy, true);
  if (diagonal) one = one && walk_pad_corner(pass, ctx, x, y, dx, dy);
  if (one) { *tx = x + dx; *ty = y + dy; return true; }
  return false;
}

static inline bool walk_pad_target(const WalkPadPos *pos, int dx, int dy, float ax, float ay,
                                   WalkPadPass pass, void *ctx, int *tx, int *ty) {
  if (walk_pad_ahead(pass, ctx, pos->x, pos->y, dx, dy, tx, ty)) return true;
  if (!dx || !dy) return false;
  // Blocked diagonal: slide along the axis the stick points at more, then the other.
  int ax1 = dx, ay1 = 0, ax2 = 0, ay2 = dy;
  if (walk_pad_projection(ax, ay, ax2, ay2) > walk_pad_projection(ax, ay, ax1, ay1)) {
    int t = ax1; ax1 = ax2; ax2 = t;
    t = ay1; ay1 = ay2; ay2 = t;
  }
  if (walk_pad_ahead(pass, ctx, pos->x, pos->y, ax1, ay1, tx, ty)) return true;
  return walk_pad_ahead(pass, ctx, pos->x, pos->y, ax2, ay2, tx, ty);
}

// One frame of pad walking. now is the only clock; a zeroed WalkPad is ready to use.
static inline void walk_pad_step(WalkPad *w, float ax, float ay, unsigned dpad, const WalkPadPos *pos, u64 now,
                                 WalkPadPass pass, WalkPadSend send, void *ctx) {
  if (!pass) return;
  float vx, vy;
  walk_pad_vector(ax, ay, dpad, &vx, &vy);
  float r = sqrtf(vx * vx + vy * vy);
  bool was_active = w->active;

  if (w->active) {
    if (r < WALK_PAD_LEAVE) { w->active = false; w->released_at = now; w->stop_sent = false; }
  } else if (r >= WALK_PAD_ENTER) {
    w->active = true; w->released_at = 0; w->stop_sent = false;
  }

  if (!w->active) {
    if (w->released_at && !w->stop_sent && now - w->released_at >= WALK_PAD_GRACE_MS &&
        (!w->sent || now - w->last_send >= WALK_PAD_SEND_MS)) {
      w->stop_sent = true;
      if (pos->next_x != 255 && send) {
        send(ctx, pos->next_x, pos->next_y);
        w->sent = true; w->sent_x = pos->next_x; w->sent_y = pos->next_y; w->last_send = now;
      }
    }
    return;
  }

  float angle = atan2f(vx, vy);
  if (!was_active || !walk_pad_sector_holds(w->sector, angle)) w->sector = walk_pad_sector_of(angle);

  int dx, dy;
  walk_pad_dir(w->sector, &dx, &dy);
  int tx, ty;
  if (!walk_pad_target(pos, dx, dy, vx, vy, pass, ctx, &tx, &ty)) return;
  bool same = w->sent && tx == w->sent_x && ty == w->sent_y;
  if (same && pos->next_x != 255) return;
  if (w->sent && now - w->last_send < (same ? WALK_PAD_RETRY_MS : WALK_PAD_SEND_MS)) return;
  if (send) {
    send(ctx, tx, ty);
    w->sent = true; w->sent_x = tx; w->sent_y = ty; w->last_send = now;
  }
}
