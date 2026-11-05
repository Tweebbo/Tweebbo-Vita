#pragma once
#include <stdbool.h>
#include <vita2d.h>
#include "gfx_highlight.h"
bool vita_highlight_init(void);
void vita_highlight_free(void);
bool vita_highlight_draw(vita2d_texture *texture,float x,float y,int sx,int sy,int sw,int sh,float scale_x,float scale_y,unsigned color,bool additive,const GfxHighlightMatrix *matrix);
