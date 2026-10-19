#pragma once
#include "common.h"
void client_music_reset(void);
void client_music_receive(const char *json,u64 now);
void client_music_update(void);
void client_music_draw(float x,float y,float w);
void client_music_action(int command,int arg);
bool client_music_context(char *label,int cap,u64 now);
