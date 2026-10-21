#include "client_ui.h"
#define DONATE_LABEL (SCREEN_W >= 600 ? "Donate / TC" : "Donate")
#include "client_shop.h"
#include "client_social.h"
#include "client_games.h"
#include "client_pets.h"
#include "client_trade.h"
#include "client_casino.h"
#include "client_snow.h"
#include "client_pool.h"
#include "client_bar.h"
#include "client_identity.h"
#include "client_music.h"
#include "audio.h"
#include "avatar.h"
#include "gfx.h"
#include "net.h"
#include "show.h"
#include "world.h"
#include "proto.h"
#include "paylink.h"
#include "update.h"
#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { CLOSED, ROOMS, CHAT, ME, DRESS, HELP, KEYBOARD, NEW_ROOM, PEOPLE, PERSON, CONFIRM, RIGHTS, MORE, SHOP, SOCIAL, GAMES, PETS, SETTINGS, LOCK_CONFIRM, TRADE, ACTIONS, CASINO, SNOW, POOL, BAR, IDENTITY, MUSIC, CHIPS, SUPPORT, UPDATE, DELETE_CONFIRM };
enum { OPEN = 1, CLOSE, REFRESH, SCOPE, ENTER_ROOM, PAGE, WRITE, RENAME, CATEGORY,
       STYLE, COLOR, SAVE_LOOK, KEY, BACKSPACE, SHIFT, SPACE, SUBMIT, OFFLINE, RESUME, LOBBY,
       PICK_STYLE, PICK_COLOR, SEARCH, CLEAR_SEARCH, MODEL, CREATE_ROOM, SELECT_PERSON, MODERATE, CONFIRM_MOD, SYMBOLS, REVOKE, UNBAN, OPEN_SHOP, REMOVE_LOCK, SAVE_LOCK, GESTURE, PUT_DRINK_AWAY, ACTIVITY_CONTEXT, GOTO_CASINO, PAY_CODE, UPDATE_LATER, UPDATE_INSTALL, UPDATE_CHECK, UPDATE_RESTART, UPDATE_CLOSE, CHECK_UPDATES };
#ifdef TWEEBBO_LAN_ONLY
#define RETURN_LABEL "Return to LAN hotel"
#else
#define RETURN_LABEL "Return to the hotel"
#endif
enum { EDIT_CHAT, EDIT_NAME, EDIT_PASSWORD, EDIT_SEARCH, EDIT_TITLE, EDIT_FEATURE, EDIT_ROOM_PASSWORD, EDIT_ROOM_NAME };
enum { DELETE_ROOM=99, ALERT_CLOSE=100 };
typedef struct { float x, y, w, h; int cmd, arg; } Button;
typedef struct { char slug[48], title[164], owner[40], label[48], icon[32]; int online; bool locked, featured, credits; } Room;
static struct {
  int panel, focus, count, scope, page, room_n, job, category, shop_kind, edit, return_panel;
  bool shift, tv, keyboard_focus, symbols, secret;
  bool alert;
  char alert_title[80],alert_message[512];
  json_int_t alert_id;
  Button buttons[100];
  Room rooms[100];
  struct { char key[48], name[80]; } models[64];
  int model_n, model, job_kind, visible[100], visible_n;
  char query[128], title[164];
  u32 person;
  int people_page, mod;
  int rights_page;
  Look look;
  char error[128], input[256], draft[256], status[128];
  char chat[160][96];
  int chat_n, chat_page, chat_pending;
  char server[128];
  char chat_room[48];
  char edit_title[64];
  char settings_room[32], lock_job_room[32], room_password[129];
  bool lock_known, locked;
  float chip_x, chip_y, chip_w, chip_h;
  bool (*edit_submit)(const char *);
  void (*edit_cancel)(const char *);
} U;
static float para(const char *text,float x,float y,float width,int lines,u32 ink);
static int rooms_per_page(void);
static void panel_rect(float *x,float *y,float *w,float *h);
static const char *key_row(int row) {
  static const char *lower[]={"1234567890","qwertyuiop","asdfghjkl","zxcvbnm.,?","ñáéíóúü¡¿!"};
  static const char *upper[]={"1234567890","QWERTYUIOP","ASDFGHJKL","ZXCVBNM.,?","ÑÁÉÍÓÚÜ¡¿!"};
  static const char *symbols[]={"1234567890","@#$%&*()-+",":;/\\_=<>[]",".,?!'\"{}|~","ñáéíóúü¡¿!"};
  return (U.symbols?symbols:U.shift?upper:lower)[row];
}

static u32 color(u32 c) { return 0xff000000 | (c & 255) << 16 | (c & 0xff00) | (c >> 16); }
static int wrap(int v, int n) { return n > 0 ? (v % n + n) % n : 0; }
static void copy(char *dst, size_t cap, const char *src) {
  size_t n = src ? strlen(src) : 0;
  if (n >= cap) { n = cap - 1; while (n && ((unsigned char)src[n] & 0xc0) == 0x80) n--; }
  memcpy(dst, src ? src : "", n); dst[n] = 0;
}
// ---------------------------------------------------------------- Classic interface look
// The same steel-blue windows, capsule buttons and light body as the web edition and the
// 3DS client (client/public/classic), drawn over the shared room renderer.
enum { CL_RIM=0x4f7486, CL_EDGE=0x36586b, CL_BODY=0xeeeeee, CL_MUTED=0x4d5b61,
       CL_SEL=0xcfe3ec, CL_FACE=0xf7f7f2, CL_FACE_OFF=0xdcdcd4, CL_SHADE=0xd2d2ca,
       CL_GOLD=0xffd34d, CL_DARK=0x2b3b42, CL_BAR=0x3a3a3a, CL_INK=0x18272a,
       CL_PLACEHOLDER=0x6b6b6b, CL_DISABLED=0x5f5f5f,
       CL_RED=0xc1272d, CL_ONLINE=0x3aa83a };

static u32 color_alpha(u32 c, u8 a) {
  return (u32)a << 24 | (c & 255) << 16 | (c & 0xff00) | (c >> 16 & 255);
}
static const UiSprite *ui_find(const char *name) {
  for (int i = 0; i < ui_sprite_count; i++) if (!strcmp(ui_sprites[i].name, name)) return &ui_sprites[i];
  return NULL;
}
static bool ui_icon(const char *name, float x, float y) {
  const UiSprite *s = ui_find(name);
  if (!s) return false;
  gfx_sprite(s->page, s->x, s->y, s->w, s->h, floorf(x), floorf(y), 1, false, 0xffffffff);
  return true;
}
/** Draws a classic graphic centred on (cx, cy) at 1:1. */
static bool ui_icon_at(const char *name, float cx, float cy) {
  const UiSprite *s = ui_find(name);
  if (!s) return false;
  return ui_icon(name, cx - s->w / 2.0f, cy - s->h / 2.0f);
}
/** Classic graphic centred on (cx, cy) and shrunk to fit a `box` square. */
static bool ui_icon_fit(const char *name, float cx, float cy, float box) {
  const UiSprite *s = ui_find(name);
  if (!s) return false;
  float k = fminf(1.0f, box / fmaxf((float)s->w, (float)s->h));
  gfx_sprite(s->page, s->x, s->y, s->w, s->h,
             floorf(cx - s->w * k / 2.0f), floorf(cy - s->h * k / 2.0f), k, false, 0xffffffff);
  return true;
}
/** 9-slice of the original "<prefix>top.left" chrome (window. / content.). */
static void nine_slice(const char *prefix, float x, float y, float w, float h, u32 tint) {
  char n[48];
  snprintf(n, sizeof n, "%stop.left", prefix);      const UiSprite *tl = ui_find(n);
  snprintf(n, sizeof n, "%stop.middle", prefix);    const UiSprite *tm = ui_find(n);
  snprintf(n, sizeof n, "%stop.right", prefix);     const UiSprite *tr = ui_find(n);
  snprintf(n, sizeof n, "%smiddle.left", prefix);   const UiSprite *ml = ui_find(n);
  snprintf(n, sizeof n, "%smiddle.middle", prefix); const UiSprite *mm = ui_find(n);
  snprintf(n, sizeof n, "%smiddle.right", prefix);  const UiSprite *mr = ui_find(n);
  snprintf(n, sizeof n, "%sbottom.left", prefix);   const UiSprite *bl = ui_find(n);
  snprintf(n, sizeof n, "%sbottom.middle", prefix); const UiSprite *bm = ui_find(n);
  snprintf(n, sizeof n, "%sbottom.right", prefix);  const UiSprite *br = ui_find(n);
  if (!tl || !tm || !tr || !ml || !mm || !mr || !bl || !bm || !br) return;
  float l = tl->w, r = tr->w, t = tl->h, b = bl->h;
  float iw = w - l - r, ih = h - t - b;
  if (iw < 0) iw = 0;
  if (ih < 0) ih = 0;
  if (iw > 0 && ih > 0)
    gfx_sprite_xy(mm->page, mm->x, mm->y, mm->w, mm->h, x+l, y+t, iw/mm->w, ih/mm->h, false, tint);
  if (iw > 0) {
    gfx_sprite_xy(tm->page, tm->x, tm->y, tm->w, tm->h, x+l, y, iw/tm->w, t/tm->h, false, tint);
    gfx_sprite_xy(bm->page, bm->x, bm->y, bm->w, bm->h, x+l, y+h-b, iw/bm->w, b/bm->h, false, tint);
  }
  if (ih > 0) {
    gfx_sprite_xy(ml->page, ml->x, ml->y, ml->w, ml->h, x, y+t, l/ml->w, ih/ml->h, false, tint);
    gfx_sprite_xy(mr->page, mr->x, mr->y, mr->w, mr->h, x+w-r, y+t, r/mr->w, ih/mr->h, false, tint);
  }
  gfx_sprite(tl->page, tl->x, tl->y, tl->w, tl->h, x, y, 1, false, tint);
  gfx_sprite(tr->page, tr->x, tr->y, tr->w, tr->h, x+w-r, y, 1, false, tint);
  gfx_sprite(bl->page, bl->x, bl->y, bl->w, bl->h, x, y+h-b, 1, false, tint);
  gfx_sprite(br->page, br->x, br->y, br->w, br->h, x+w-r, y+h-b, 1, false, tint);
}
/** Original capsule/field 9-slice ("<base>.left[.state]" and so on). */
static const UiSprite *pill_part(const char *base, const char *part, const char *state) {
  char n[64];
  if (state && *state) snprintf(n, sizeof n, "%s.%s.%s", base, part, state);
  else snprintf(n, sizeof n, "%s.%s", base, part);
  return ui_find(n);
}
static bool pill(const char *base, const char *state, float x, float y, float w, float h, u32 tint) {
  const UiSprite *l = pill_part(base, "left", state);
  const UiSprite *m = pill_part(base, "middle", state);
  const UiSprite *r = pill_part(base, "right", state);
  if (!l || !m || !r) return false;
  float lw = l->w, rw = r->w, iw = w - lw - rw;
  gfx_sprite_xy(l->page, l->x, l->y, l->w, l->h, x, y, 1, h/l->h, false, tint);
  gfx_sprite_xy(r->page, r->x, r->y, r->w, r->h, x+w-rw, y, 1, h/r->h, false, tint);
  if (iw > 0) gfx_sprite_xy(m->page, m->x, m->y, m->w, m->h, x+lw, y, iw/m->w, h/m->h, false, tint);
  return true;
}
/** Width of a pad-button glyph (circle, shoulder or D-pad). */
static float pad_glyph_width(const char *key) {
  if (!strcmp(key, "Cross") || !strcmp(key, "Circle") || !strcmp(key, "Triangle") ||
      !strcmp(key, "Square") || !strcmp(key, "D-pad")) return 16;
  return fmaxf(16, text_width(key, 11) + 10);
}
#if TWEEBBO_DEVICE == 3
/** PlayStation face symbol inside the light key. */
static void play_shape(const char *key, float cx, float cy, u32 ink) {
  if (!strcmp(key, "Cross")) {
    for (int k = -1; k <= 1; k++) {
      draw_line(cx - 3.4f, cy - 3.4f + k, cx + 3.4f, cy + 3.4f + k, ink);
      draw_line(cx - 3.4f, cy + 3.4f + k, cx + 3.4f, cy - 3.4f + k, ink);
    }
  } else if (!strcmp(key, "Circle")) {
    fill_circle(cx, cy, 4.2f, ink);
    fill_circle(cx, cy, 2.7f, color(CL_FACE));
  } else if (!strcmp(key, "Triangle")) {
    fill_triangle(cx, cy - 4.2f, cx - 4.2f, cy + 3.2f, cx + 4.2f, cy + 3.2f, ink);
    fill_triangle(cx, cy - 1.6f, cx - 2.2f, cy + 2.2f, cx + 2.2f, cy + 2.2f, color(CL_FACE));
  } else if (!strcmp(key, "Square")) {
    fill_rect(cx - 3.5f, cy - 3.5f, 7, 7, ink);
    fill_rect(cx - 2.1f, cy - 2.1f, 4.2f, 4.2f, color(CL_FACE));
  }
}
#endif
/** One native pad glyph: Switch-style circles/shoulders or Vita shapes. */
static void pad_glyph(const char *key, float x, float y) {
  float w = pad_glyph_width(key), cx = x + w / 2, cy = y + 7;
#if TWEEBBO_DEVICE == 3
  if (!strcmp(key, "Cross") || !strcmp(key, "Circle") || !strcmp(key, "Triangle") || !strcmp(key, "Square")) {
    fill_circle(cx, cy, 7, color(0x1f2a30));
    fill_circle(cx, cy, 6, color(CL_FACE));
    play_shape(key, cx, cy, color(0x1f2a30));
    return;
  }
#endif
  if (key[0] && !key[1] && strchr("ABXY", key[0])) {
    fill_circle(cx, cy, 7, color(0x1f2a30));
    fill_circle(cx, cy, 6, color(CL_FACE));
    text_draw(key, cx, y + 1, 10, color(0x1f2a30), 1);
    return;
  }
  if (!strcmp(key, "D-pad")) {
    fill_circle(cx, cy, 7, color(0x1f2a30));
    fill_circle(cx, cy, 6, color(CL_FACE));
    fill_rect(cx - 1.5f, cy - 3.8f, 3, 7.6f, color(0x1f2a30));
    fill_rect(cx - 3.8f, cy - 1.5f, 7.6f, 3, color(0x1f2a30));
    return;
  }
  // Shoulder and menu buttons keep the capsule shape.
  fill_round(x, y, w, 14, 7, color(0x1f2a30));
  fill_round(x + 1, y + 1, w - 2, 12, 6, color(CL_FACE));
  // The glyph letter is always dark: the key face is light whatever the hint text colour is.
  text_draw(key, cx, y + 2, 10, color(0x1f2a30), 1);
}
/** One "Ⓐ Choose" hint; returns the x after it. */
float client_ui_key_hint(const char *key, const char *text, float x, float y, u32 ink, u32 text_ink) {
  (void)ink;
  pad_glyph(key, x, y);
  float w = pad_glyph_width(key);
  text_draw(text, x + w + 5, y + 3, 11, color(text_ink), 0);
  return x + w + 5 + text_width(text, 11) + 10;
}
static void hotspot(float x, float y, float w, float h, int cmd, int arg);
static bool pill(const char *base, const char *state, float x, float y, float w, float h, u32 tint);
/** One pad hint as a small classic button (the original button.b capsule, dark Verdana-like
 *  label, the pad glyph inside), like the hotel's own buttons; tappable when `cmd` is set. */
