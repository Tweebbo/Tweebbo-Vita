#pragma once
#include "common.h"

void client_ui_init(void);
void client_ui_update(void);
void client_ui_draw(void);
bool client_ui_action(int action);
bool client_ui_move(int dx, int dy);
bool client_ui_tap(float x, float y);
void client_ui_chat(u32 eid, const char *text);
void client_ui_room(void);
void client_ui_reset(void);
/** TV safe margins; changing mode preserves the current panel and draft. */
void client_ui_set_tv(bool enabled);
/** Left edge of the room area not covered by the current panel. */
float client_ui_room_left(void);
/* Shared Classic controls; feature modules reserve commands from 1000 onward. */
void client_ui_button(float x, float y, float w, float h, const char *text, int cmd, int arg);
/** Classic inset text field (white over a grey rim), with a placeholder when empty. */
void client_ui_field(float x, float y, float w, float h, const char *text, const char *placeholder, int cmd, int arg);
/** Original 9-slice window frame with its centred title, without registering a close box. */
void client_ui_window(float x, float y, float w, float h, const char *title);
/** Original content inset (white body, classic blue rim) for floating hints and notices. */
void client_ui_panel(float x, float y, float w, float h);
/** Enabled classic capsule button drawn as a hint only (no hotspot). */
void client_ui_capsule(float x, float y, float w, float h, const char *text);
/** Draws a pad-button glyph and its label; returns the x after the hint. */
float client_ui_key_hint(const char *key, const char *text, float x, float y, u32 ink, u32 text_ink);
/** The room's contextual prompt, drawn as a classic button.b capsule ending at `right`. */
void client_ui_prompt(const char *key, const char *text, float right, float y);
void client_ui_label(const char *text, float x, float y, float width, u32 ink);
/** Registers a pad/touch hotspot without drawing it; returns whether it has the pad focus. */
bool client_ui_hot(float x, float y, float w, float h, int cmd, int arg);
/** Classic graphic (ui_sprites) shrunk to fit a `box` square centred on (cx, cy). */
bool client_ui_icon_fit(const char *name, float cx, float cy, float box);
/** Classic graphic at `scale` with its top-left at (x, y); returns its drawn width, 0 if absent. */
float client_ui_sprite(const char *name, float x, float y, float scale);
/** 0xRRGGBB as an opaque renderer colour. */
u32 client_ui_rgb(u32 c);
/** Small classic list/page arrow button ("<", ">", "^", "v"). */
void client_ui_arrow(float x, float y, float w, float h, const char *glyph, int cmd, int arg);
void client_ui_changed(void);
void client_ui_inspect(u32 item);
void client_ui_close(void);
void client_ui_alert(const char *json);
void client_ui_casino(void);
/* Return false to keep the draft open when a request could not be queued. */
void client_ui_edit(const char *title, const char *initial, bool (*submit)(const char *));
void client_ui_edit_draft(const char *title,const char *initial,bool (*submit)(const char *),void (*cancel)(const char *));
size_t client_ui_paragraph(const char *text, float x, float y, float width, int lines, u32 ink);

void client_ui_bar(void);

void client_ui_edit_secret(const char *title,bool (*submit)(const char *));

void client_ui_music(void);
void client_ui_camera(void);
void client_ui_feedback(void);
void client_ui_person(u32 id);

void client_ui_support(void);
