// Update notice: after connecting, the console reads /static/consoles/latest.json from the
// hotel and, when a newer version is published for this console, offers it at login with a
// QR code to the download page (homebrew cannot update itself safely on every system).
#pragma once
#include "common.h"

void update_poll(u64 now);
/** A newer version waits: its version text, else NULL. */
const char *update_available(void);
const char *update_url(void);
/** The player chose Later: stop offering it this session. */
void update_dismiss(void);
bool update_dismissed(void);
