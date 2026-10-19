#pragma once
#include "common.h"
void client_trade_reset(void);
void client_trade_update(bool visible);
void client_trade_draw(float x,float y,float w);
void client_trade_action(int command,int arg);
void client_trade_invite(u32 target);
void client_trade_close(void);
void client_trade_notice(const char *text);
