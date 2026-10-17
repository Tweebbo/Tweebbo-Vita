// Showcase application shared by the Switch and Vita clients: isometric room with the
// classic Kepler dress, chat bubbles, console badges, the five rotating brand models
// and a small classic interface. Platform mains feed input through show_action().
#pragma once
#include "common.h"

/** Set by the platform main (used by the banner and the quick chat). */
void show_set_platform(const char *name, const char *hello);
/** Name of the button that toggles the offline simulation, for the on-screen hints. */
void show_set_offline_key(const char *key);

void show_start(u64 now);
void show_frame(u64 now);
/** Room entered (world.c on_room): picks the background and clears the bubbles. */
void show_room_changed(u64 now);
/** Chat bubble over a player (world.c on_chat). */
void show_chat(u32 eid, const char *text, u64 now);
/** Short notice pill (hotel alerts and server notices). */
void show_toast(const char *text, u64 now);

enum { SHOW_A = 1, SHOW_B, SHOW_X, SHOW_Y, SHOW_L, SHOW_R, SHOW_OFFLINE };
/** Button actions (A contextual, B next model, X wave, Y quick chat, L/R zoom). */
void show_action(int action);
/** One step in a direction (dx and dy in -1..1). */
void show_walk(int dx, int dy);
/** Canvas tap: walks to the tapped tile (touch or pointer). */
void show_tap(float x, float y);
/** START / Plus: the platform loop should end. */
bool show_quit_requested(void);
