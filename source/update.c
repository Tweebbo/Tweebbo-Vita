// In-game updates (see update.h). The check and the download run outside the render thread;
// the UI only reads the state. GitHub publishes the digest of every release asset, so a
// downloaded build is installed only after its SHA-256 matches.
#include "update.h"
#include "net.h"
#include "ws.h"
#include <jansson.h>
#include <mbedtls/sha256.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#if TWEEBBO_DEVICE == 3
#define UPDATE_REPO "Tweebbo/Tweebbo-Vita"
#define UPDATE_ASSET "tweebbo.vpk"
#else
#define UPDATE_REPO "Tweebbo/TweebboNX"
#define UPDATE_ASSET "tweebbo.nro"
#endif

#if defined(__vita__)
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#define UPDATE_DATA_DIR "ux0:data/Tweebbo"
static void update_mkdir(const char *path) { sceIoMkdir(path, 0777); }
static void wait_ms(int ms) { sceKernelDelayThread((SceUInt)ms * 1000); }
#else
#include <sys/stat.h>
#include <unistd.h>
#ifdef TWEEBBO_HOST
#define UPDATE_DATA_DIR "update"
#else
#define UPDATE_DATA_DIR "sdmc:/switch/Tweebbo"
#endif
static void update_mkdir(const char *path) { mkdir(path, 0777); }
static void wait_ms(int ms) { usleep((useconds_t)ms * 1000); }
#endif

#ifndef UPDATE_API
#define UPDATE_API "https://api.github.com/repos/" UPDATE_REPO "/releases/latest"
#endif

#ifdef __vita__
#define UPDATE_VITA 1
#else
#define UPDATE_VITA 0
#endif

static const char *manifest_url(void) {
#ifdef TWEEBBO_HOST
  const char *override = getenv("TWEEBBO_UPDATE_MANIFEST");
  if (override && *override) return override;
#endif
  return UPDATE_API;
}

#define CHECK_MS (15 * 60 * 1000)
#define CHECK_INITIAL_DELAY 4000
#define DOWNLOAD_ATTEMPTS 8
#define CHUNK (32 * 1024)

static struct {
  volatile UpdateState state;
  volatile int offers;
  volatile long size, done;
  char version[24], notes[600], error[180], url[1024], sha[72], installed[300], launch[300];
  bool dismissed, exit_requested;
  volatile bool thread_active;
  u64 last_check;
} U;

