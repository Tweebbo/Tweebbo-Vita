#pragma once
#include "common.h"
void client_identity_open(u32 guest);
void client_identity_reset(void);
void client_identity_update(void);
void client_identity_draw(float x,float y,float w);
void client_identity_action(int command,int arg);
void client_identity_close(void);
