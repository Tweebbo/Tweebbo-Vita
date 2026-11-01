#pragma once
#include "client_shop.h"

bool client_build_valid(BuildGhost *g);
void client_build_wall(BuildGhost *g, int step, bool nearest);
/** Keep a wall item where the whole sprite is on the wall face (Kepler local y). */
void client_build_wall_clamp(BuildGhost *g);