static void copy_text(char *dst, size_t cap, const char *src) {
  if (!src) src = "";
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

int version_cmp(const char *a, const char *b) {
  int x[3] = {0}, y[3] = {0};
  sscanf(a ? a : "", "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b ? b : "", "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] - y[i];
  return 0;
}

static bool is_sha256(const char *s) {
  if (!s || strlen(s) != 64) return false;
  for (; *s; s++) if (!strchr("0123456789abcdefABCDEF", *s)) return false;
  return true;
}

static bool file_text(const char *path, char *out, int cap) {
  FILE *f = fopen(path, "r");
  if (!f) return false;
  bool ok = fgets(out, cap, f) != NULL;
  fclose(f);
  return ok;
}

static void file_write(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  if (!f) return;
  fputs(text, f);
  fclose(f);
}

static void forget_file(const char *path) {
  char side[340];
  remove(path);
  snprintf(side, sizeof side, "%s.sha", path);
  remove(side);
}

/* The Vita SDK ships an older mbedTLS without the _ret SHA-256 helpers. */
static void sha_start(mbedtls_sha256_context *ctx) {
#if UPDATE_VITA
  mbedtls_sha256_starts(ctx, 0);
#else
  mbedtls_sha256_starts_ret(ctx, 0);
#endif
}
static void sha_update(mbedtls_sha256_context *ctx, const u8 *data, size_t len) {
#if UPDATE_VITA
  mbedtls_sha256_update(ctx, data, len);
#else
  mbedtls_sha256_update_ret(ctx, data, len);
#endif
}
static void sha_finish(mbedtls_sha256_context *ctx, u8 digest[32]) {
#if UPDATE_VITA
  mbedtls_sha256_finish(ctx, digest);
#else
  mbedtls_sha256_finish_ret(ctx, digest);
#endif
}

typedef struct { char *buf; int len, cap; } Mem;

static bool to_memory(const u8 *data, int len, void *ctx) {
  Mem *m = ctx;
  if (m->len + len >= m->cap) return false;
  memcpy(m->buf + m->len, data, (size_t)len);
  m->len += len;
  m->buf[m->len] = 0;
  return true;
}

/** Reads the latest GitHub release. Returns AVAILABLE, LATEST or FAILED and fills the offer. */
static UpdateState read_manifest(void) {
  Mem m = {malloc(65536), 0, 65536};
  long total = 0;
  char err[128] = "";
  int status = 0;
  if (m.buf) status = http_download(manifest_url(), to_memory, &m, &total, err, sizeof err);
  if (!m.buf) status = 0;
  if (status == 404) {
    free(m.buf);
    copy_text(U.error, sizeof U.error, "No updates published yet.");
    return UPD_LATEST;
  }
  if (status != 200) {
    free(m.buf);
    if (status == 403 || status == 429) copy_text(U.error, sizeof U.error, "GitHub is limiting update checks. Try again later.");
    else copy_text(U.error, sizeof U.error, err[0] ? err : "Could not read the update list.");
    dbg_log("update check: HTTP %d %s", status, U.error);
    return UPD_FAILED;
  }
  json_error_t jerr;
  json_t *j = json_loads(m.buf, 0, &jerr);
  free(m.buf);
  if (!j) {
    copy_text(U.error, sizeof U.error, "The update list is not readable.");
    return UPD_FAILED;
  }
  const char *tag = json_string_value(json_object_get(j, "tag_name"));
  const char *body = json_string_value(json_object_get(j, "body"));
  const char *version = tag ? tag : "";
  while (*version == 'v' || *version == 'V') version++;
  const char *url = NULL, *sha = NULL;
  long size = 0;
  json_t *assets = json_object_get(j, "assets"), *asset;
  size_t i;
  json_array_foreach(assets, i, asset) {
    const char *name = json_string_value(json_object_get(asset, "name"));
    if (!name || strcmp(name, UPDATE_ASSET)) continue;
    url = json_string_value(json_object_get(asset, "browser_download_url"));
    size = (long)json_integer_value(json_object_get(asset, "size"));
    sha = json_string_value(json_object_get(asset, "digest"));
    if (!sha) sha = json_string_value(json_object_get(asset, "sha256"));
    break;
  }
  if (!sha) sha = json_string_value(json_object_get(j, "sha256"));
  if (sha && !strncmp(sha, "sha256:", 7)) sha += 7;
  bool newer = version_cmp(version, APP_VERSION) > 0;
  bool complete = url && *url && is_sha256(sha) && size > 0 && strlen(url) < sizeof U.url;
  if (!newer) {
    U.version[0] = 0;
    U.notes[0] = 0;
    json_decref(j);
    return UPD_LATEST;
  }
  if (!complete) {
    copy_text(U.error, sizeof U.error, "The update list is incomplete. Try again in a minute.");
    dbg_log("update check: incomplete release %s", version);
    json_decref(j);
    return UPD_FAILED;
  }
  if (strcmp(U.version, version) || strcmp(U.sha, sha)) U.offers++;
  copy_text(U.version, sizeof U.version, version);
  copy_text(U.url, sizeof U.url, url);
  copy_text(U.sha, sizeof U.sha, sha);
  copy_text(U.notes, sizeof U.notes, body ? body : "");
  U.size = size;
  U.error[0] = 0;
  dbg_log("update check: running %s, latest %s (%ld bytes)", APP_VERSION, U.version, size);
  json_decref(j);
  return UPD_AVAILABLE;
}

static void *check_thread(void *arg) {
  bool quiet = arg != NULL;
  UpdateState st = read_manifest();
  // A background check that fails keeps the previous answer instead of showing an error.
  if (quiet && st == UPD_FAILED) st = U.version[0] ? UPD_AVAILABLE : UPD_LATEST;
  U.state = st;
  U.thread_active = false;
  return NULL;
}

static bool start_thread(void *(*fn)(void *), void *arg) {
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 512 * 1024);
  int rc = pthread_create(&thread, &attr, fn, arg);
  pthread_attr_destroy(&attr);
  if (rc == 0) pthread_detach(thread);
  return rc == 0;
}

