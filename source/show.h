// Shared room renderer and input routing for Switch and Vita.
#pragma once
#include "common.h"

/** Set by the platform main (used by the banner and the quick chat). */
void show_set_platform(const char *name, const char *hello);
/** The console's name ("Nintendo Switch", "PlayStation Vita"). */
const char *show_platform(void);
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
/** Pixel-art badge of a native device, for the player panel (1 3DS, 2 Switch, 3 Vita). */
void show_device_badge(int device, float x, float y, float k);

/** SHOW_ZL/SHOW_ZR: Switch ZL/ZR, Vita right stick up/down (page and section keys of the windows). */
enum { SHOW_A = 1, SHOW_B, SHOW_X, SHOW_Y, SHOW_L, SHOW_R, SHOW_OFFLINE, SHOW_ZL, SHOW_ZR };
/** A confirms, B closes/opens Rooms, X waves, Y writes chat, L/R zoom. */
void show_action(int action);
/** One step in a direction (dx and dy in -1..1). */
void show_walk(int dx, int dy);
/** Canvas tap: walks to the tapped tile (touch or pointer). */
void show_tap(float x, float y);
/** Plus / START pressed: true on the second press within 3 s, else asks for it with a toast. */
bool show_confirm_quit(const char *key, u64 now);
/** START / Plus: the platform loop should end. */
bool show_quit_requested(void);

float show_zoom(void);
void show_to_screen(float wx,float wy,float *x,float *y);
