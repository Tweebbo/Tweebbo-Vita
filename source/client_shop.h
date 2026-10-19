#pragma once
#include "common.h"

enum { SHOP_CATALOGUE, SHOP_INVENTORY, SHOP_ROOM };
void client_shop_open(int section);
void client_shop_inspect(u32 item);
void client_shop_reset(void);
void client_shop_update(void);
void client_shop_visible(bool visible);
const char *client_shop_wall(void);
const char *client_shop_floor(void);
void client_shop_draw(float x, float y, float w);
void client_shop_action(int command, int arg);
void client_shop_notice(const char *text);
void client_shop_item(u32 id);
bool client_shop_building(void);
bool client_shop_move(int dx, int dy);
bool client_shop_key(int action);
void client_shop_tile(int x, int y);
typedef struct { bool on, wall, ok; u16 def; int x, y, rot, z; u32 ignore; } BuildGhost;
const BuildGhost *client_shop_ghost(void);