static float hint_chip(const char *key, const char *text, float x, float y, int cmd, int arg) {
  float gw = pad_glyph_width(key), w = gw + 6 + text_width(text, 11) + 14, h = 22;
  int i = U.count;
  hotspot(x, y, w, h, cmd, arg);
  if (cmd && U.panel && U.focus == i) fill_round(x-2, y-2, w+4, h+4, 8, color(CL_EDGE));
  if (!pill("button.b", "active", x, y, w, h, 0xffffffff)) {
    fill_round(x, y, w, h, 6, color(CL_DARK));
    fill_round(x+1, y+1, w-2, h-2, 5, color(CL_FACE));
  }
  pad_glyph(key, x + 6, y + 4);
  text_draw(text, x + 6 + gw + 5, y + 5, 11, color(CL_INK), 0);
  return x + w + 4;
}
/** Always-on control legend in the room: a row of classic hint buttons, wrapped when needed. */
static void room_hints(float margin) {
#if TWEEBBO_DEVICE == 3
  static const char *keys[] = {"Cross", "Triangle", "Square", "Circle", "R-stick"};
  static const char *labels[] = {"Talk/use", "Chat", "Wave", "Rooms", "Tools"};
#else
  static const char *keys[] = {"A", "Y", "X", "B", "L/R", "ZL/ZR"};
  static const char *labels[] = {"Talk/use", "Chat", "Wave", "Rooms", "Zoom", "Tools"};
#endif
  float hud = SCREEN_W >= 600 ? 252 : 168;
  float start = margin + 8, x = start, y = margin + 8, max = SCREEN_W - margin - 16 - hud;
  for (int i = 0; i < COUNT(keys); i++) {
    float w = pad_glyph_width(keys[i]) + 6 + text_width(labels[i], 11) + 14;
    if (x > start && x + w > max) { x = start; y += 26; }
    int cmd = !strcmp(labels[i], "Rooms") || !strcmp(labels[i], "Tools") ? OPEN : 0;
    int arg = !strcmp(labels[i], "Rooms") ? ROOMS : MORE;
    x = hint_chip(keys[i], labels[i], x, y, cmd, arg);
  }
}
/** The room's contextual prompt as the same classic capsule, with no panel border band. */
void client_ui_prompt(const char *key, const char *text, float right, float y) {
  float w = pad_glyph_width(key) + 6 + text_width(text, 11) + 14;
  hint_chip(key, text, right - w, y, 0, 0);
}
/** Pad legend of the large windows, as classic hint buttons on the toolbar strip. */
static void window_hints(float x,float y,bool rooms) {
#if TWEEBBO_DEVICE == 3
  const char *k[]={"Cross","Circle","D-pad","L/R","R-stick"};
#else
  const char *k[]={"A","B","D-pad","L/R","ZL/ZR"};
#endif
  bool sections=!rooms && client_shop_sections();
  const char *l[]={rooms?"Enter":"Choose","Close","Move","Page","Section"};
  int n=sections?5:4;
  for(int i=0;i<n;i++)x=hint_chip(k[i],l[i],x,y,i==1?CLOSE:0,0);
}
/** Registers a control rectangle without drawing anything. */
static void hotspot(float x, float y, float w, float h, int cmd, int arg) {
  if (cmd && U.count < COUNT(U.buttons)) U.buttons[U.count++] = (Button){x,y,w,h,cmd,arg};
}
static void label(const char *s, float x, float y, float width, u32 ink) {
  char text[384]; copy(text, sizeof text, s); text_fit(text, 12, width);
  text_draw(text, x, y, 12, color(ink), 0);
}
/** Classic capsule button with the original button.a/b art and the yellow pad focus. */
static void button(float x, float y, float w, float h, const char *text, int cmd, int arg) {
  int i = U.count;
  hotspot(x, y, w, h, cmd, arg);
  bool enabled = cmd != 0;
  bool focused = enabled && U.panel && U.focus == i;
  bool selected = (cmd==CATEGORY && arg==U.category) || (cmd==SCOPE && arg==U.scope) || (cmd==OPEN && arg==U.panel);
  float r = fminf(h / 2.0f, 12.0f);
  if (focused) fill_round(x-2, y-2, w+4, h+4, r+2, color(CL_EDGE));
  u32 tint = enabled ? 0xffffffff : 0xffb4b4b4;
  if (!pill(h >= 20 ? "button.a" : "button.b", "active", x, y, w, h, tint)) {
    fill_round(x, y, w, h, r, color(CL_DARK));
    fill_round(x+1, y+1, w-2, h-2, fmaxf(r-1, 1), color(!enabled ? CL_FACE_OFF : CL_FACE));
  }
  if (selected) fill_round(x+4, y+3, fmaxf(w-8, 2), fmaxf(h-6, 2), fmaxf(r-2, 2), color_alpha(CL_SEL, 120));
  char t[384]; copy(t, sizeof t, text); text_fit(t, 12, w-10);
  text_draw(t, x+w/2, y+(h-14)/2, 12, color(enabled ? CL_INK : CL_DISABLED), 1);
}
/** Original text field (field.*): white inset over the classic rim. */
static void field(float x, float y, float w, float h, const char *text, const char *placeholder, int cmd, int arg) {
  int i = U.count;
  hotspot(x, y, w, h, cmd, arg);
  bool focused = cmd && U.panel && U.focus == i;
  if (focused) fill_round(x-2, y-2, w+4, h+4, fminf((h+4)/2.0f, 12), color(CL_EDGE));
  fill_round(x, y, w, h, 4, color(0xffffff));
  if (!pill("field", NULL, x, y, w, h, 0xffffffff)) {
    fill_round(x, y, w, h, 4, color(0x7d7d7d));
    fill_round(x+1, y+1, w-2, h-2, 3, color(0xffffff));
  }
  bool empty = !text || !text[0];
  char t[256]; copy(t, sizeof t, empty ? placeholder : text); text_fit(t, 12, w-12);
  text_draw(t, x+7, y+(h-14)/2, 12, color(empty ? CL_PLACEHOLDER : CL_INK), 0);
}
/** Original 9-slice window with a centred title and the classic close box; registers CLOSE. */
static void window_chrome(float x, float y, float w, float h, const char *title, bool closable) {
  // Solid classic frame like the 3DS skin: black edge, steel-blue rim, light body.
  fill_round(x, y, w, h, 6, color(CL_INK));
  fill_round(x+1, y+1, w-2, h-2, 5, color(CL_RIM));
  fill_round(x+6, y+21, w-12, h-27, 5, color(CL_EDGE));
  fill_round(x+7, y+22, w-14, h-29, 4, color(CL_BODY));
  char t[384]; copy(t, sizeof t, title); text_fit(t, 12, w-46);
  text_draw(t, x+w/2, y+4, 12, color(0xffffff), 1);
  if (!closable) return;
  hotspot(x+w-24, y+2, 20, 18, CLOSE, 0);
  if (!ui_icon("button.close.up", x+w-20, y+5)) label("x", x+w-19, y+4, 16, 0xffffff);
}
/** Bottom toolbar: the original alapalkki_bg strip with the PC tools, all directly reachable. */
typedef struct { const char *label, *icon; int cmd, arg; } Tool;
static const Tool TOOLS[] = {
  {"Me",        NULL,               OPEN,        ME},
  {"Navigator", "building_icon@24", OPEN,        ROOMS},
  {"Catalogue", "brochure_icon@24", OPEN_SHOP,   SHOP_CATALOGUE},
  {"Inventory", "hand_icon@26",     OPEN_SHOP,   SHOP_INVENTORY},
  {"Friends",   "mes_dark_icon@24", OPEN,        SOCIAL},
  {"Pets",      NULL,               OPEN,        PETS},
  {"Games",     "games-icon",       OPEN,        GAMES},
  {"Casino",    "casino-icon",      GOTO_CASINO, 0},
  {"Help",      "help_icon@24",     OPEN,        HELP},
  {"Donate",   "badge-CLUB2",      OPEN,        SUPPORT},
};
static bool tool_on(const Tool *t) {
  if (t->cmd == OPEN_SHOP) return U.panel == SHOP && U.shop_kind == t->arg;
  if (t->cmd == GOTO_CASINO) return U.panel == CASINO || !strcmp(W.slug, "casino");
  if (t->cmd == OPEN) return U.panel == t->arg;
  return false;
}
static void paw_icon(float cx, float cy, u32 c) {
  fill_circle(cx, cy+2.5f, 4.4f, color(c));
  fill_circle(cx-5, cy-3.5f, 2.2f, color(c));
  fill_circle(cx, cy-5.5f, 2.2f, color(c));
  fill_circle(cx+5, cy-3.5f, 2.2f, color(c));
}
static void classic_tools(float margin, float bar) {
  Ent *me = world_ent(W.me);
  int n = COUNT(TOOLS);
  float cell = (SCREEN_W - 2*margin - 12) / n;
  for (int i = 0; i < n; i++) {
    float x = margin+6 + i*cell, y = bar+3, w = cell-2, h = 26;
    int idx = U.count;
    const Tool *t = &TOOLS[i];
    hotspot(x, y, w, h, t->cmd, t->arg);
    bool on = tool_on(t);
    bool focused = U.panel && U.focus == idx;
    bool support = t->cmd == OPEN && t->arg == SUPPORT;
    if (support) fill_round(x+1, y, w-2, h-1, 4, color_alpha(CL_GOLD, on ? 64 : 30));
    else if (on) fill_round(x+1, y, w-2, h-1, 4, color_alpha(0xffffff, 40));
    else if (focused) fill_round(x+1, y, w-2, h-1, 4, color_alpha(CL_GOLD, 70));
    float cx = x + w/2;
    // The small Vita canvas keeps the bar icon-only: ten 11 px labels do not fit.
    bool show_label = SCREEN_W >= 600;
    float icy = show_label ? y+8 : y+12, box = show_label ? 22 : 24;
    if (t->cmd == OPEN && t->arg == ME && me)
      avatar_draw_head(me->look, 3, cx - ANCHOR_X*0.5f, (show_label ? y+7 : y+11) - 36*0.5f, 0.5f);
    else if (t->cmd == OPEN && t->arg == PETS) paw_icon(cx, icy, on ? CL_GOLD : 0xdddddd);
    else if (t->icon) ui_icon_fit(t->icon, cx, icy, box);
    if (show_label) text_draw(t->label, cx, y+15, 11, color(support || on ? CL_GOLD : 0xdddddd), 1);
    if (on || support) fill_rect(x+6, bar+30, w-12, 2, color(CL_GOLD));
  }
}
/** The Casino's real-money chip (the web draws it in CSS). */
static void chip_icon(float cx, float cy, float r) {
  fill_circle(cx, cy, r, color(0x7c1116));
  fill_circle(cx, cy, r-1, color(CL_RED));
  fill_circle(cx, cy, r-3, color(0xf6dede));
  fill_circle(cx, cy, r-4.5f, color(CL_RED));
}
/** Top-right HUD, like the web's floating room window: room, players online, credits and chips. */
static void classic_hud(float margin) {
  bool wide = SCREEN_W >= 600;
  float w = wide ? 252 : 168;
  float x = SCREEN_W - margin - 8 - w, y = 8, h = wide ? 50 : 28;
  fill_round(x, y, w, h, 6, color(CL_INK));
  fill_round(x+1, y+1, w-2, h-2, 5, color(CL_RIM));
  fill_round(x+6, y+6, w-12, h-12, 4, color(CL_EDGE));
  fill_round(x+7, y+7, w-14, h-14, 3, color(CL_BODY));
  char buf[32];
  if (wide) {
    label(W.in_room ? W.title : "Tweebbo", x+11, y+9, w-98, CL_INK);
    snprintf(buf, sizeof buf, "%d online", W.online);
    float ox = x + w - 10 - text_width(buf, 11);
    fill_round(ox-9, y+13, 6, 6, 3, color(CL_ONLINE));
    text_draw(buf, ox, y+9, 11, color(CL_MUTED), 0);
    ui_icon("credits_icon@14", x+12, y+29);
    snprintf(buf, sizeof buf, "%u", (unsigned)W.credits);
    text_draw(buf, x+28, y+28, 12, color(CL_INK), 0);
    chip_icon(x+96, y+35, 8);
    snprintf(buf, sizeof buf, "%u", (unsigned)W.chips);
    text_draw(buf, x+108, y+28, 12, color(CL_INK), 0);
    text_draw("+", x+108+text_width(buf, 12)+7, y+27, 13, color(CL_RED), 0);
  } else {
    snprintf(buf, sizeof buf, "%d", W.online);
    fill_round(x+10, y+11, 6, 6, 3, color(CL_ONLINE));
    text_draw(buf, x+20, y+7, 11, color(CL_MUTED), 0);
    ui_icon("credits_icon@14", x+46, y+7);
    snprintf(buf, sizeof buf, "%u", (unsigned)W.credits);
    text_draw(buf, x+60, y+6, 11, color(CL_INK), 0);
    chip_icon(x+112, y+13, 7);
    snprintf(buf, sizeof buf, "%u", (unsigned)W.chips);
    text_draw(buf, x+122, y+6, 11, color(CL_INK), 0);
  }
  // The chips pill opens the chip shop window (QR to the PC edition). Over a panel it stays
  // available by touch and from Help, instead of a pad control of the panel.
  U.chip_x = x+w-(wide?70:56); U.chip_y = y+(wide?26:3);
  U.chip_w = wide?66:50; U.chip_h = 20;
  if (!U.panel) hotspot(U.chip_x, U.chip_y, U.chip_w, U.chip_h, OPEN, CHIPS);
}
/** The update window: offer, download progress and install instructions. */
static void update_draw(float x, float y, float w) {
  UpdateState st = update_state();
  char text[240];
  if (st == UPD_CHECKING) {
    client_ui_paragraph("Searching GitHub for the newest Tweebbo release...", x + 12, y + 40, w - 24, 3, CL_INK);
    client_ui_paragraph("The hotel keeps running while Tweebbo checks.", x + 12, y + 110, w - 24, 2, CL_MUTED);
    button(x + 8, y + 188, w - 16, 26, "Close", UPDATE_CLOSE, 0);
  } else if (st == UPD_AVAILABLE) {
    snprintf(text, sizeof text, "Tweebbo %s is available.", update_version());
    label(text, x + 12, y + 34, w - 24, CL_INK);
    client_ui_paragraph(update_notes()[0] ? update_notes() : "Your character, rooms and purchases stay the same.", x + 12, y + 60, w - 24, 6, CL_INK);
    client_ui_paragraph("Update now? The download continues if the connection drops.", x + 12, y + 150, w - 24, 2, CL_MUTED);
    button(x + 8, y + 188, 140, 26, "Later", UPDATE_LATER, 0);
    button(x + 156, y + 188, 146, 26, "Update now", UPDATE_INSTALL, 0);
  } else if (st == UPD_DOWNLOADING) {
    int pct = (int)(update_progress() * 100);
    snprintf(text, sizeof text, "Downloading Tweebbo %s... %d%%", update_version(), pct);
    label(text, x + 12, y + 40, w - 24, CL_INK);
    fill_round(x + 12, y + 70, w - 24, 14, 7, color(CL_SHADE));
    if (pct > 0) fill_round(x + 12, y + 70, (w - 24) * pct / 100.0f, 14, 7, color(CL_ONLINE));
    client_ui_paragraph("Keep Tweebbo open. If the connection drops, the download resumes where it stopped.", x + 12, y + 102, w - 24, 3, CL_MUTED);
    client_ui_paragraph("The release is checked against its SHA-256 before installing.", x + 12, y + 150, w - 24, 2, CL_MUTED);
  } else if (st == UPD_INSTALLED) {
#if TWEEBBO_DEVICE == 3
    label("Update downloaded", x + 12, y + 36, w - 24, CL_ONLINE);
    client_ui_paragraph("Open VitaShell and install this file. Your character and rooms stay the same.", x + 12, y + 66, w - 24, 3, CL_INK);
    snprintf(text, sizeof text, "%s", update_installed_path());
    client_ui_paragraph(text, x + 12, y + 106, w - 24, 3, CL_MUTED);
    button(x + 8, y + 188, w - 16, 26, "Close", UPDATE_CLOSE, 0);
#else
    label("Update installed", x + 12, y + 36, w - 24, CL_ONLINE);
    client_ui_paragraph("Close Tweebbo and open it again from the Homebrew Menu to use the new version. Your character and rooms stay the same.", x + 12, y + 66, w - 24, 5, CL_INK);
    button(x + 8, y + 188, 140, 26, "Later", UPDATE_CLOSE, 0);
    button(x + 156, y + 188, 146, 26, "Restart now", UPDATE_RESTART, 0);
#endif
  } else if (st == UPD_FAILED) {
    label("Update not installed", x + 12, y + 36, w - 24, CL_RED);
    client_ui_paragraph(update_error()[0] ? update_error() : "The update could not be installed.", x + 12, y + 64, w - 24, 5, CL_INK);
    client_ui_paragraph("A partial download is kept: searching again resumes it.", x + 12, y + 150, w - 24, 2, CL_MUTED);
    button(x + 8, y + 188, 140, 26, "Later", UPDATE_LATER, 0);
    button(x + 156, y + 188, 146, 26, "Try again", UPDATE_CHECK, 0);
  } else if (st == UPD_LATEST) {
    snprintf(text, sizeof text, "You have the latest version (%s).", update_current());
    label(text, x + 12, y + 40, w - 24, CL_INK);
    client_ui_paragraph("Tweebbo searches for updates when you enter the hotel. You can search again here at any time.", x + 12, y + 82, w - 24, 4, CL_MUTED);
    button(x + 8, y + 188, w - 16, 26, "Close", UPDATE_CLOSE, 0);
  } else {
    label("Tweebbo updates", x + 12, y + 40, w - 24, CL_INK);
    client_ui_paragraph("Search GitHub for the newest release. Tweebbo installs it inside the game.", x + 12, y + 70, w - 24, 4, CL_MUTED);
    button(x + 8, y + 188, 140, 26, "Later", UPDATE_LATER, 0);
    button(x + 156, y + 188, 146, 26, "Search now", UPDATE_CHECK, 0);
  }
}
/** The live pay code (paylink.c) in a white square, or the QR to tweebbo.lol without one. */
static void pay_qr(float cx, float cy, float side) {
  fill_round(cx-side/2-4, cy-side/2-4, side+8, side+8, 4, color(0xffffff));
  int n = paylink_qr_size();
  if (!n || paylink_state() != PAY_READY) { text_draw("Create a code", cx, cy-6, 11, color(CL_MUTED), 1); return; }
  float m = (float)(int)(side / n), o = (side - m * n) / 2;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (paylink_qr_dark(x, y)) fill_rect(cx-side/2+o+x*m, cy-side/2+o+y*m, m, m, color(0x111920));
}
/** Under the copy: the pay button and where the current code stands. */
static void pay_footer(float x, float y, float w, float qx, int kind) {
  PayState st = paylink_state();
  char line[200];
  if (st == PAY_READY) { int s = paylink_seconds_left(now_ms()); snprintf(line, sizeof line, "Scan to pay %d:%02d", s/60, s%60); }
  else snprintf(line, sizeof line, "%s", st == PAY_LOADING ? "Making a code..." : "Phone checkout");
  text_draw(line, qx + 44, y + 128, 11, color(CL_INK), 1);
  const char *hint = st == PAY_PAID ? paylink_message() : st == PAY_ERROR ? paylink_message()
    : st == PAY_EXPIRED ? "The code expired. Make a new one."
    : st == PAY_READY ? "Scan the code with your phone and pay there. This window updates by itself."
    : "Choose crypto or PayPal on your phone. Donations also accept custom amounts.";
  client_ui_paragraph(hint, x + 12, y + 150, w - 24, 2, st == PAY_PAID ? CL_ONLINE : st == PAY_ERROR ? CL_RED : CL_MUTED);
  bool busy = st == PAY_LOADING;
  button(x + 8, y + 188, w - 16, 26, st == PAY_READY ? "Make a new code" : "Pay with my phone", busy || !net_online() ? 0 : PAY_CODE, kind);
}
/** Chips: bought with a phone through a pay code; on consoles the Casino plays with credits. */
static void chips_draw(float x, float y, float w) {
  float tx = x + 12, tw = w - 118, qx = x + w - 100, ty = y + 32;
  char text[200];
  ty += para("Chips are the Casino's real-money currency (18+). Buy them for this character; they play at tweebbo.lol.", tx, ty, tw, 5, CL_INK) + 4;
  snprintf(text, sizeof text, "On %s the Casino plays with free credits.", show_platform());
  para(text, tx, ty, tw, 2, CL_MUTED);
  pay_qr(qx + 44, y + 74, 80);
  pay_footer(x, y, w, qx, 1);
}
/** Support Tweebbo: the $5 donation (Tweebbo Club for life), paid with a phone. */
static void support_draw(float x, float y, float w) {
  float tx = x + 12, tw = w - 118, qx = x + w - 100, ty = y + 30;
  Ent *me = world_ent(W.me);
  bool member = me && (me->flags & FLAG_CLUB);
  label(member ? "You are a Club member. Thank you!" : "Keep the hotel open", tx, ty, tw, member ? CL_ONLINE : 0x6b4a00); ty += 18;
  ty += para("Donate $5.00 or more for these benefits:", tx, ty, tw, 2, CL_INK);
  static const char *perks[] = {"- Tweebbo Club for life", "- VIP badge", "- 100 credits for hanging out", "- 10,000 credits the first time"};
  for (int i = 0; i < 4; i++) ty += para(perks[i], tx, ty, tw, 1, CL_INK);
  pay_qr(qx + 44, y + 74, 80);
  pay_footer(x, y, w, qx, 0);
}
static void changed(void) { U.count = 0; U.focus = 0; }
static void arrow_page(float x,float y,float w,int pages) {
  char s[32];snprintf(s,sizeof s,"Page %d / %d",U.page+1,pages);
  client_ui_arrow(x,y,22,18,"<",pages>1?PAGE:0,-1);
  text_draw(s,x+60,y+3,10,color(CL_MUTED),1);
  client_ui_arrow(x+98,y,22,18,">",pages>1?PAGE:0,1);
}
void client_ui_button(float x,float y,float w,float h,const char *text,int cmd,int arg) {button(x,y,w,h,text,cmd,arg);}
void client_ui_field(float x,float y,float w,float h,const char *text,const char *placeholder,int cmd,int arg) {field(x,y,w,h,text,placeholder,cmd,arg);}
void client_ui_window(float x,float y,float w,float h,const char *title) {window_chrome(x,y,w,h,title,false);}
void client_ui_panel(float x,float y,float w,float h) {nine_slice("content.",x,y,w,h,0xffffffff);}
void client_ui_capsule(float x,float y,float w,float h,const char *text) {
  float r=fminf(h/2.0f,12.0f);
  if(!pill("button.a","active",x,y,w,h,0xffffffff)) {
    fill_round(x,y,w,h,r,color(CL_DARK));
    fill_round(x+1,y+1,w-2,h-2,fmaxf(r-1,1),color(CL_FACE));
  }
  char t[384]; copy(t,sizeof t,text); text_fit(t,12,w-10);
  text_draw(t,x+w/2,y+(h-14)/2,12,color(CL_INK),1);
}
void client_ui_label(const char *text,float x,float y,float width,u32 ink) {label(text,x,y,width,ink);}
bool client_ui_hot(float x,float y,float w,float h,int cmd,int arg) {int i=U.count;hotspot(x,y,w,h,cmd,arg);return cmd && U.panel && U.focus==i;}
bool client_ui_icon_fit(const char *name,float cx,float cy,float box) {return ui_icon_fit(name,cx,cy,box);}
u32 client_ui_rgb(u32 c) {return color(c);}
float client_ui_sprite(const char *name,float x,float y,float scale) {
  const UiSprite *sp=ui_find(name);if(!sp)return 0;
  gfx_sprite(sp->page,sp->x,sp->y,sp->w,sp->h,x,y,scale,false,0xffffffff);return sp->w*scale;
}
void client_ui_arrow(float x,float y,float w,float h,const char *glyph,int cmd,int arg) {
  int i=U.count;hotspot(x,y,w,h,cmd,arg);
  bool focused=cmd && U.panel && U.focus==i;
  if(focused)fill_round(x-2,y-2,w+4,h+4,5,color(CL_EDGE));
  fill_round(x,y,w,h,3,color(CL_DARK));
  fill_round(x+1,y+1,w-2,h-2,2,color(cmd?CL_FACE:CL_FACE_OFF));
  float cx=x+w/2,cy=y+h/2;u32 ink=color(cmd?CL_INK:CL_DISABLED);
  if(!strcmp(glyph,"<"))fill_triangle(cx+3,cy-5,cx+3,cy+5,cx-4,cy,ink);
  else if(!strcmp(glyph,">"))fill_triangle(cx-3,cy-5,cx-3,cy+5,cx+4,cy,ink);
  else if(!strcmp(glyph,"^"))fill_triangle(cx-5,cy+3,cx+5,cy+3,cx,cy-4,ink);
  else fill_triangle(cx-5,cy-3,cx+5,cy-3,cx,cy+4,ink);
}
void client_ui_changed(void) {changed();}
static int paragraph_rows;
size_t client_ui_paragraph(const char *text,float x,float y,float width,int lines,u32 ink) {
  const char *start=text?text:"",*p=start;
  paragraph_rows=0;
  for(int row=0;row<lines && *p;row++,paragraph_rows++) {
    char line[256]={0};int n=0,last_space=-1;
    while(p[n] && p[n]!='\n' && n<250) {
      int bytes=1;while(((unsigned char)p[n+bytes]&0xc0)==0x80)bytes++;
      memcpy(line+n,p+n,bytes);line[n+bytes]=0;
      if(n && text_width(line,12)>width) {line[n]=0;break;}
      if(p[n]==' ')last_space=n;
      n+=bytes;
    }
    if(p[n] && p[n]!='\n' && last_space>0) {n=last_space;line[n]=0;}
    label(line,x,y+row*17,width,ink);p+=n;while(*p==' ')p++;if(*p=='\n')p++;
  }
  return (size_t)(p-start);
}
/** A paragraph that returns its height in pixels (17 per drawn row). */
static float para(const char *text,float x,float y,float width,int lines,u32 ink) {
  client_ui_paragraph(text,x,y,width,lines,ink);
  return paragraph_rows*17.0f;
}
static bool contains(const char *s, const char *query) {
  for (; *s; s++) {
    const unsigned char *a=(const unsigned char *)s,*b=(const unsigned char *)query;
    while (*a && *b && tolower(*a)==tolower(*b)) { a++; b++; }
    if (!*b) return true;
  }
  return !*query;
}
static void filter_rooms(void) {
  U.visible_n=0; U.page=0;
  for (int i=0;i<U.room_n;i++) if (contains(U.rooms[i].title,U.query) || contains(U.rooms[i].slug,U.query))
    U.visible[U.visible_n++]=i;
}
// Public rooms hosted by the offline simulation (source/sim.c), in navigator order.
static const struct { const char *slug, *title; } OFFLINE_ROOMS[] = {
  {"lobby", "Classic Lobby"}, {"park", "Classic Park"}, {"pool", "The Pool"},
  {"diving", "Diving Deck"}, {"rooftop", "Rooftop Rumble"}, {"casino", "Casino"},
  {"afterhours", "After Hours Bar"},
  {"igloo", "Igloo"},
};
static void offline_rooms(void) {
  U.room_n=0; U.visible_n=0; U.page=0; U.error[0]=0; U.job=0; U.job_kind=0;
  for (int i=0;i<COUNT(OFFLINE_ROOMS) && U.room_n<COUNT(U.rooms);i++) {
    Room *room=&U.rooms[U.room_n++];
    copy(room->slug,sizeof room->slug,OFFLINE_ROOMS[i].slug);
    copy(room->title,sizeof room->title,OFFLINE_ROOMS[i].title);
    room->online=0;room->credits=!strcmp(room->slug,"casino");room->featured=room->credits||!strcmp(room->slug,"afterhours");
    copy(room->icon,sizeof room->icon,!strcmp(room->slug,"casino")?"credits_icon":!strcmp(room->slug,"afterhours")?"bar_cigar_icon":"building_icon");
    copy(room->label,sizeof room->label,"Practice with bots");
  }
  filter_rooms();
}
static void refresh(void) {
  if (U.job) return;
  U.room_n = 0; U.visible_n=0; U.page = 0; U.error[0] = 0; U.job_kind=0;
  if (net_state() == NET_OFFLINE) { offline_rooms(); return; }
  static const char *scopes[] = {"all", "public", "mine"};
  char path[64]; snprintf(path, sizeof path, "/api/rooms?scope=%s", scopes[U.scope]);
  U.job = net_http("GET", path, NULL, true);
  if (!U.job) copy(U.error, sizeof U.error, "Could not request rooms. Try Refresh.");
}
static void open_panel(int panel) {
  if(U.panel==IDENTITY && panel!=KEYBOARD)client_identity_close();
  if(panel==IDENTITY && U.panel!=KEYBOARD)client_identity_open(W.guest_id);
  if(panel!=LOCK_CONFIRM)memset(U.room_password,0,sizeof U.room_password);
  if(panel!=U.panel && (U.panel==CHIPS || U.panel==SUPPORT))paylink_close();
  if(panel!=U.panel && U.panel==UPDATE)update_dismiss();
  U.panel = panel; U.status[0] = 0; changed();
  if(panel==SETTINGS) {
    copy(U.settings_room,sizeof U.settings_room,W.slug);U.lock_known=false;
    if(!U.job && W.room_owner && net_online()) {
      U.job=net_http("GET","/api/rooms?scope=mine",NULL,true);U.job_kind=2;
      copy(U.lock_job_room,sizeof U.lock_job_room,W.slug);
    }
  }
  if(panel==SNOW)client_snow_open();
  if(panel==BAR)client_bar_open();
  client_shop_visible(panel==SHOP);
  if (panel == ROOMS) refresh();
  if (panel == SOCIAL) client_social_open();
  if (panel == PETS) client_pets_open();
  if (panel == DRESS) {
    Ent *me = world_ent(W.me);
    U.look = look_decode(me ? me->look : 0); U.category = 0;
  }
}
static void edit_text(int edit) {
  U.edit = edit; U.return_panel = U.panel; U.panel = KEYBOARD; U.shift = false; U.symbols=false;
  copy(U.input, sizeof U.input, edit == EDIT_CHAT ? U.draft : edit == EDIT_NAME ? W.name : edit==EDIT_SEARCH ? U.query : edit==EDIT_TITLE ? U.title : edit==EDIT_ROOM_NAME ? W.title : "");
  changed();
  U.keyboard_focus = true;
}
void client_ui_close(void) {open_panel(CLOSED);}
void client_ui_alert(const char *text) {
  if(!text || strlen(text)>8192)return;
  json_t *j=json_loads(text,JSON_REJECT_DUPLICATES,NULL);
  const char *title=json_string_value(json_object_get(j,"title")),*message=json_string_value(json_object_get(j,"message"));
  json_int_t id=json_integer_value(json_object_get(j,"id"));
  if(title && message && id!=U.alert_id) {
    U.alert_id=id;U.alert=true;
    copy(U.alert_title,sizeof U.alert_title,title);copy(U.alert_message,sizeof U.alert_message,message);
    changed();
  }
  json_decref(j);
}
void client_ui_music(void){open_panel(MUSIC);}
void client_ui_bar(void){open_panel(BAR);}
void client_ui_casino(void) {open_panel(CASINO);}
void client_ui_inspect(u32 item) {
  if(!world_item(item) || client_shop_building())return;
  client_shop_inspect(item);open_panel(SHOP);
}
void client_ui_edit(const char *title,const char *initial,bool (*submit_text)(const char *)) {
  client_ui_edit_draft(title,initial,submit_text,NULL);
}
void client_ui_edit_draft(const char *title,const char *initial,bool (*submit_text)(const char *),void (*cancel)(const char *)) {
  U.secret=false;
  edit_text(EDIT_FEATURE);copy(U.edit_title,sizeof U.edit_title,title);copy(U.input,sizeof U.input,initial);U.edit_submit=submit_text;U.edit_cancel=cancel;
}
typedef struct { const char *name; int *style, *tint; int count, colors; const WardrobeEntry *items; const u32 *palette; } Category;
static Category category(void) {
  Category cats[] = {
    {"Face", &U.look.face_style, &U.look.skin, wardrobe_faces_count,4,wardrobe_faces,SKIN},
    {"Hair", &U.look.hair_style, &U.look.hair_color, wardrobe_hair_count,8,wardrobe_hair,HAIR},
    {"Top", &U.look.shirt_style, &U.look.shirt_color, wardrobe_tops_count,16,wardrobe_tops,SHIRT},
    {"Trousers", &U.look.pants_style, &U.look.pants, wardrobe_bottoms_count,8,wardrobe_bottoms,PANTS},
    {"Shoes", &U.look.shoe_style, &U.look.shoes, wardrobe_shoes_count,4,wardrobe_shoes,SHOES}
  };
  return cats[U.category];
}
static bool ready(void) { return W.in_room && (net_online() || net_state() == NET_OFFLINE); }
static void submit(void) {
  if(U.edit==EDIT_ROOM_NAME) {
    if(U.job || !W.room_owner || !net_online() || strcmp(U.settings_room,W.slug))return;
    json_t *j=json_pack("{s:s,s:s}","slug",U.settings_room,"title",U.input);
    char *body=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
    U.job=body?net_http("POST","/api/rooms/rename",body,true):0;U.job_kind=4;
    copy(U.lock_job_room,sizeof U.lock_job_room,U.settings_room);free(body);
    if(U.job){U.panel=SETTINGS;changed();copy(U.status,sizeof U.status,"Saving room name...");}
    else copy(U.status,sizeof U.status,"Could not send. Your draft is still here.");
    return;
  }
  if(U.edit==EDIT_ROOM_PASSWORD) {
    if(!W.room_owner || !net_online() || strcmp(U.settings_room,W.slug)) {
      memset(U.input,0,sizeof U.input);open_panel(CLOSED);return;
    }
    int chars=0;bool control=false;
    for(const unsigned char *p=(const unsigned char *)U.input;*p;p++) {
      if((*p&0xc0)!=0x80)chars++;
      if(*p<32 || *p==127)control=true;
    }
    if(chars<4 || chars>32 || control || U.input[0]==' ' || U.input[strlen(U.input)-1]==' ') {
      copy(U.status,sizeof U.status,"Use 4-32 characters, without outer spaces.");return;
    }
    copy(U.room_password,sizeof U.room_password,U.input);memset(U.input,0,sizeof U.input);
    open_panel(LOCK_CONFIRM);return;
  }
  if(U.edit==EDIT_FEATURE) {
    if(U.edit_submit && U.edit_submit(U.input)) {memset(U.input,0,sizeof U.input);U.edit_submit=NULL;U.edit_cancel=NULL;U.panel=U.return_panel;U.status[0]=0;changed();}
    else copy(U.status,sizeof U.status,"Could not submit. Your draft is still here.");
    return;
  }
  if (U.edit==EDIT_SEARCH || U.edit==EDIT_TITLE) {
    if (U.edit==EDIT_SEARCH) { copy(U.query,sizeof U.query,U.input);filter_rooms();U.panel=ROOMS;changed(); }
    else { copy(U.title,sizeof U.title,U.input);open_panel(NEW_ROOM); }
    memset(U.input,0,sizeof U.input); return;
  }
  if (U.edit == EDIT_PASSWORD) {
    if (net_state() != NET_LOCKED) { copy(U.status,sizeof U.status,"The room is no longer waiting for a password."); return; }
    net_unlock(U.input); memset(U.input,0,sizeof U.input); open_panel(CLOSED); return;
  }
  if (!ready()) { copy(U.status,sizeof U.status,"Wait until the room reconnects."); return; }
  char *s = U.input;
  while (*s == ' ') s++;
  size_t n = strlen(s); while (n && s[n-1] == ' ') s[--n] = 0;
  if (!n) { copy(U.status,sizeof U.status,"Enter some text first."); return; }
  if (U.edit == EDIT_CHAT) { send_chat(s); U.draft[0] = 0; open_panel(CHAT); }
  else { send_name(s); open_panel(ME); }
  memset(U.input,0,sizeof U.input);
}
static void activate(int cmd, int arg) {
  if(cmd==ALERT_CLOSE){U.alert=false;changed();return;}
  if(net_state()==NET_REPLACED && cmd!=RESUME)return;
  if(cmd==RESUME)U.alert=false;
  if(cmd>=11000){client_music_action(cmd,arg);return;}
  if(cmd>=10000){client_identity_action(cmd,arg);return;}
  if(cmd>=9000){client_bar_action(cmd,arg);return;}
  if(cmd>=8000){pool_action(cmd,arg);return;}
  if(cmd>=7000) {client_snow_action(cmd,arg);return;}
  if(cmd>=6000) {client_casino_action(cmd,arg);return;}
  if(cmd>=5000) {client_trade_action(cmd,arg);return;}
  if(cmd>=4000) {client_pets_action(cmd,arg);return;}
  if(cmd>=3000) {client_games_action(cmd,arg);return;}
  if(cmd>=2000) {client_social_action(cmd,arg);return;}
  if(cmd>=1000) {client_shop_action(cmd,arg);return;}
  if(client_shop_building() && cmd!=CLOSE)return;
  Category c = category();
  switch (cmd) {
    case ACTIVITY_CONTEXT:{
      char label[128];if(!net_online())break;
      if(arg==0&&pool_context(label,sizeof label,now_ms()))pool_context_run(now_ms());
      if(arg==1&&client_bar_context(label,sizeof label,now_ms()))client_ui_bar();
      if(arg==2&&client_music_context(label,sizeof label,now_ms()))client_ui_music();
      break;
    }
    case GESTURE: {
      Ent *me=world_ent(W.me);
      if(ready() && me && arg>=GESTURE_WAVE && arg<=GESTURE_SIT){send_gesture(arg);open_panel(CLOSED);}break;
    }
    case PUT_DRINK_AWAY:
      if(ready()){send_drink(0,0);open_panel(CLOSED);}break;
    case DELETE_ROOM: {
      if(U.job || !W.room_owner || !net_online() || strcmp(U.settings_room,W.slug))break;
      json_t *j=json_pack("{s:s}","slug",U.settings_room);
      char *body=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
      U.job=body?net_http("POST","/api/rooms/delete",body,true):0;U.job_kind=5;
      free(body);copy(U.lock_job_room,sizeof U.lock_job_room,U.settings_room);
      copy(U.status,sizeof U.status,U.job?"Deleting room...":"Could not send. Try again.");
      break;
    }
    case REMOVE_LOCK:
      if(W.room_owner && net_online() && !U.job && !strcmp(U.settings_room,W.slug)) {
        memset(U.room_password,0,sizeof U.room_password);open_panel(LOCK_CONFIRM);
      }
      break;
    case SAVE_LOCK: {
      if(U.job || !W.room_owner || !net_online() || strcmp(U.settings_room,W.slug))break;
      json_t *j=json_pack("{s:s,s:s}","slug",U.settings_room,"password",U.room_password);
      char *body=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
      U.job=body?net_http("POST","/api/rooms/lock",body,true):0;U.job_kind=3;
      copy(U.lock_job_room,sizeof U.lock_job_room,U.settings_room);
      if(body){memset(body,0,strlen(body));free(body);}
      memset(U.room_password,0,sizeof U.room_password);
      U.panel=SETTINGS;changed();
      copy(U.status,sizeof U.status,U.job?"Saving room access...":"Could not send. Enter the password again.");
      break;
    }
    case OPEN: open_panel(arg); break;
    case UPDATE_LATER: update_dismiss(); open_panel(CLOSED); break;
    case UPDATE_INSTALL: update_start(); break;
    case UPDATE_CHECK: update_check(); break;
    case UPDATE_RESTART: update_restart(); break;
    case UPDATE_CLOSE: update_dismiss(); open_panel(CLOSED); break;
    case CHECK_UPDATES:
      if (update_state() != UPD_AVAILABLE && update_state() != UPD_DOWNLOADING &&
          update_state() != UPD_INSTALLED && update_state() != UPD_CHECKING) update_check();
      open_panel(UPDATE);
      break;
    case PAY_CODE: paylink_open(arg ? "chips" : "club", now_ms()); break;
    case OPEN_SHOP: U.shop_kind=arg;client_shop_open(arg);open_panel(SHOP);break;
    case GOTO_CASINO:
      if(net_online() && W.in_room){net_goto("casino","");open_panel(CLOSED);}
      else show_toast("The Casino needs the hotel connection.",now_ms());
      break;
    case CLOSE:
      if(U.panel==POOL){pool_leave();open_panel(CLOSED);return;}
      if(U.panel==SNOW){client_snow_close();return;}
      if(U.panel==TRADE)client_trade_close();
      if(client_shop_building()) {client_shop_key(SHOW_B);return;}
      if (U.panel == KEYBOARD) {
        if (U.edit == EDIT_CHAT) copy(U.draft,sizeof U.draft,U.input);
        if(U.edit==EDIT_FEATURE && U.edit_cancel)U.edit_cancel(U.input);
        memset(U.input,0,sizeof U.input);U.edit_submit=NULL;U.edit_cancel=NULL;U.panel=U.return_panel;U.status[0]=0;changed();
      } else open_panel(U.panel==LOCK_CONFIRM ? SETTINGS : U.panel==DRESS ? ME : U.panel==NEW_ROOM ? ROOMS : U.panel==PERSON ? PEOPLE : U.panel==CONFIRM ? PERSON : CLOSED);
      break;
    case REFRESH: refresh(); break;
    case SCOPE: if (!U.job) { U.scope=arg; refresh(); changed(); } break;
    case ENTER_ROOM:
      if (arg >= 0 && arg < U.room_n) { net_goto(U.rooms[arg].slug, ""); open_panel(CLOSED); }
      break;
    case PAGE:
      if (U.panel == ROOMS) { int per=rooms_per_page(); U.page = wrap(U.page+arg, (U.visible_n+per-1)/per); }
      else if(U.panel==PEOPLE) { U.people_page+=arg;if(U.people_page<0)U.people_page=0; }
      else if(U.panel==RIGHTS) { U.rights_page+=arg;if(U.rights_page<0)U.rights_page=0; }
      else {
        int max=(U.chat_n-1)/5;
        U.chat_page+=arg;
        if(U.chat_page<0)U.chat_page=0;
        if(U.chat_page>max)U.chat_page=max;
        if(!U.chat_page)U.chat_pending=0;
      }
      changed(); break;
    case WRITE: edit_text(arg); break;
    case RENAME: edit_text(EDIT_NAME); break;
    case CATEGORY: U.category=arg; changed(); break;
    case STYLE: *c.style=wrap(*c.style+arg,c.count); break;
    case COLOR: *c.tint=wrap(*c.tint+arg,c.colors); break;
    case PICK_STYLE: *c.style=wrap(arg,c.count); break;
    case PICK_COLOR: *c.tint=wrap(arg,c.colors); break;
    case SEARCH: edit_text(EDIT_SEARCH); break;
    case CLEAR_SEARCH: U.query[0]=0;filter_rooms();changed();break;
    case MODEL: U.model=wrap(U.model+arg,U.model_n);break;
    case REVOKE: case UNBAN:
      if(ready() && W.room_owner)send_room_mod(cmd==REVOKE?MOD_REMOVE_RIGHTS_OF:MOD_UNBAN,(u32)arg,0);
      break;
    case SELECT_PERSON: U.person=(u32)arg;open_panel(PERSON);break;
    case MODERATE: U.mod=arg;open_panel(CONFIRM);break;
    case CONFIRM_MOD: {
      Ent *e=world_ent(U.person);
      bool owner_action=U.mod==MOD_GIVE_RIGHTS || U.mod==MOD_REMOVE_RIGHTS;
      if(!ready() || !e || e->id==W.me || e->guest==W.owner || !(owner_action?W.room_owner:W.room_rights)) {
        copy(U.status,sizeof U.status,"Room access changed. Reopen the player list.");break;
      }
      send_room_mod(U.mod,e->id,1);open_panel(PEOPLE);break;
    }
    case CREATE_ROOM: {
      if(U.job)break;
      if(!net_online() || !*net_token()) { copy(U.status,sizeof U.status,"Reconnect before creating a room.");break; }
      int chars=0;for(const unsigned char *p=(const unsigned char *)U.title;*p;p++)if((*p&0xc0)!=0x80)chars++;
      if(chars<3 || chars>40) { copy(U.status,sizeof U.status,"Use a room name of 3 to 40 characters.");break; }
      if(!U.model_n)break;
      json_t *j=json_pack("{s:s,s:s,s:s}","title",U.title,"model",U.models[U.model].key,"token",net_token());
      char *body=json_dumps(j,JSON_COMPACT);json_decref(j);
      U.job=body?net_http("POST","/api/rooms",body,true):0;
      if(body){memset(body,0,strlen(body));free(body);}
      U.job_kind=1;
      copy(U.status,sizeof U.status,U.job?"Creating your room...":"Could not create room. Try again.");
      changed();break;
    }
    case SAVE_LOOK:
      if (ready()) { send_look(look_encode(&U.look)); open_panel(ME); }
      else copy(U.status,sizeof U.status,"Wait until the room reconnects.");
      break;
    case KEY: {
      const char *s=key_row(arg/10);
      for (int i=0;i<arg%10 && *s;i++) { s++; while (((unsigned char)*s & 0xc0)==0x80) s++; }
      int bytes=1; while (((unsigned char)s[bytes]&0xc0)==0x80) bytes++;
      size_t n=strlen(U.input), cap=U.edit==EDIT_NAME ? NAME_CAP : U.edit==EDIT_PASSWORD ? 65 : sizeof U.input;
      if (*s && n+bytes < cap) { memcpy(U.input+n,s,bytes); U.input[n+bytes]=0; }
      break;
    }
    case SPACE: { size_t n=strlen(U.input), cap=U.edit==EDIT_NAME ? NAME_CAP : U.edit==EDIT_PASSWORD ? 65 : sizeof U.input;
      if (n+1<cap) { U.input[n]=' '; U.input[n+1]=0; } break; }
    case BACKSPACE: { size_t n=strlen(U.input); if (n) { n--; while(n && ((unsigned char)U.input[n]&0xc0)==0x80)n--; U.input[n]=0; } break; }
    case SHIFT: U.shift=!U.shift; break;
    case SYMBOLS: U.symbols=!U.symbols;changed();break;
    case SUBMIT: submit(); break;
    case OFFLINE: open_panel(CLOSED); show_action(SHOW_OFFLINE); break;
    case RESUME: net_resume(); open_panel(CLOSED); break;
    case LOBBY: net_goto("lobby", ""); open_panel(CLOSED); break;
  }
}
/** Navigator rows that fit the window (two-line rows on the Switch, one line on the Vita). */
static float room_row_h(void) { return SCREEN_H>=340 ? 30 : 22; }
static int rooms_per_page(void) {
  float x,y,w,h;int saved=U.panel;U.panel=ROOMS;panel_rect(&x,&y,&w,&h);U.panel=saved;
  int n=(int)((h-112)/room_row_h());return n<1?1:n;
}
/** Classic navigator tab: the selected one joins the body, the others sit darker behind. */
static void nav_tab(float x,float y,float w,const char *text,bool on,int cmd,int arg) {
  int i=U.count;hotspot(x,y,w,22,cmd,arg);
  bool focused=cmd && U.focus==i;
  if(focused)fill_round(x-2,y-2,w+4,26,6,color(CL_EDGE));
  fill_round(x,y,w,24,4,color(CL_EDGE));
  fill_round(x+1,y+1,w-2,23,3,color(on?0xf5f5ec:0xd6d6cc));
  char t[64];copy(t,sizeof t,text);text_fit(t,11,w-8);
  text_draw(t,x+w/2,y+5,11,color(cmd||on?CL_INK:CL_DISABLED),1);
}
/** Occupancy like the classic navigator: count plus a small bar, green to red. */
static void occupancy(float rx,float y,int online) {
  char n[16];snprintf(n,sizeof n,"%d",online);
  float bw=26;fill_rect(rx-bw,y+4,bw,6,color(0xc9c9c0));
  int f=online>=25?bw:(int)(online*bw/25);
  if(f)fill_rect(rx-bw,y+4,f,6,color(online>=20?CL_RED:online>=8?0xe0a020:CL_ONLINE));
  text_draw(n,rx-bw-5,y,10,color(CL_MUTED),2);
}
static void navigator(float x,float y,float w,float h) {
  bool offline=net_state()==NET_OFFLINE,tall=SCREEN_H>=340;
  char line[384];
  float ix=x+10,iw=w-20,ty=y+26;
  if(offline) label("Offline practice: public rooms with bots",ix,ty+4,iw,CL_MUTED);
  else {
    static const char *tabs[]={"All Rooms","Public Rooms","My Rooms"};
    float tw=(iw-4)/3;
    for(int i=0;i<3;i++) nav_tab(ix+i*(tw+2),ty,tw,tabs[i],U.scope==i,U.job?0:SCOPE,i);
  }
  fill_rect(ix,ty+24,iw,1,color(CL_EDGE));
  float sy=ty+30;
  field(ix,sy,iw-(offline?64:150),22,U.query,"Find a room",SEARCH,0);
  button(ix+iw-(offline?60:146),sy,60,22,"Clear",*U.query?CLEAR_SEARCH:0,0);
  if(!offline){button(ix+iw-82,sy,82,22,"Create",!U.job&&U.model_n&&net_online()?OPEN:0,NEW_ROOM);}
  float ly=sy+28,rh=room_row_h();int per=rooms_per_page();
  float lh=per*rh;
  fill_round(ix,ly-1,iw,lh+2,3,color(0xaaaaaa));fill_rect(ix+1,ly,iw-2,lh,color(0xffffff));
  if(U.job) label("Loading rooms...",ix+8,ly+6,iw-16,CL_MUTED);
  else if(U.error[0]) label(U.error,ix+8,ly+6,iw-16,CL_MUTED);
  else if(!U.visible_n) label(*U.query?"No matches. Clear or change the search.":U.scope==2?"You have no rooms yet. Choose Create.":"No rooms here.",ix+8,ly+6,iw-16,CL_MUTED);
  int pages=(U.visible_n+per-1)/per;if(pages<1)pages=1;if(U.page>=pages)U.page=pages-1;
  for(int i=0;i<per && U.page*per+i<U.visible_n;i++) {
    int index=U.visible[U.page*per+i];Room *r=&U.rooms[index];
    float ry=ly+i*rh;
    int hi=U.count;hotspot(ix+1,ry,iw-2,rh,ENTER_ROOM,index);
    bool focused=U.focus==hi;
    if(r->credits)fill_rect(ix+1,ry,iw-2,rh,color(0xfff1c4));
    else if(i%2)fill_rect(ix+1,ry,iw-2,rh,color(0xf1f5f6));
    if(focused){fill_rect(ix+1,ry,iw-2,rh,color(0xcfe3ec));fill_rect(ix+1,ry,3,rh,color(CL_EDGE));}
    if(i)fill_rect(ix+4,ry,iw-8,1,color(0xdde3e5));
    // Icon: featured rooms keep their navigator icon, locked rooms the classic door lock.
    float icx=ix+16,icy=ry+rh/2;
    char icon[48];snprintf(icon,sizeof icon,"%s@18",r->icon);
    if(r->locked) ui_icon_fit("door_password@18",icx,icy,18);
    else if(r->icon[0] && !ui_icon_fit(icon,icx,icy,18)) ui_icon_fit(r->icon,icx,icy,18);
    else if(!r->icon[0]) { fill_round(icx-6,icy-6,12,12,2,color(0x9fb5bd));fill_rect(icx-3,icy-3,6,6,color(0xffffff)); }
    copy(line,sizeof line,r->title);text_fit(line,tall?12:11,iw-(offline?50:96));
    if(tall) {
      text_draw(line,ix+30,ry+2,12,color(CL_INK),0);
      if(r->featured && r->label[0]) snprintf(line,sizeof line,"Featured / %s",r->label);
      else if(r->owner[0]) snprintf(line,sizeof line,"by %s%s",r->owner,r->locked?" / password":"");
      else line[0]=0;
      text_fit(line,10,iw-110);text_draw(line,ix+30,ry+16,10,color(CL_MUTED),0);
    } else text_draw(line,ix+30,ry+4,11,color(CL_INK),0);
    if(!offline) occupancy(ix+iw-34,ry+(rh-12)/2,r->online);
    // The classic "Go" capsule.
    fill_round(ix+iw-30,ry+(rh-16)/2,24,16,3,color(focused?CL_EDGE:0x666666));
    text_draw("Go",ix+iw-18,ry+(rh-16)/2+2,10,color(0xffffff),1);
  }
  float fy=ly+lh+6;
  arrow_page(ix,fy,iw,pages);
  if(!offline) button(ix+iw-74,fy-1,74,20,"Refresh",U.job?0:REFRESH,0);
}
void client_ui_init(void) { audio_init();char muted[8];if(store_load("audio-muted",muted,sizeof muted))audio_mute(!strcmp(muted,"1")); memset(&U,0,sizeof U);client_shop_reset();client_social_reset();client_games_reset();client_pets_reset();client_trade_reset(); copy(U.server,sizeof U.server,net_server()); }
void client_ui_set_tv(bool enabled) {
  enabled = enabled && SCREEN_W >= 640 && SCREEN_H >= 320;
  if (U.tv == enabled) return;
  U.tv = enabled;
  /* Touch rectangles belong to the previous layout until the next draw. */
  U.count = 0;
}
/** The open window: the Navigator and the catalogue/inventory use a large PC-style window
 *  sized to the screen (Switch 640x360, Vita 480x272); the rest keep the compact 310x224. */
