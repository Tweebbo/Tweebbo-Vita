#pragma once
#include "common.h"
#include "world.h"
void client_bar_reset(void);
void client_bar_open(void);
void client_bar_update(void);
bool client_bar_receive(const char *json,u64 now);
int client_bar_sip(const Ent *e,u64 now,bool moving);
bool client_bar_context(char *label,int cap,u64 now);
void client_bar_draw(float x,float y,float w);
void client_bar_action(int command,int arg);