static void start_check(bool quiet) {
  if (U.state == UPD_CHECKING || U.state == UPD_DOWNLOADING || U.state == UPD_INSTALLED) return;
  if (U.thread_active) return;
  if (!net_online()) {
    if (!quiet) {
      copy_text(U.error, sizeof U.error, "Connect to the hotel to search for updates.");
      U.state = UPD_FAILED;
    }
    return;
  }
  if (!quiet) U.dismissed = false;
  U.error[0] = 0;
  U.state = UPD_CHECKING;
  U.last_check = now_ms();
  U.thread_active = true;
  if (!start_thread(check_thread, quiet ? (void *)1 : NULL)) {
    U.thread_active = false;
    copy_text(U.error, sizeof U.error, "Could not start the update check.");
    U.state = UPD_FAILED;
  }
}

void update_check(void) { start_check(false); }
void update_check_quiet(void) { start_check(true); }

void update_poll(u64 now) {
  if (!net_online()) return;
  UpdateState st = U.state;
  if (st == UPD_IDLE) {
    // Give the hotel handshake a moment, then check once.
    if (!U.last_check) U.last_check = now;
    else if (now - U.last_check >= CHECK_INITIAL_DELAY && !U.thread_active) start_check(true);
    return;
  }
  if ((st == UPD_LATEST || st == UPD_FAILED) &&
      !U.thread_active && now - U.last_check >= CHECK_MS) start_check(true);
}

// ---------------------------------------------------------------- download and install

typedef struct {
  FILE *file;
  mbedtls_sha256_context sha;
} Sink;

static bool file_sink(const u8 *data, int len, void *ctx) {
  Sink *s = ctx;
  if (fwrite(data, 1, (size_t)len, s->file) != (size_t)len) return false;
  sha_update(&s->sha, data, (size_t)len);
  U.done += len;
  return true;
}

/** Downloads U.url into `path`, resuming an earlier attempt, and checks U.sha. */
static bool download_file(const char *path, char *err, int cap) {
  char side[340], had[80] = "";
  snprintf(side, sizeof side, "%s.sha", path);
  // A partial file of another build is useless: start over.
  if (!file_text(side, had, sizeof had) || strcasecmp(had, U.sha)) {
    remove(path);
    file_write(side, U.sha);
  }
  Sink s = {0};
  mbedtls_sha256_init(&s.sha);
  sha_start(&s.sha);
  long offset = 0;
  u8 *buf = malloc(CHUNK);
  if (!buf) {
    mbedtls_sha256_free(&s.sha);
    snprintf(err, (size_t)cap, "Not enough memory to update.");
    return false;
  }
  FILE *old = fopen(path, "rb");
  if (old) {
    size_t n;
    while ((n = fread(buf, 1, CHUNK, old)) > 0) {
      sha_update(&s.sha, buf, n);
      offset += (long)n;
    }
    fclose(old);
    if (offset) dbg_log("update: resuming at %ld", offset);
  }
  free(buf);
  U.done = offset;
  s.file = fopen(path, "ab");
  bool ok = false;
  if (!s.file) {
    snprintf(err, (size_t)cap, "Could not save the update on the SD card. Is it full or locked?");
  }
  for (int attempt = 0; s.file && attempt < DOWNLOAD_ATTEMPTS; attempt++) {
    if (attempt) wait_ms(attempt < 4 ? attempt * 2000 : 8000);
    if (U.size > 0 && offset >= U.size) { ok = true; break; }
    long total = 0;
    err[0] = 0;
    int status = http_download_from(U.url, offset, file_sink, &s, &total, err, cap);
    fflush(s.file);
    offset = U.done;
    if (total > 0) U.size = total;
    dbg_log("update: attempt %d status %d, %ld bytes", attempt, status, offset);
    if (status == 200 || status == 206 || status == 416) { ok = true; break; }
    if (status == 403 || status == 404 || status == 410) break;  // gone: re-read the list
    if (status == 0 && !err[0]) snprintf(err, (size_t)cap, "The download was interrupted.");
  }
  if (s.file) { fclose(s.file); s.file = NULL; }
  u8 digest[32];
  sha_finish(&s.sha, digest);
  mbedtls_sha256_free(&s.sha);
  if (!ok) {
    if (!err[0]) snprintf(err, (size_t)cap, "The download was interrupted. Try again to resume it.");
    return false;
  }
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  if (strcasecmp(hex, U.sha)) {
    dbg_log("update: checksum mismatch (%s)", hex);
    forget_file(path);
    snprintf(err, (size_t)cap, "The download is damaged (checksum mismatch).");
    return false;
  }
  return true;
}

