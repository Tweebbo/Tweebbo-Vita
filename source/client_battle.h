#pragma once
#include "battle_panel.h"
void client_battle_action(int command,int argument);
bool client_battle_key(int key);
bool client_battle_move(int dx,int dy);
bool client_battle_tap(float x,float y);
void client_battle_draw(float x,float y,float width);
void client_battle_hud(void);
