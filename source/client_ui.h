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
/** Enabled classic capsule button drawn as a hint only (no hotspot). */
void client_ui_capsule(float x, float y, float w, float h, const char *text);
/** Draws a pad-button glyph and its label; returns the x after the hint. */
float client_ui_key_hint(const char *key, const char *text, float x, float y, u32 ink, u32 text_ink);
void client_ui_label(const char *text, float x, float y, float width, u32 ink);
void client_ui_changed(void);
void client_ui_inspect(u32 item);
void client_ui_close(void);
void client_ui_casino(void);
/* Return false to keep the draft open when a request could not be queued. */
void client_ui_edit(const char *title, const char *initial, bool (*submit)(const char *));
void client_ui_edit_draft(const char *title,const char *initial,bool (*submit)(const char *),void (*cancel)(const char *));
size_t client_ui_paragraph(const char *text, float x, float y, float width, int lines, u32 ink);

void client_ui_bar(void);

void client_ui_edit_secret(const char *title,bool (*submit)(const char *));

void client_ui_music(void);