static void panel_rect(float *x,float *y,float *w,float *h) {
  float margin=U.tv?16:0,bar=SCREEN_H-32-margin;
  *x=margin+8;*y=margin+8;*w=U.panel==KEYBOARD?360:310;*h=224;
  bool wide=SCREEN_W>=600;
  if(U.panel==ROOMS){*w=wide?372:SCREEN_W-16-2*margin;*h=bar-*y-6;}
  else if(U.panel==CASINO){*w=SCREEN_W-16-2*margin;}
  else if(U.panel==SHOP && client_shop_wide()){*w=wide?476:SCREEN_W-16-2*margin;*h=bar-*y-6;}
}
float client_ui_room_left(void) {
  if(!U.panel)return 0;
  float x,y,w,h;panel_rect(&x,&y,&w,&h);
  return x+w+8;
}
void client_ui_reset(void) {
  pool_room_changed();client_bar_reset();client_identity_reset();client_music_reset();
  client_snow_reset();
  client_casino_reset();
  client_trade_reset();
  client_shop_reset();
  client_social_reset();
  client_games_reset();
  client_pets_reset();
  /* A server switch frees pending HTTP slots; never poll their old IDs. */
  U.job=0; U.room_n=0; U.visible_n=0; U.model_n=0; U.query[0]=0; U.title[0]=0; U.chat_n=0; U.chat_page=0; U.chat_pending=0; U.draft[0]=0;
  U.chat_room[0]=0;
  memset(U.input,0,sizeof U.input); open_panel(CLOSED);
  U.settings_room[0]=0;U.lock_known=false;
  copy(U.server,sizeof U.server,net_server());
}
void client_ui_room(void) {
  pool_room_changed();client_bar_reset();client_identity_reset();client_music_reset();
  if(U.panel==POOL||U.panel==BAR)open_panel(CLOSED);
  client_snow_reset();
  if(U.panel==SNOW || (U.panel==KEYBOARD && U.return_panel==SNOW))open_panel(CLOSED);
  client_casino_reset();
  if(U.panel==CASINO)open_panel(CLOSED);
  client_shop_reset();
  client_games_reset();
  if (strcmp(U.chat_room,W.slug)) {
    client_trade_reset();
    if(U.panel==TRADE)open_panel(CLOSED);
    if(U.panel==SETTINGS || U.panel==LOCK_CONFIRM || (U.panel==KEYBOARD && (U.edit==EDIT_ROOM_PASSWORD || U.edit==EDIT_ROOM_NAME))) {
      memset(U.input,0,sizeof U.input);open_panel(CLOSED);
    }
    U.chat_n=0; U.chat_page=0; U.chat_pending=0;
    if(U.panel==PERSON || U.panel==CONFIRM || U.panel==RIGHTS)open_panel(CLOSED);
  }
  copy(U.chat_room,sizeof U.chat_room,W.slug);
  changed();
}
void client_ui_chat(u32 eid,const char *text) {
  Ent *e=world_ent(eid);
  char message[600]; snprintf(message,sizeof message,"%s: %s",e ? e->name : "Hotel",text);
  const char *s=message;
  while(*s) {
    char line[96]={0}; size_t used=0;
    while(*s) {
      size_t n=1; while(((unsigned char)s[n]&0xc0)==0x80)n++;
      if(used+n>=sizeof line)break;
      memcpy(line+used,s,n);line[used+n]=0;
      if(used && text_width(line,12)>290) { line[used]=0;break; }
      used+=n;s+=n;
    }
    if(U.chat_n==COUNT(U.chat)) { memmove(U.chat,U.chat+1,sizeof U.chat-sizeof U.chat[0]); U.chat_n--; }
    copy(U.chat[U.chat_n++],sizeof U.chat[0],line);
    if(U.chat_page && U.chat_page*5+U.chat_pending<U.chat_n-1)U.chat_pending++;
  }
}
void client_ui_edit_secret(const char *title,bool (*submit)(const char *)){client_ui_edit(title,"",submit);U.secret=true;}
void client_ui_update(void) {
  client_identity_update();client_music_update();
  if(U.panel==CHIPS || U.panel==SUPPORT)paylink_poll(now_ms());
  update_poll(now_ms());
  // Offer a published update at login, once, until the player picks Later. A newer
  // release found later clears the dismissal.
  static int seen_offer;
  if (update_offer() != seen_offer) { seen_offer = update_offer(); update_forget_dismiss(); }
  if (update_state() == UPD_AVAILABLE && !update_dismissed() && !U.panel && W.in_room) open_panel(UPDATE);
  if (strcmp(U.server,net_server())) client_ui_reset();
  if((U.panel==LOCK_CONFIRM || (U.panel==KEYBOARD && (U.edit==EDIT_ROOM_PASSWORD || U.edit==EDIT_ROOM_NAME))) &&
     (!W.room_owner || !net_online() || strcmp(U.settings_room,W.slug))) {
    memset(U.input,0,sizeof U.input);open_panel(CLOSED);
  }
  client_shop_visible(U.panel==SHOP);
  client_shop_update();
  client_social_update(U.panel==SOCIAL);
  client_games_update(U.panel==GAMES);
  client_pets_update();
  client_trade_update(U.panel==TRADE);
  client_casino_update();
  client_snow_update();
  pool_poll(now_ms());client_bar_update();
  static int previous_pool_mode;
  int activity_mode=pool_mode();
  if(activity_mode!=previous_pool_mode){
    if(activity_mode)open_panel(POOL);else if(U.panel==POOL)open_panel(CLOSED);
    previous_pool_mode=activity_mode;
  }
  HttpResult r;
  if (!U.job || !net_http_result(U.job,&r)) return;
  U.job=0;
  json_t *j=r.body ? json_loads(r.body,0,NULL) : NULL;
  if(U.job_kind==5) {
    const char *error=json_string_value(json_object_get(j,"error"));
    if(r.status==200){open_panel(CLOSED);copy(U.status,sizeof U.status,"Room deleted.");}
    else copy(U.status,sizeof U.status,error?error:"Could not delete the room.");
    json_decref(j);free(r.body);changed();return;
  }
  if(U.job_kind==4) {
    const char *title=json_string_value(json_object_get(j,"title"));
    const char *error=json_string_value(json_object_get(j,"error"));
    if(!strcmp(U.lock_job_room,W.slug) && W.room_owner) {
      if(r.status==200 && title){copy(W.title,sizeof W.title,title);copy(U.status,sizeof U.status,"Room name saved.");}
      else copy(U.status,sizeof U.status,error?error:"Could not rename the room.");
      changed();
    }
    json_decref(j);free(r.body);return;
  }
  if(U.job_kind==2 || U.job_kind==3) {
    if(!strcmp(U.lock_job_room,W.slug) && !strcmp(U.settings_room,W.slug) && W.room_owner) {
      if(U.job_kind==3) {
        json_t *locked=json_object_get(j,"locked");
        U.lock_known=r.status==200 && json_is_boolean(locked);
        if(U.lock_known)U.locked=json_is_true(locked);
        copy(U.status,sizeof U.status,U.lock_known?"Room access saved.":"Could not confirm. Reopen settings to check.");
      } else {
        size_t i;json_t *v;
        if(r.status==200)json_array_foreach(json_object_get(j,"rooms"),i,v) {
          const char *slug=json_string_value(json_object_get(v,"slug"));
          if(slug && !strcmp(slug,U.settings_room)) {U.lock_known=true;U.locked=json_is_true(json_object_get(v,"locked"));break;}
        }
        if(!U.lock_known)copy(U.status,sizeof U.status,"Could not load access. Reopen settings to retry.");
      }
      changed();
    }
    json_decref(j);if(r.body){memset(r.body,0,strlen(r.body));free(r.body);}return;
  }
  if(U.job_kind==1) {
    const char *slug=json_string_value(json_object_get(j,"slug"));
    if(r.status==200 && slug && *slug && strlen(slug)<sizeof U.rooms[0].slug) {
      net_goto(slug,"");U.title[0]=0;open_panel(CLOSED);
    } else {
      const char *err=json_string_value(json_object_get(j,"error"));
      copy(U.status,sizeof U.status,err?err:"Room creation failed. Refresh Mine before retrying.");changed();
    }
    json_decref(j);if(r.body){memset(r.body,0,strlen(r.body));free(r.body);}return;
  }
  json_t *rows=json_object_get(j,"rooms");
  if (r.status==200 && json_is_array(rows)) {
    U.room_n=0; U.page=0; U.error[0]=0;
    size_t i; json_t *v;
    json_array_foreach(rows,i,v) {
      const char *slug=json_string_value(json_object_get(v,"slug"));
      const char *title=json_string_value(json_object_get(v,"title"));
      if (!slug || !*slug || strlen(slug)>=sizeof U.rooms[0].slug || !title) continue;
      if (U.room_n==COUNT(U.rooms)) break;
      Room *room=&U.rooms[U.room_n++];
      copy(room->slug,sizeof room->slug,slug); copy(room->title,sizeof room->title,title);
      room->online=(int)json_integer_value(json_object_get(v,"online"));
      room->locked=json_is_true(json_object_get(v,"locked"));
      const char *owner=json_string_value(json_object_get(v,"owner"));
      copy(room->owner,sizeof room->owner,owner?owner:"");
      json_t *feat=json_object_get(v,"featured");
      room->featured=json_is_object(feat);
      const char *fl=json_string_value(json_object_get(feat,"label")),*fi=json_string_value(json_object_get(feat,"icon")),*fa=json_string_value(json_object_get(feat,"accent"));
      copy(room->label,sizeof room->label,fl?fl:"");copy(room->icon,sizeof room->icon,fi?fi:"");
      room->credits=fa && !strcmp(fa,"credits");
    }
    filter_rooms();
    U.model_n=0;U.model=0;
    json_t *models=json_object_get(j,"models");
    json_array_foreach(models,i,v) {
      const char *key=json_string_value(json_object_get(v,"key")),*name=json_string_value(json_object_get(v,"name"));
      if(!key || !name || strlen(key)>=sizeof U.models[0].key || U.model_n==COUNT(U.models))continue;
      copy(U.models[U.model_n].key,sizeof U.models[0].key,key);copy(U.models[U.model_n++].name,sizeof U.models[0].name,name);
    }
  } else copy(U.error,sizeof U.error,r.err[0] ? r.err : "Could not load rooms. Try Refresh.");
  json_decref(j); free(r.body); if(U.panel==ROOMS)changed();
}

