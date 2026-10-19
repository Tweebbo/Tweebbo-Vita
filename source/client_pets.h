#pragma once
#include "common.h"
void client_pets_reset(void);
void client_pets_open(void);
void client_pets_update(void);
void client_pets_draw(float x,float y,float w);
void client_pets_action(int command,int arg);
void client_pets_notice(const char *text);
