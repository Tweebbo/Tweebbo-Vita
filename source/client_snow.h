#pragma once
#include "common.h"
void client_snow_open(void);
void client_snow_reset(void);
void client_snow_update(void);
void client_snow_receive(const u8 *data,int len);
bool client_snow_scene(u64 now);
bool client_snow_playing(void);
bool client_snow_key(int action);
bool client_snow_move(int dx,int dy);
void client_snow_close(void);
void client_snow_draw(float x,float y,float w);
void client_snow_hud(void);
void client_snow_action(int command,int arg);
bool client_snow_tap(float x,float y);
