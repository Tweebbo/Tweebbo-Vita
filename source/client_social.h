#pragma once
#include "common.h"
void client_social_reset(void);
void client_social_open(void);
void client_social_push(void);
void client_social_update(bool visible);
void client_social_draw(float x,float y,float w);
void client_social_action(int command,int arg);
bool client_social_blocked(u32 guest);
