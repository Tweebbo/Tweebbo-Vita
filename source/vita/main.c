// Tweebbo for PlayStation Vita: showcase proof of concept.
// One 960×544 screen at a 2× integer canvas, vita2d renderer, SceNet sockets with mbedTLS;
// plays in the same room as the web, the 3DS and the Switch (protocol v6 + device byte).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>

#include "common.h"
#include "gfx.h"
#include "net.h"
#include "show.h"
#include "world.h"

#define DATA_DIR "ux0:data/Tweebbo"

u64 now_ms(void) { return sceKernelGetProcessTimeWide() / 1000; }
int rnd_range(int lo, int hi) { return hi <= lo ? lo : lo + (int)((u32)rand() % (u32)(hi - lo)); }

bool store_log_available(void) { return true; }
void log_start(void) {}

static void data_mkdir(void) {
  static bool done;
  if (done) return;
  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir(DATA_DIR, 0777);
  done = true;
}

void dbg_log(const char *fmt, ...) {
  data_mkdir();
  FILE *f = fopen(DATA_DIR "/log.txt", "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

// Settings and per-server token on the memory card (never shown on screen).
static void store_path(const char *key, char *path, int cap) {
  char safe[96];
  int n = 0;
  for (const char *k = key; *k && n < (int)sizeof safe - 1; k++)
    safe[n++] = (*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') || (*k >= '0' && *k <= '9') || *k == '-' || *k == '.' ? *k : '_';
  safe[n] = 0;
  snprintf(path, (size_t)cap, DATA_DIR "/%s.txt", safe);
}

bool store_load(const char *key, char *out, int cap) {
  char path[160];
  store_path(key, path, sizeof path);
  FILE *f = fopen(path, "r");
  if (!f) return false;
  int n = (int)fread(out, 1, (size_t)cap - 1, f);
  fclose(f);
  out[n > 0 ? n : 0] = 0;
  out[strcspn(out, "\r\n")] = 0;
  return true;
}

void store_save(const char *key, const char *value) {
  data_mkdir();
  char path[160];
  store_path(key, path, sizeof path);
  FILE *f = fopen(path, "w");
  if (!f) return;
  fputs(value, f);
  fclose(f);
}

/** Left stick, D-pad and buttons. One walk step per press with a short repeat. */
static void input(SceCtrlData *pad, unsigned prev, bool *running) {
  static u64 next;
  u64 now = now_ms();
  unsigned down = pad->buttons & ~prev;

  if (down & SCE_CTRL_CROSS) show_action(SHOW_A);
  if (down & SCE_CTRL_CIRCLE) show_action(SHOW_B);
  if (down & SCE_CTRL_SQUARE) show_action(SHOW_X);
  if (down & SCE_CTRL_TRIANGLE) show_action(SHOW_Y);
  if (down & SCE_CTRL_LTRIGGER) show_action(SHOW_L);
  if (down & SCE_CTRL_RTRIGGER) show_action(SHOW_R);
  if (down & SCE_CTRL_SELECT) show_action(SHOW_OFFLINE);
  if (down & SCE_CTRL_START) *running = false;

  if (now < next) return;
  int dx = 0, dy = 0;
  if (pad->buttons & SCE_CTRL_UP) dy = -1;
  else if (pad->buttons & SCE_CTRL_DOWN) dy = 1;
  else if (pad->buttons & SCE_CTRL_LEFT) dx = -1;
  else if (pad->buttons & SCE_CTRL_RIGHT) dx = 1;
  else if (pad->lx > 128 + 48) dx = 1;
  else if (pad->lx < 128 - 48) dx = -1;
  else if (pad->ly > 128 + 48) dy = 1;
  else if (pad->ly < 128 - 48) dy = -1;
  if (!dx && !dy) return;
  show_walk(dx, dy);
  next = now + 180;
}

int main(void) {
  data_mkdir();
  log_start();
  dbg_log("Tweebbo Vita showcase starting (v%s)", APP_VERSION);
  srand((unsigned)sceKernelGetProcessTimeWide());
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

  if (!gfx_init()) {
    dbg_log("gfx_init failed");
    sceKernelExitProcess(1);
    return 1;
  }
  show_set_platform("PlayStation Vita", "Hello from PlayStation Vita!");
  char neterr[128];
  conn_global_init(neterr, sizeof neterr);
  net_start(now_ms());
  show_start(now_ms());

  bool running = true;
  unsigned prev = 0;
  while (running) {
    SceCtrlData pad;
    memset(&pad, 0, sizeof pad);
    sceCtrlPeekBufferPositive(0, &pad, 1);
    input(&pad, prev, &running);
    prev = pad.buttons;

    SceTouchData touch;
    memset(&touch, 0, sizeof touch);
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0 && touch.reportNum > 0)
      show_tap(touch.report[0].x / 4.0f, touch.report[0].y / 4.0f); /* 1920×1088 → 480×272 canvas */

    u64 now = now_ms();
    net_update(now);
    gfx_begin();
    show_frame(now_ms());
    gfx_present();
    if (show_quit_requested()) running = false;
  }

  net_stop();
  conn_global_exit();
  gfx_exit();
  sceKernelExitProcess(0);
  return 0;
}
