// In-game updates: the console reads the latest GitHub release, offers it after login and
// installs it (NRO in place on Switch, VPK saved for VitaShell on Vita). Downloads resume and
// are verified with the release asset's SHA-256.
#pragma once
#include "common.h"

typedef enum { UPD_IDLE, UPD_CHECKING, UPD_LATEST, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_INSTALLED, UPD_FAILED } UpdateState;

/** Background check: at login and every 15 minutes while the hotel is online. */
void update_poll(u64 now);
/** Manual check (Help > Check for updates). Re-shows the offer after a previous Later. */
void update_check(void);
/** Background check that never replaces a previous answer with a transient error. */
void update_check_quiet(void);
UpdateState update_state(void);
const char *update_version(void);
/** The running version, as compared with the release tag. */
const char *update_current(void);
const char *update_notes(void);
const char *update_error(void);
/** Where the verified download was saved (Vita: the VPK to install with VitaShell). */
const char *update_installed_path(void);
/** 0–1 while downloading. */
float update_progress(void);
/** Downloads and installs the offered release. */
void update_start(void);
/** Switch: asks the main loop to exit so the new NRO is loaded next time. Vita: false. */
bool update_restart(void);
bool update_exit_requested(void);
void update_dismiss(void);
bool update_dismissed(void);
/** Increases whenever a different release is found (re-shows a dismissed offer). */
int update_offer(void);
void update_forget_dismiss(void);
/** Compares "a.b.c" versions: >0 if a is newer. */
int version_cmp(const char *a, const char *b);
/** Path of the running NRO (empty on Vita). */
void update_set_launch_path(const char *argv0);
