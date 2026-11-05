/* Panel-routing fixture dependencies. Real game engines, camera capture and
 * network services are exercised by the host and emulator integration suites. */
#include "client_fastfood.h"
#include "client_battle.h"
#include "client_camera.h"
#include "fastfood.h"
#include "feedback.h"
#include "doorbell.h"
#include "language.h"
#include "net.h"

static bool food_open, ball_open;
bool gfx_language_font(int language) { return language >= 0 && language < TW_LANGUAGE_COUNT; }
bool net_doorbell(void) { return false; }
FeedbackDraft feedback;
void feedback_poll(void) {}
void feedback_send(void) {}
bool feedback_receptionist(u32 guest, const char *model) { return false; }
DoorbellState doorbell;
void doorbell_poll(void) {}
void doorbell_ring(void) {}
void doorbell_answer(bool allow) {}
void doorbell_set(bool enabled) { doorbell.enabled = enabled; }
void client_camera_update(void) {}
void typing_input(const char *text) { (void)text; }
void client_camera_action(int cmd, int arg) {}
void client_camera_draw(float x, float y, float w, float h) {}
bool client_camera_capturing(void) { return false; }
void fastfood_open(const char *name, u64 look) { food_open = true; }
void fastfood_close(void) { food_open = false; }
bool fastfood_active(void) { return food_open; }
void fastfood_update(u64 now) {}
void client_fastfood_draw(float x, float y, float width) {}
void client_fastfood_hud(void) {}
bool client_fastfood_key(int key) { return false; }
void client_fastfood_action(int command, int argument) {}
bool client_fastfood_playing(void) { return false; }
void battle_open(u32 player) { ball_open = true; }
void battle_close(void) { ball_open = false; }
bool battle_active(void) { return ball_open; }
bool battle_playing(void) { return false; }
void battle_update(u64 now) {}
void battle_panel_reset(void) {}
void client_battle_action(int command, int argument) {}
bool client_battle_key(int key) { return false; }
bool client_battle_move(int dx, int dy) { return false; }
bool client_battle_tap(float x, float y) { return false; }
void client_battle_draw(float x, float y, float width) {}
void client_battle_hud(void) {}
