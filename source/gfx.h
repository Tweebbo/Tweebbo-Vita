// Platform graphics contract for the Tweebbo Switch/Vita showcase clients.
// The portable core (avatar.c, show.c, text.c) draws through this API; each
// platform provides it over SDL2 (Switch) or vita2d (Vita).
// Colors are ABGR (0xAABBGGRR), like citro2d and vita2d.
#pragma once
#include "assets.h"

/** Logical canvas of the showcase (the platform scales it by a whole factor). */
#ifndef SCREEN_W
#define SCREEN_W 640
#endif
#ifndef SCREEN_H
#define SCREEN_H 360
#endif

bool gfx_init(void);
void gfx_exit(void);
/** Frame brackets: clear and present (the platform owns the renderer). */
void gfx_begin(void);
void gfx_present(void);
/** Call once per frame: frees cached textures over budget. */
void gfx_frame(void);
/** Region of an atlas page drawn at (x, y). `flip` mirrors horizontally in place. */
void gfx_sprite(int page, int sx, int sy, int sw, int sh, float x, float y, float scale, bool flip, u32 color);
/** Like gfx_sprite with separate x and y scales. */
void gfx_sprite_xy(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color);
/** Second pass in a solid color with alpha (teal water veil, like source-atop on the web). */
void gfx_sprite_solid(int page, int sx, int sy, int sw, int sh, float x, float y, float kx, float ky, bool flip, u32 color);
/** Glyph sprite of the bitmap font (romfs gfx/font0.png). */
void gfx_font_sprite(int sx, int sy, int sw, int sh, float x, float y, bool flip, u32 color);
/** Furniture layer: loads its texture from romfs furni/ on demand (LRU cache). */
void gfx_furni(u16 def, const FurniPart *part, float x, float y, float scale, u32 color);
/** Preloads a furniture texture (when entering a room). */
void gfx_furni_preload(u16 def);
void gfx_pet_preload(int palette);
void gfx_pet(int palette,const PetPart *part,float x,float y,float scale,bool flip,u32 color,bool solid);
/** Pre-rendered background of a classic room; NULL if not packed. */
const RoomBackground *gfx_room(const char *model);
void gfx_room_release(void);
void gfx_room_draw(const RoomBackground *bg, float x, float y, float scale);
/** Layered public room (park/pool); NULL if it has none. */
const Scene *gfx_scene(const char *model);
void gfx_scene_sprite(const SceneSprite *s, float x, float y, float zoom, u8 alpha);
void gfx_filter(bool linear);
void gfx_additive(bool on);
bool gfx_snow_load(void);
void gfx_snow_free(void);
void gfx_snow_sprite(unsigned id,float x,float y,float kx,float ky,u32 color,float left,float top,float right,float bottom);

void fill_rect(float x, float y, float w, float h, u32 color);
void fill_round(float x, float y, float w, float h, float r, u32 color);
void fill_circle(float cx, float cy, float r, u32 color);
void fill_triangle(float x1, float y1, float x2, float y2, float x3, float y3, u32 color);
void draw_line(float x1, float y1, float x2, float y2, u32 color);

// Text with the packed Noto Sans bitmap atlas (integer sizes 10–16).
void text_begin(void);
/** Draws and returns the width in pixels. `align`: 0 left, 1 center, 2 right. */
float text_draw(const char *s, float x, float y, float size, u32 color, int align);
float text_width(const char *s, float size);
/** Cuts `s` with "…" so it does not exceed `max` pixels. */
void text_fit(char *s, float size, float max);

const SceneArt *gfx_art(const char *name);
void gfx_art_draw(const SceneArt *a,float x,float y,float kx,float ky,u32 color);
void gfx_sprite_transform(float x,float y,float kx,float ky);