#if !UPDATE_VITA
/** Replaces the running NRO, keeping the old file as .bak. */
static bool install_nro(const char *tmp, const char *target, char *err, int cap) {
  char backup[340];
  snprintf(backup, sizeof backup, "%s.bak", target);
  remove(backup);
  bool moved = rename(target, backup) == 0;
  if (!moved) {
    // Some SD implementations replace atomically; try that before removing the old file.
    if (rename(tmp, target) == 0) return true;
    if (remove(target) != 0 && errno != ENOENT) {
      snprintf(err, (size_t)cap, "Close Tweebbo and try again: the NRO file is in use.");
      return false;
    }
  }
  if (rename(tmp, target) != 0) {
    if (moved) rename(backup, target);
    snprintf(err, (size_t)cap, "Could not replace the NRO. Close Tweebbo and try again.");
    return false;
  }
  return true;
}
#endif

static void *install_thread(void *arg) {
  (void)arg;
  // Re-read the release first: a build published after the offer must not be compared
  // against the old checksum.
  UpdateState fresh = read_manifest();
  if (fresh != UPD_AVAILABLE) {
    U.state = fresh == UPD_LATEST ? UPD_LATEST : UPD_FAILED;
    U.thread_active = false;
    return NULL;
  }
  char path[340], err[180] = "";
  snprintf(path, sizeof path, UPDATE_DATA_DIR "/update-" UPDATE_ASSET);
  update_mkdir(UPDATE_DATA_DIR);
  bool ok = download_file(path, err, sizeof err);
  if (ok) {
#if UPDATE_VITA
    char dst[340];
    snprintf(dst, sizeof dst, UPDATE_DATA_DIR "/tweebbo.vpk");
    remove(dst);
    if (rename(path, dst) != 0) {
      ok = false;
      snprintf(err, sizeof err, "Could not save the update on the memory card.");
    } else copy_text(U.installed, sizeof U.installed, dst);
#else
#ifdef TWEEBBO_HOST
    const char *target = getenv("TWEEBBO_UPDATE_TARGET");
    if (!target || !*target) target = UPDATE_DATA_DIR "/tweebbo.nro";
#else
    const char *target = U.launch[0] ? U.launch : "sdmc:/switch/Tweebbo/tweebbo.nro";
#endif
    ok = install_nro(path, target, err, sizeof err);
    if (ok) copy_text(U.installed, sizeof U.installed, target);
#endif
  }
  if (!ok) copy_text(U.error, sizeof U.error, err[0] ? err : "The update failed.");
  U.state = ok ? UPD_INSTALLED : UPD_FAILED;
  U.thread_active = false;
  dbg_log("update %s: %s %s", U.version, ok ? "installed" : "failed", ok ? "" : U.error);
  return NULL;
}

void update_start(void) {
  if (U.state != UPD_AVAILABLE || U.thread_active) return;
  U.done = 0;
  U.error[0] = 0;
  U.exit_requested = false;
  U.state = UPD_DOWNLOADING;
  U.thread_active = true;
  if (!start_thread(install_thread, NULL)) {
    U.thread_active = false;
    copy_text(U.error, sizeof U.error, "Could not start the update.");
    U.state = UPD_FAILED;
  }
}

UpdateState update_state(void) { return U.state; }
const char *update_version(void) { return U.version; }
const char *update_current(void) { return APP_VERSION; }
const char *update_notes(void) { return U.notes; }
const char *update_error(void) { return U.error; }
const char *update_installed_path(void) { return U.installed; }

float update_progress(void) {
  if (U.size <= 0) return 0;
  float p = (float)U.done / (float)U.size;
  return p < 0 ? 0 : p > 1 ? 1 : p;
}

bool update_restart(void) {
#if UPDATE_VITA
  return false;
#else
  U.exit_requested = true;
  return true;
#endif
}

bool update_exit_requested(void) { return U.exit_requested; }
void update_dismiss(void) { U.dismissed = true; }
bool update_dismissed(void) { return U.dismissed; }
int update_offer(void) { return U.offers; }
void update_forget_dismiss(void) { U.dismissed = false; }

void update_set_launch_path(const char *argv0) {
  // A NRO starts from the SD card ("sdmc:/switch/…"); anything else keeps the default.
  if (!argv0 || strncmp(argv0, "sdmc:/", 6) || !strstr(argv0, ".nro")) return;
  copy_text(U.launch, sizeof U.launch, argv0);
}