static void keyboard(float x,float y,float w) {
  char shown[256];
  if(U.secret || U.edit==EDIT_PASSWORD || U.edit==EDIT_ROOM_PASSWORD) { size_t n=strlen(U.input); memset(shown,'*',n); shown[n]=0; }
  else copy(shown,sizeof shown,U.input);
  /* Keep the insertion end visible on a small screen. */
  char *tail=shown;
  while (*tail && text_width(tail,12)>w-24) { tail++; while(((unsigned char)*tail&0xc0)==0x80)tail++; }
  fill_rect(x+8,y+28,w-16,23,color(0xffffff)); label(*tail ? tail : "Type here",x+12,y+32,w-24,0x18272a);
  float kw=(w-16)/10;
  for(int row=0;row<5;row++) {
    const char *s=key_row(row); int col=0;
    while(*s) {
      int n=1; while(((unsigned char)s[n]&0xc0)==0x80)n++;
      char key[5]={0}; memcpy(key,s,n);
      button(x+8+col*kw,y+56+row*24,kw-2,22,key,KEY,row*10+col);
      s+=n;col++;
    }
  }
  button(x+8,y+180,54,24,U.shift?"abc":"ABC",SHIFT,0);
  button(x+66,y+180,54,24,"Space",SPACE,0);
  button(x+124,y+180,54,24,"Delete",BACKSPACE,0);
  button(x+182,y+180,54,24,U.symbols?"Letters":"#+=",SYMBOLS,0);
  button(x+240,y+180,w-248,24,U.edit==EDIT_CHAT?"Send":"Save",SUBMIT,0);
}

