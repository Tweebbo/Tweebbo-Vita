#pragma once
#include "common.h"
void client_casino_reset(void);
void client_casino_open(u32 item);
void client_casino_receive(const char *json);
void client_casino_update(void);
const char *client_casino_title(void);
void client_casino_draw(float x,float y,float w);
void client_casino_action(int command,int arg);