void client_ui_draw(void) {
  U.count=0;
  if(net_state()==NET_REPLACED) {
    float w=300,x=(SCREEN_W-w)/2,y=(SCREEN_H-150)/2;
    client_ui_window(x,y,w,150,"Tweebbo");
    client_ui_paragraph(net_status(),x+12,y+34,w-24,4,CL_INK);
    button(x+50,y+110,w-100,26,"Play here",RESUME,0);
    U.focus=0;
    return;
  }
  if(U.alert) {
    float w=310,x=(SCREEN_W-w)/2,y=(SCREEN_H-224)/2;
    client_ui_window(x,y,w,224,U.alert_title);
    client_ui_paragraph(U.alert_message,x+12,y+34,w-24,10,CL_INK);
    button(x+55,y+184,w-110,26,"OK",ALERT_CLOSE,0);
    U.focus=0;
    return;
  }
  if(U.panel==POOL&&pool_mode()==1){pool_draw_booth();return;}
  if(U.panel==POOL&&(pool_mode()==2||pool_mode()==4)){pool_draw_hud();return;}
  if(U.panel==SNOW && client_snow_playing()){client_snow_hud();return;}
  float margin=U.tv?16:0, bar=SCREEN_H-32-margin;
  // Classic toolbar strip (alapalkki_bg) stretched across the bottom.
  const UiSprite *bar_bg = ui_find("alapalkki_bg");
  if (bar_bg) gfx_sprite_xy(bar_bg->page, bar_bg->x, bar_bg->y, bar_bg->w, bar_bg->h,
                            0, bar-2, (float)SCREEN_W/bar_bg->w, 1, false, 0xffffffff);
  else fill_rect(0,bar,SCREEN_W,32+margin,color(CL_BAR));
  fill_rect(0,bar-1,SCREEN_W,1,color(0x000000));
  // Floating room window (room, players online, credits and chips). Hidden on the small
  // canvas while a panel is open, where it would overlap.
  if (W.in_room && (SCREEN_W >= 600 || !U.panel)) classic_hud(margin);
  else U.chip_w = 0;
  // Small always-on control legend, so the pad map is visible while playing.
  if (!U.panel && W.in_room) room_hints(margin);
  if (!U.panel) {
    char context[128];int kind=-1;
    if(net_online()){
      if(pool_context(context,sizeof context,now_ms()))kind=0;
      else if(client_bar_context(context,sizeof context,now_ms()))kind=1;
      else if(client_music_context(context,sizeof context,now_ms()))kind=2;
    }
    if(kind>=0)button(margin+8,bar-28,302,24,context,ACTIVITY_CONTEXT,kind);
    classic_tools(margin,bar);
    if (!W.in_room) {
      button(SCREEN_W/2-130,SCREEN_H/2+72,124,24,"Rooms",OPEN,ROOMS);
      button(SCREEN_W/2+6,SCREEN_H/2+72,124,24,"Help",OPEN,HELP);
      if(net_state()==NET_LOCKED) button(SCREEN_W/2-90,SCREEN_H/2-2,180,24,"Enter password",WRITE,EDIT_PASSWORD);
    }
    return;
  }
  float x,y,w,h;panel_rect(&x,&y,&w,&h);
  static const char *titles[]={"","Navigator","Chat","My character","Change looks","Help","Write","Create a room","People in this room","Player","Confirm room action","Room access","Hotel","Furniture","Friends Console","Games","Pets","Room settings","Confirm room access","Safe Trading","Character actions","Casino","Snow Storm","Lido activities","After Hours Bar","Badges & identity","Music & sound","Buy chips",DONATE_LABEL,"Tweebbo update"};
  static const char *edit_titles[]={"Write a message","Change name","Room password","Find a room","Room name","","New room password","Rename room"};
  char title_buf[384]; const char *title;
  if (U.panel==KEYBOARD) title = U.edit==EDIT_FEATURE?U.edit_title:edit_titles[U.edit];
  else if (U.panel==CASINO) { snprintf(title_buf,sizeof title_buf,"%s / %u credits",client_casino_title(),W.credits); title=title_buf; }
  else if (U.panel==SHOP && client_shop_wide()) title = client_shop_title();
  else if (U.panel==DELETE_CONFIRM) title = "Delete room";
  else title = titles[U.panel];
  window_chrome(x,y,w,h,title,true);
  char line[384];
  if(U.panel==KEYBOARD) keyboard(x,y,w);
  else if(U.panel==MORE) {
    static const char *names[]={"People","Catalogue","Inventory","Room furniture","Friends","Games","Pets","Help",DONATE_LABEL};
    static const int commands[]={OPEN,OPEN_SHOP,OPEN_SHOP,OPEN_SHOP,OPEN,OPEN,OPEN,OPEN,OPEN};
    static const int args[]={PEOPLE,SHOP_CATALOGUE,SHOP_INVENTORY,SHOP_ROOM,SOCIAL,GAMES,PETS,HELP,SUPPORT};
    for(int i=0;i<COUNT(names);i++)button(x+8+(i%2)*148,y+28+(i/2)*34,146,28,names[i],commands[i],args[i]);
    button(x+8,y+192,146,24,"Snow Storm",OPEN,SNOW);
    button(x+156,y+192,146,24,"Music / sound",OPEN,MUSIC);
    if(W.room_owner)button(x+156,y+164,146,28,"Room settings",OPEN,SETTINGS);
  }
  else if(U.panel==SETTINGS) {
    bool owner=W.room_owner && net_online() && !strcmp(U.settings_room,W.slug);
    label(W.title,x+10,y+32,w-20,0x18272a);
    label(!owner?"Only the connected owner can change access.":U.job?"Loading room access...":!U.lock_known?"Access status unavailable.":U.locked?"Password required for visitors.":"Open to visitors.",x+10,y+57,w-20,0x26373a);
    button(x+8,y+81,w-16,26,"Rename room",owner&&!U.job?WRITE:0,EDIT_ROOM_NAME);
    button(x+8,y+113,w-16,26,"Set / change password",owner&&!U.job?WRITE:0,EDIT_ROOM_PASSWORD);
    button(x+8,y+145,w-16,26,"Remove password",owner&&!U.job&&U.lock_known&&U.locked?REMOVE_LOCK:0,0);
    button(x+8,y+177,140,26,"Rights and bans",owner?OPEN:0,RIGHTS);
    button(x+156,y+177,146,26,"Delete room",owner&&!U.job?OPEN:0,DELETE_CONFIRM);

  }
  else if(U.panel==DELETE_CONFIRM) {
    label(W.title,x+10,y+32,w-20,0x18272a);
    client_ui_paragraph("Delete this room? Everyone leaves. Furniture and pets return to their owners.",x+10,y+65,w-20,4,0x26373a);
    button(x+8,y+180,140,26,"Cancel",CLOSE,0);
    button(x+156,y+180,146,26,"Delete room",!U.job&&W.room_owner&&net_online()?DELETE_ROOM:0,0);
  }
  else if(U.panel==LOCK_CONFIRM) {
    label(W.title,x+10,y+32,w-20,0x18272a);
    client_ui_paragraph(*U.room_password?"Require the new password for future visitors?":"Remove the password and open this room to visitors?",x+10,y+65,w-20,3,0x26373a);
    button(x+8,y+180,140,26,"Cancel",CLOSE,0);
    button(x+156,y+180,146,26,"Confirm",!U.job&&W.room_owner&&net_online()?SAVE_LOCK:0,0);
  }
  else if(U.panel==SHOP)client_shop_draw(x,y,w,h);
  else if(U.panel==SOCIAL)client_social_draw(x,y,w);
  else if(U.panel==GAMES)client_games_draw(x,y,w);
  else if(U.panel==PETS)client_pets_draw(x,y,w);
  else if(U.panel==TRADE)client_trade_draw(x,y,w);
  else if(U.panel==CASINO)client_casino_draw(x,y,w);
  else if(U.panel==SNOW)client_snow_draw(x,y,w);
  else if(U.panel==POOL)pool_draw_panel(x,y,w);
  else if(U.panel==BAR)client_bar_draw(x,y,w);
  else if(U.panel==IDENTITY)client_identity_draw(x,y,w);
  else if(U.panel==MUSIC)client_music_draw(x,y,w);
  else if(U.panel==CHIPS)chips_draw(x,y,w);
  else if(U.panel==SUPPORT)support_draw(x,y,w);
  else if(U.panel==UPDATE)update_draw(x,y,w);
  else if(U.panel==ACTIONS) {
    Ent *me=world_ent(W.me);bool enabled=me && ready();
    label(W.name,x+10,y+30,w-20,0x18272a);
    button(x+8,y+56,w-16,26,"Wave",enabled?GESTURE:0,GESTURE_WAVE);
    button(x+8,y+88,w-16,26,me&&(me->flags&FLAG_DANCE)?"Stop dancing":"Dance",enabled?GESTURE:0,GESTURE_DANCE);
    button(x+8,y+120,w-16,26,me&&(me->flags&(FLAG_SIT|FLAG_LAY))?"Stand up":"Sit on the floor",enabled?GESTURE:0,GESTURE_SIT);
    button(x+8,y+152,w-16,26,"Put drink away",enabled&&me->drink?PUT_DRINK_AWAY:0,0);
  }
  else if(U.panel==ROOMS) {
    navigator(x,y,w,h);
  } else if(U.panel==NEW_ROOM) {
    label("Name (3 to 40 characters)",x+10,y+32,w-20,0x26373a);
    field(x+8,y+52,w-16,26,U.title,"Enter a room name",U.job?0:WRITE,EDIT_TITLE);
    label("Layout",x+10,y+88,w-20,0x26373a);
    label(U.model_n?U.models[U.model].name:"Load the Navigator first.",x+10,y+109,w-20,0x18272a);
    button(x+8,y+134,140,25,"Previous",!U.job&&U.model_n?MODEL:0,-1);
    button(x+156,y+134,146,25,"Next",!U.job&&U.model_n?MODEL:0,1);
    button(x+8,y+188,100,26,"Back",CLOSE,0);
    button(x+164,y+188,138,26,U.job?"Creating...":"Create and enter",!U.job&&U.model_n?CREATE_ROOM:0,0);
  } else if(U.panel==RIGHTS) {
    json_t *j=W.rights_json?json_loads(W.rights_json,0,NULL):NULL;
    json_t *holders=json_object_get(j,"holders"),*bans=json_object_get(j,"bans");
    int nh=(int)json_array_size(holders),nb=(int)json_array_size(bans),n=nh+nb;
    if(!W.room_owner)label("Open your own room to manage access.",x+10,y+36,w-20,0x26373a);
    else {
      int pages=(n+4)/5;if(!pages)pages=1;if(U.rights_page>=pages)U.rights_page=pages-1;
      if(!n)label("No other rights holders or banned players.",x+10,y+36,w-20,0x26373a);
      for(int row=0;row<5 && U.rights_page*5+row<n;row++) {
        int index=U.rights_page*5+row;
        json_t *v=json_array_get(index<nh?holders:bans,index<nh?index:index-nh);
        const char *name=json_string_value(json_object_get(v,"name"));
        label(name?name:"Player",x+10,y+35+row*29,184,0x18272a);
        button(x+198,y+30+row*29,104,26,index<nh?"Remove rights":"Lift ban",index<nh?REVOKE:UNBAN,(int)json_integer_value(json_object_get(v,"guest")));
      }
      button(x+8,y+188,88,26,"Previous",U.rights_page?PAGE:0,-1);
      button(x+214,y+188,88,26,"Next",U.rights_page+1<pages?PAGE:0,1);
    }
    json_decref(j);
  } else if(U.panel==PEOPLE) {
    int count=0;for(int i=0;i<MAX_ENTS;i++)if(W.ents[i].used)count++;
    int pages=(count+4)/5;if(!pages)pages=1;if(U.people_page>=pages)U.people_page=pages-1;
    int n=0;
    for(int i=0;i<MAX_ENTS;i++)if(W.ents[i].used) {
      int row=n++-U.people_page*5;if(row<0 || row>=5)continue;
      Ent *e=&W.ents[i];
      snprintf(line,sizeof line,"%s%s",e->name,e->id==W.me?" (you)":e->guest==W.owner?" (owner)":"");
      button(x+8,y+30+row*29,w-16,26,line,SELECT_PERSON,(int)e->id);
    }
    if(!count)label("Join a room to see its players.",x+10,y+36,w-20,0x26373a);
    button(x+8,y+188,88,26,"Previous",U.people_page?PAGE:0,-1);
    button(x+102,y+188,106,26,"Trading",OPEN,TRADE);
    button(x+214,y+188,88,26,"Next",U.people_page+1<pages?PAGE:0,1);
  } else if(U.panel==PERSON || U.panel==CONFIRM) {
    Ent *e=world_ent(U.person);
    if(!e)label("This player has left the room.",x+10,y+36,w-20,0x26373a);
    else {
      label(e->name,x+10,y+30,w-20,0x18272a);
      if(U.panel==CONFIRM) {
        const char *action=U.mod==MOD_KICK?"Remove this player from the room?":U.mod==MOD_BAN?"Ban this player for one hour?":U.mod==MOD_GIVE_RIGHTS?"Allow this player to arrange furniture?":"Remove this player's room rights?";
        label(action,x+10,y+68,w-20,0x18272a);
        button(x+8,y+170,140,26,"Cancel",CLOSE,0);button(x+156,y+170,146,26,"Confirm",CONFIRM_MOD,0);
      } else {
        avatar_draw(e->look,2,POSE_STD,0,0,2,0,false,x+12,y+50,1,255);
        if(e->device) {
          const char *system=e->device==3?"PlayStation Vita":e->device==2?"Nintendo Switch":"Nintendo 3DS";
          char note[128];
          snprintf(note,sizeof note,e->id==W.me?"You are connected through a %s system.":"This person is connected through a %s system.",system);
          show_device_badge(e->device,x+90,y+54,1.0f);
          client_ui_paragraph(note,x+108,y+50,w-118,2,0x26373a);
        } else label("Web / other client",x+90,y+54,w-100,0x26373a);
        bool other=e->id!=W.me && e->guest!=W.owner && ready();
        if(other && W.room_rights) {
          button(x+90,y+88,100,25,"Kick",MODERATE,MOD_KICK);
          button(x+196,y+88,106,25,"Ban (1 hour)",MODERATE,MOD_BAN);
        }
        if(other && W.room_owner) {
          button(x+90,y+118,212,25,"Give room rights",MODERATE,MOD_GIVE_RIGHTS);
          button(x+90,y+148,212,25,"Remove room rights",MODERATE,MOD_REMOVE_RIGHTS);
        }
        button(x+8,y+188,100,26,"Back",CLOSE,0);
        button(x+116,y+188,186,26,"Trading",OPEN,TRADE);
      }
    }
  } else if(U.panel==CHAT) {
    if(!U.chat_n) label("Messages will appear here.",x+10,y+32,w-20,0x26373a);
    int end=U.chat_n-U.chat_page*5-U.chat_pending;
    if(end<1)end=1;
    int start=end-5; if(start<0)start=0;
    for(int i=start;i<end;i++) label(U.chat[i],x+10,y+30+(i-start)*27,w-20,0x18272a);
    button(x+8,y+174,90,24,"Write",WRITE,EDIT_CHAT);
    if(U.chat_n>5) { button(x+106,y+174,90,24,"Older",PAGE,1); button(x+204,y+174,98,24,"Newer",PAGE,-1); }
  } else if(U.panel==ME) {
    Ent *me=world_ent(W.me);
    if(me) avatar_draw(me->look,2,POSE_STD,0,0,2,0,false,x+16,y+26,1,255);
    label(W.name[0]?W.name:"Waiting for character...",x+94,y+36,w-104,0x18272a);
    snprintf(line,sizeof line,"%u credits",W.credits); label(line,x+94,y+60,w-104,0x26373a);
    button(x+94,y+88,180,25,"Change looks",me?OPEN:0,DRESS);
    button(x+94,y+120,180,25,"Change name",me?RENAME:0,0);
    button(x+94,y+152,180,25,"Badges / character code",OPEN,IDENTITY);
    if(!strcmp(W.slug,"afterhours")) {
      button(x+8,y+188,144,26,"Character actions",OPEN,ACTIONS);
      button(x+158,y+188,144,26,"Bar",OPEN,BAR);
    } else if(W.room_owner && net_online()) {
      button(x+8,y+188,w-136,26,"Character actions",OPEN,ACTIONS);
      button(x+w-124,y+188,116,26,"Room settings",OPEN,SETTINGS);
    } else button(x+8,y+188,w-16,26,"Character actions",OPEN,ACTIONS);
  } else if(U.panel==DRESS) {
    static const char *names[]={"Face","Hair","Top","Legs","Shoes"};
    for(int i=0;i<5;i++)button(x+8+i*59,y+28,55,23,names[i],CATEGORY,i);
    Category c=category();
    int selected=wrap(*c.style,c.count),start=(selected/3)*3;
    button(x+8,y+92,30,28,"<",STYLE,-1);button(x+272,y+92,30,28,">",STYLE,1);
    for(int i=0;i<3 && start+i<c.count;i++) {
      int index=start+i;
      button(x+43+i*75,y+55,72,106,"",PICK_STYLE,index);
      if(index==selected) {fill_rect(x+46+i*75,y+58,66,100,color(0xbed3d5));}
      *c.style=index;
      avatar_draw(look_encode(&U.look),2,POSE_STD,0,0,2,0,false,x+47+i*75,y+51,1,255);
    }
    *c.style=selected;
    for(int i=0;i<c.colors;i++) {
      float cx=x+8+i*18;
      button(cx,y+165,17,21,"",PICK_COLOR,i);
      fill_rect(cx+3,y+168,11,15,color(c.palette[i]));
      if(i==wrap(*c.tint,c.colors))fill_rect(cx+5,y+187,7,2,color(0x18272a));
    }
    button(x+8,y+192,70,24,"Cancel",CLOSE,0);
    label(c.items[selected].label,x+84,y+198,124,0x18272a);
    button(x+214,y+192,88,24,"Save look",SAVE_LOOK,0);
  } else if(U.panel==HELP) {
#if TWEEBBO_DEVICE == 3
    float hy=y+30;
    client_ui_key_hint("Cross","Choose",x+10,hy,CL_INK,CL_INK);
    client_ui_key_hint("Circle","Close or Rooms",x+146,hy,CL_INK,CL_INK);
    hy+=18;
    client_ui_key_hint("Triangle","Chat",x+10,hy,CL_INK,CL_INK);
    client_ui_key_hint("Square","Wave",x+146,hy,CL_INK,CL_INK);
    hy+=18;
    client_ui_key_hint("L/R","Room zoom",x+10,hy,CL_INK,CL_INK);
    client_ui_key_hint("D-pad","Move",x+146,hy,CL_INK,CL_INK);
    hy+=18;
    label("The left stick and touch also work.",x+10,hy,w-20,CL_INK);
#else
    float hy=y+30;
    client_ui_key_hint("A","Choose",x+10,hy,CL_INK,CL_INK);
    client_ui_key_hint("B","Close or Rooms",x+146,hy,CL_INK,CL_INK);
    hy+=18;
    client_ui_key_hint("Y","Chat",x+10,hy,CL_INK,CL_INK);
    client_ui_key_hint("X","Wave",x+146,hy,CL_INK,CL_INK);
    hy+=18;
    client_ui_key_hint("L/R","Room zoom",x+10,hy,CL_INK,CL_INK);
    client_ui_key_hint("D-pad","Move",x+146,hy,CL_INK,CL_INK);
    hy+=18;
    label(U.tv?"TV mode: every control works with a pad.":"The left stick and touch also work.",x+10,hy,w-20,CL_INK);
#endif
    button(x+8,y+110,w-16,24,net_state()==NET_OFFLINE?RETURN_LABEL:"Play offline",OFFLINE,0);
    UpdateState us=update_state();
    char upd[96];
    if(us==UPD_AVAILABLE)snprintf(upd,sizeof upd,"Install update %s",update_version());
    else if(us==UPD_CHECKING)snprintf(upd,sizeof upd,"Searching for updates...");
    else if(us==UPD_DOWNLOADING)snprintf(upd,sizeof upd,"Updating...");
    else if(us==UPD_INSTALLED)snprintf(upd,sizeof upd,"Finish update");
    else snprintf(upd,sizeof upd,"Search for updates");
    button(x+8,y+138,w-16,24,upd,CHECK_UPDATES,0);
    float third=(w-24)/3;
    button(x+8,y+164,third,24,"Music / sound",OPEN,MUSIC);
    button(x+12+third,y+164,third,24,DONATE_LABEL,OPEN,SUPPORT);
    button(x+16+third*2,y+164,third,24,"Buy chips",OPEN,CHIPS);
    if(net_state()==NET_REPLACED || net_state()==NET_DENIED)button(x+8,y+192,w-16,24,"Retry connection",RESUME,0);
    else if(net_state()!=NET_OFFLINE)button(x+8,y+192,w-16,24,"Return to lobby",LOBBY,0);
  }
  if(U.status[0])label(U.status,margin+8,bar+8,SCREEN_W-16-margin*2,0xffffff);
  else if(client_shop_building()) {
#if TWEEBBO_DEVICE == 3
    float hx=margin+8;
    hx=hint_chip("D-pad","Move",hx,bar+6,0,0);
    hx=hint_chip("Cross","Place",hx,bar+6,0,0);
    hx=hint_chip("Circle","Cancel",hx,bar+6,0,0);
    hint_chip("L/R","Rotate / height",hx,bar+6,0,0);
#else
    float hx=margin+8;
    hx=hint_chip("D-pad","Move",hx,bar+6,0,0);
    hx=hint_chip("A","Place",hx,bar+6,0,0);
    hx=hint_chip("B","Cancel",hx,bar+6,0,0);
    hint_chip("L/R","Rotate / height",hx,bar+6,0,0);
#endif
  } else if(U.panel==ROOMS || (U.panel==SHOP && client_shop_wide())) {
    window_hints(margin+8,bar+6,U.panel==ROOMS);
  } else if(U.panel != KEYBOARD && U.panel != SNOW && U.panel != POOL) {
    classic_tools(margin,bar);
  } else if(U.panel==KEYBOARD) {
#if TWEEBBO_DEVICE == 3
    float hx=margin+8;
    hx=hint_chip("Cross","Type",hx,bar+6,0,0);
    hx=hint_chip("Circle","Back",hx,bar+6,0,0);
    hx=hint_chip("Square","Delete",hx,bar+6,0,0);
    hx=hint_chip("Triangle",U.edit==EDIT_CHAT?"Send":"Save",hx,bar+6,0,0);
    hx=hint_chip("L","Case",hx,bar+6,0,0);
    hint_chip("R","Space",hx,bar+6,0,0);
#else
    float hx=margin+8;
    hx=hint_chip("A","Type",hx,bar+6,0,0);
    hx=hint_chip("B","Back",hx,bar+6,0,0);
    hx=hint_chip("X","Delete",hx,bar+6,0,0);
    hx=hint_chip("Y",U.edit==EDIT_CHAT?"Send":"Save",hx,bar+6,0,0);
    hx=hint_chip("L","Case",hx,bar+6,0,0);
    hint_chip("R","Space",hx,bar+6,0,0);
#endif
  }
  if(U.panel==KEYBOARD && U.keyboard_focus) {
    for(int i=0;i<U.count;i++) if(U.buttons[i].cmd==KEY && U.buttons[i].arg==10) U.focus=i;
    U.keyboard_focus=false;
  }
  if(U.focus>=U.count)U.focus=0;
}
bool client_ui_action(int action) {
  if(net_state()==NET_REPLACED){if(action==SHOW_A)activate(RESUME,0);return true;}
  if(U.alert){if(action==SHOW_A || action==SHOW_B)activate(ALERT_CLOSE,0);return true;}
  if(U.panel==CHAT && (action==SHOW_L || action==SHOW_R)) {
    activate(PAGE,action==SHOW_L?1:-1); return true;
  }
  if(U.panel==POOL&&pool_key(action))return true;
  if(U.panel==SNOW && client_snow_key(action))return true;
  if(U.panel==SNOW && action==SHOW_Y)return true;
  if(U.panel==SHOP && client_shop_key(action))return true;
  if(U.panel==KEYBOARD) {
    if(action==SHOW_X) { activate(BACKSPACE,0);return true; }
    if(action==SHOW_Y) { activate(SUBMIT,0);return true; }
    if(action==SHOW_L) { activate(SHIFT,0);return true; }
    if(action==SHOW_R) { activate(SPACE,0);return true; }
  }
  if(U.panel==ROOMS && (action==SHOW_L || action==SHOW_R || action==SHOW_ZL || action==SHOW_ZR)) { activate(PAGE,action==SHOW_L||action==SHOW_ZL?-1:1); return true; }
  if(!U.panel && (action==SHOW_ZL || action==SHOW_ZR)) { open_panel(MORE); return true; }
  if(action==SHOW_Y) { if(U.panel!=KEYBOARD){open_panel(CHAT);edit_text(EDIT_CHAT);} return true; }
  if(action==SHOW_B) { if(U.panel)activate(CLOSE,0);else open_panel(ROOMS);return true; }
  if(!U.panel)return false;
  if(action==SHOW_A && U.count) { Button b=U.buttons[U.focus];activate(b.cmd,b.arg); }
  return action!=SHOW_OFFLINE;
}
bool client_ui_move(int dx,int dy) {
  if(net_state()==NET_REPLACED || U.alert)return true;
  if(U.panel==POOL&&pool_move(dx,dy))return true;
  if(U.panel==SNOW && client_snow_move(dx,dy))return true;
  if(U.panel==SHOP && client_shop_move(dx,dy))return true;
  if(!U.panel)return false;
  if(!U.count)return true;
  Button a=U.buttons[U.focus]; float best=1e9;int target=U.focus;
  for(int i=0;i<U.count;i++) {
    Button b=U.buttons[i]; float x=b.x+b.w/2-a.x-a.w/2,y=b.y+b.h/2-a.y-a.h/2;
    float forward=dx?x*dx:y*dy,side=dx?fabsf(y):fabsf(x);
    if(forward<=1)continue;
    float score=forward+side*4;
    if(score<best){best=score;target=i;}
  }
  U.focus=target;return true;
}
bool client_ui_tap(float x,float y) {
  for(int i=0;i<U.count;i++) {
    Button b=U.buttons[i];
    if(x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h){U.focus=i;activate(b.cmd,b.arg);return true;}
  }
  if(U.chip_w>0 && x>=U.chip_x && x<U.chip_x+U.chip_w && y>=U.chip_y && y<U.chip_y+U.chip_h){open_panel(CHIPS);return true;}
  if(U.panel==SNOW && client_snow_tap(x,y))return true;
  if(U.panel==SHOP && client_shop_building() && x>=client_ui_room_left() && y<SCREEN_H-32-(U.tv?16:0))return false;
  return U.panel || y>=SCREEN_H-32-(U.tv?16:0);
}
