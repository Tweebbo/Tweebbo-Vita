// In-game updates (see update.h). The check and the download run outside the render thread;
// the UI only reads the state. GitHub publishes the digest of every release asset, so a
// downloaded build is installed only after its SHA-256 matches.
#include "update.h"
#include "net.h"
#include "ws.h"
#include "incremental.h"
#include <jansson.h>
#include <mbedtls/sha256.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#if TWEEBBO_DEVICE == 3
#define UPDATE_REPO "Tweebbo/Tweebbo-Vita"
#define UPDATE_ASSET "tweebbo.vpk"
#define UPDATE_FORMAT "vpk"
#define UPDATE_IDENTITY "TWEEBBO-VITA____"
#define UPDATE_KIND 4
#else
#define UPDATE_REPO "Tweebbo/TweebboNX"
#define UPDATE_ASSET "tweebbo.nro"
#define UPDATE_FORMAT "nro"
#define UPDATE_IDENTITY "TWEEBBO-SWITCH__"
#define UPDATE_KIND 3
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

#if TWEEBBO_DEVICE == 3
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
#define CHUNK (128 * 1024)

static struct {
  volatile UpdateState state;
  volatile int offers;
  volatile long size, done;
  volatile long progress_total;
  char version[24], notes[600], error[180], url[1024], sha[72], installed[300], launch[300];
  char phase[64];
  IncrementalOffer small;
  bool full_fallback;
  bool download_invalid;
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

static bool file_write(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  if (!f) return false;
  bool ok = fputs(text, f) >= 0;
  if (fclose(f)) ok = false;
  return ok;
}

static void forget_file(const char *path) {
  char side[340];
  remove(path);
  snprintf(side, sizeof side, "%s.sha", path);
  remove(side);
}

/* The Vita SDK ships an older mbedTLS without the _ret SHA-256 helpers. */
static void sha_start(mbedtls_sha256_context *ctx) {
#ifdef __vita__
  mbedtls_sha256_starts(ctx, 0);
#else
  mbedtls_sha256_starts_ret(ctx, 0);
#endif
}
static void sha_update(mbedtls_sha256_context *ctx, const u8 *data, size_t len) {
#ifdef __vita__
  mbedtls_sha256_update(ctx, data, len);
#else
  mbedtls_sha256_update_ret(ctx, data, len);
#endif
}
static void sha_finish(mbedtls_sha256_context *ctx, u8 digest[32]) {
#ifdef __vita__
  mbedtls_sha256_finish(ctx, digest);
#else
  mbedtls_sha256_finish_ret(ctx, digest);
#endif
}

typedef struct { char *buf; int len, cap; } Mem;

static bool to_memory(const u8 *data, int len, void *ctx) {
  Mem *m = ctx;
  if (len < 0 || len >= m->cap - m->len) return false;
  memcpy(m->buf + m->len, data, (size_t)len);
  m->len += len;
  m->buf[m->len] = 0;
  return true;
}

static bool valid_version(const char *value) {
  if (!value || !*value || strlen(value) >= sizeof U.version) return false;
  for (unsigned part = 0; part < 3; part++) {
    unsigned digits = 0;
    while (*value >= '0' && *value <= '9') { value++; if (++digits > 5) return false; }
    if (!digits || (part < 2 ? *value++ != '.' : *value != 0)) return false;
  }
  return true;
}

static bool allowed_artifact(const char *url) {
  if (!url) return false;
#ifdef TWEEBBO_HOST
  if (!strncmp(url, "http://127.0.0.1:", 17)) {
    const char *p = url + 17, *first = p;
    while (*p >= '0' && *p <= '9') p++;
    return p > first && p - first <= 5 && *p == '/' && !strchr(p, '\r') && !strchr(p, '\n');
  }
#endif
  char prefix[160];
  snprintf(prefix, sizeof prefix, "https://github.com/" UPDATE_REPO "/releases/download/v%s/", U.version);
  size_t length = strlen(prefix);
  if (strncmp(url, prefix, length) || !url[length]) return false;
  for (const char *p = url + length; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("._-", *p))) return false;
  return true;
}

static const char *asset_hash(json_t *asset) {
  const char *sha = json_string_value(json_object_get(asset, "digest"));
  if (!sha) sha = json_string_value(json_object_get(asset, "sha256"));
  return sha && !strncmp(sha, "sha256:", 7) ? sha + 7 : sha;
}

static bool asset_matches(json_t *release, const char *url, const char *sha, long bytes) {
  json_t *assets = json_object_get(release, "assets"), *asset; size_t index;
  json_array_foreach(assets, index, asset) {
    const char *candidate = json_string_value(json_object_get(asset, "browser_download_url"));
    const char *hash = asset_hash(asset);
    if (candidate && hash && !strcmp(candidate, url) && !strcasecmp(hash, sha) &&
        json_integer_value(json_object_get(asset, "size")) == bytes) return true;
  }
  return false;
}

static void read_small_offer(json_t *release) {
  memset(&U.small, 0, sizeof U.small);
  json_t *assets = json_object_get(release, "assets"), *asset; size_t index;
  json_array_foreach(assets, index, asset) {
    const char *name = json_string_value(json_object_get(asset, "name"));
    if (!name || strcmp(name, "latest.json")) continue;
    const char *url = json_string_value(json_object_get(asset, "browser_download_url"));
    const char *sha = asset_hash(asset);
    long bytes = (long)json_integer_value(json_object_get(asset, "size"));
    if (!allowed_artifact(url) || !is_sha256(sha) || bytes <= 0 || bytes >= 16384) return;
    Mem body = {malloc(16384), 0, 16384}; long total = 0; char error[128] = "";
    int status = body.buf ? http_download(url, to_memory, &body, &total, error, sizeof error) : 0;
    if (status != 200 || body.len != bytes) { free(body.buf); return; }
    mbedtls_sha256_context hash; u8 digest[32]; char text[65];
    mbedtls_sha256_init(&hash); sha_start(&hash); sha_update(&hash, (u8 *)body.buf, (size_t)body.len); sha_finish(&hash, digest); mbedtls_sha256_free(&hash);
    for (unsigned n = 0; n < 32; n++) snprintf(text + n * 2, 3, "%02x", digest[n]);
    json_error_t parse_error;
    json_t *manifest = !strcasecmp(text, sha) ? json_loads(body.buf, JSON_REJECT_DUPLICATES, &parse_error) : NULL;
    free(body.buf);
    if (!manifest) return;
    const char *version = json_string_value(json_object_get(manifest, "version"));
    const char *identity = json_string_value(json_object_get(manifest, "identity"));
    json_t *full = json_object_get(manifest, "full");
    const char *full_url = json_string_value(json_object_get(full, "url"));
    const char *full_sha = json_string_value(json_object_get(full, "sha256"));
    if (version && identity && full_url && full_sha && !strcmp(version, U.version) && !strcmp(identity, UPDATE_IDENTITY) &&
        !strcmp(full_url, U.url) && !strcasecmp(full_sha, U.sha) && json_integer_value(json_object_get(full, "bytes")) == U.size &&
        incremental_offer(manifest, APP_VERSION, UPDATE_FORMAT, allowed_artifact, &U.small) &&
        (!asset_matches(release, U.small.url, U.small.sha256, U.small.bytes) || U.small.bytes >= (unsigned long)U.size)) memset(&U.small, 0, sizeof U.small);
    json_decref(manifest);
    return;
  }
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
  json_t *j = json_loads(m.buf, JSON_REJECT_DUPLICATES, &jerr);
  free(m.buf);
  if (!j) {
    copy_text(U.error, sizeof U.error, "The update list is not readable.");
    return UPD_FAILED;
  }
  const char *tag = json_string_value(json_object_get(j, "tag_name"));
  const char *body = json_string_value(json_object_get(j, "body"));
  const char *version = tag ? tag : "";
  if (*version == 'v' || *version == 'V') version++;
  if (!valid_version(version) || json_is_true(json_object_get(j, "draft")) || json_is_true(json_object_get(j, "prerelease"))) {
    json_decref(j); copy_text(U.error, sizeof U.error, "The release version is not supported."); return UPD_FAILED;
  }
  const char *url = NULL, *sha = NULL;
  long size = 0;
  json_t *assets = json_object_get(j, "assets"), *asset;
  size_t i;
  json_array_foreach(assets, i, asset) {
    const char *name = json_string_value(json_object_get(asset, "name"));
    if (!name || strcmp(name, UPDATE_ASSET)) continue;
    url = json_string_value(json_object_get(asset, "browser_download_url"));
    json_t *size_value = json_object_get(asset, "size");
    json_int_t count = json_integer_value(size_value);
    size = json_is_integer(size_value) && count > 0 && count <= INCREMENTAL_MAX_BYTES ? (long)count : 0;
    sha = json_string_value(json_object_get(asset, "digest"));
    if (!sha) sha = json_string_value(json_object_get(asset, "sha256"));
    break;
  }
  if (!sha) sha = json_string_value(json_object_get(j, "sha256"));
  if (sha && !strncmp(sha, "sha256:", 7)) sha += 7;
  bool newer = version_cmp(version, APP_VERSION) > 0;
  bool complete = url && *url && is_sha256(sha) && size > 0 && size <= INCREMENTAL_MAX_BYTES && strlen(url) < sizeof U.url;
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
  if (strcmp(U.version, version) || strcmp(U.sha, sha)) { U.offers++; U.full_fallback = false; }
  copy_text(U.version, sizeof U.version, version);
  if (!allowed_artifact(url)) { json_decref(j); copy_text(U.error, sizeof U.error, "The update URL is not a trusted release."); return UPD_FAILED; }
  copy_text(U.url, sizeof U.url, url);
  copy_text(U.sha, sizeof U.sha, sha);
  copy_text(U.notes, sizeof U.notes, body ? body : "");
  U.size = size;
  read_small_offer(j);
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
  bool write_failed;
} Sink;

static bool file_sink(const u8 *data, int len, void *ctx) {
  Sink *s = ctx;
  if (len < 0 || U.done > U.progress_total || len > U.progress_total - U.done) { s->write_failed = true; U.download_invalid = true; return false; }
  if (fwrite(data, 1, (size_t)len, s->file) != (size_t)len) { s->write_failed = true; return false; }
  sha_update(&s->sha, data, (size_t)len);
  U.done += len;
  return true;
}

/** Downloads a bound release artifact, retaining an interrupted partial for resume. */
static bool download_file(const char *url, const char *sha, long expected, const char *path, char *err, int cap) {
  U.download_invalid = false;
  char side[340], had[80] = "";
  snprintf(side, sizeof side, "%s.sha", path);
  // A partial file of another build is useless: start over.
  if (!file_text(side, had, sizeof had) || strcasecmp(had, sha)) {
    remove(path);
    if (!file_write(side, sha)) { snprintf(err, (size_t)cap, "Could not save the update checksum. Check the storage card."); return false; }
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
    if (fseek(old, 0, SEEK_END) || ftell(old) < 0 || ftell(old) > expected || fseek(old, 0, SEEK_SET)) {
      fclose(old); free(buf); mbedtls_sha256_free(&s.sha); forget_file(path);
      snprintf(err, (size_t)cap, "The partial update is invalid. Try again."); return false;
    }
    size_t n;
    while ((n = fread(buf, 1, CHUNK, old)) > 0) {
      sha_update(&s.sha, buf, n);
      offset += (long)n;
    }
    bool read_ok = !ferror(old); if (fclose(old)) read_ok = false;
    if (!read_ok) { free(buf); mbedtls_sha256_free(&s.sha); snprintf(err, (size_t)cap, "Could not read the saved update."); return false; }
    if (offset) dbg_log("update: resuming at %ld", offset);
  }
  U.done = offset;
  U.progress_total = expected;
  s.file = fopen(path, "ab");
  if (s.file) setvbuf(s.file, (char *)buf, _IOFBF, CHUNK);
  bool ok = false;
  if (!s.file) {
    snprintf(err, (size_t)cap, "Could not save the update on the SD card. Is it full or locked?");
  }
  for (int attempt = 0; s.file && attempt < DOWNLOAD_ATTEMPTS; attempt++) {
    if (attempt) wait_ms(attempt < 4 ? attempt * 2000 : 8000);
    if (offset == expected) { ok = true; break; }
    long total = 0;
    err[0] = 0;
    int status = http_download_from(url, offset, file_sink, &s, &total, err, cap);
    if (fflush(s.file) || s.write_failed) {
      snprintf(err, (size_t)cap, "Could not save the update on the SD card. Is it full?");
      break;
    }
    offset = U.done;
    if (total > 0 && total != expected) { U.download_invalid = true; snprintf(err, (size_t)cap, "The update size differs from the release."); break; }
    dbg_log("update: attempt %d status %d, %ld bytes", attempt, status, offset);
    if ((status == 200 || status == 206 || status == 416) && offset == expected) { ok = true; break; }
    if (status == 403 || status == 404 || status == 410) break;  // gone: re-read the list
    if (status == 0 && !err[0]) snprintf(err, (size_t)cap, "The download was interrupted.");
  }
  if (s.file) { if (fclose(s.file)) { ok = false; snprintf(err, (size_t)cap, "Could not finish saving the update."); } s.file = NULL; }
  free(buf);
  u8 digest[32];
  sha_finish(&s.sha, digest);
  mbedtls_sha256_free(&s.sha);
  if (!ok) {
    if (!err[0]) snprintf(err, (size_t)cap, "The download was interrupted. Try again to resume it.");
    return false;
  }
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  if (strcasecmp(hex, sha)) {
    U.download_invalid = true;
    dbg_log("update: checksum mismatch (%s)", hex);
    forget_file(path);
    snprintf(err, (size_t)cap, "The download is damaged (checksum mismatch).");
    return false;
  }
  return true;
}

/** Publishes a verified file, retaining the previous copy if replacement fails. */
static bool install_nro(const char *tmp, const char *target, char *err, int cap) {
  char backup[340];
  snprintf(backup, sizeof backup, "%s.bak", target);
  remove(backup);
  bool moved = rename(target, backup) == 0;
  if (!moved) {
    // Some SD implementations replace atomically; try that before removing the old file.
    if (rename(tmp, target) == 0) return true;
    snprintf(err, (size_t)cap, "Could not replace the saved update. Your previous file is retained.");
    return false;
  }
  if (rename(tmp, target) != 0) {
    if (moved) rename(backup, target);
    snprintf(err, (size_t)cap, "Could not replace the NRO. Close Tweebbo and try again.");
    return false;
  }
  return true;
}

static void rebuild_progress(unsigned bytes, void *user) { (void)user; U.done = bytes; }
static const char *installed_resources(void) {
#ifdef TWEEBBO_HOST
  const char *path = getenv("TWEEBBO_UPDATE_RESOURCES"); if (path && *path) return path;
#endif
#if UPDATE_VITA
  return "app0:/";
#else
  return "romfs:/";
#endif
}

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
  char patch[380], rebuilt[380]; snprintf(patch, sizeof patch, "%s.tupd.part", path); snprintf(rebuilt, sizeof rebuilt, "%s.rebuilding", path);
  bool ok = incremental_verify(path, U.sha, (unsigned)U.size, NULL);
  if (!ok && update_small()) {
    copy_text(U.phase, sizeof U.phase, "Downloading smaller update");
    ok = download_file(U.small.url, U.small.sha256, U.small.bytes, patch, err, sizeof err);
    if (ok) {
      copy_text(U.phase, sizeof U.phase, "Preparing update from installed files"); U.done = 0; U.progress_total = U.size;
      IncrementalTarget target = {UPDATE_IDENTITY, APP_VERSION, U.version, U.sha, (unsigned)U.size, UPDATE_KIND};
      IncrementalHooks hooks = {NULL, rebuild_progress, NULL};
      IncrementalResult result = incremental_apply(patch, rebuilt, installed_resources(), &U.small, &target, &hooks, err, sizeof err);
      ok = result == INCREMENTAL_OK;
      if (ok) ok = install_nro(rebuilt, path, err, sizeof err);
      if (!ok && (result == INCREMENTAL_INVALID || result == INCREMENTAL_BASE_MISMATCH)) { U.full_fallback = true; snprintf(err, sizeof err, "The smaller update could not be prepared. Your current app is retained. Choose Full download (%.1f MB), or Later.", U.size / 1000000.0); }
    } else {
      if (U.download_invalid) {
        U.full_fallback = true; snprintf(err, sizeof err, "The smaller download failed verification. Choose Full download (%.1f MB), or Later.", U.size / 1000000.0);
      }
    }
  } else if (!ok) {
    copy_text(U.phase, sizeof U.phase, "Downloading full update");
    ok = download_file(U.url, U.sha, U.size, path, err, sizeof err);
  }
  if (ok) {
    copy_text(U.phase, sizeof U.phase, "Verifying prepared update");
    ok = incremental_verify(path, U.sha, (unsigned)U.size, NULL);
    if (!ok) copy_text(err, sizeof err, "The prepared update failed verification. Your current app is retained.");
  }
  if (ok) {
    copy_text(U.phase, sizeof U.phase, "Saving verified update");
#if UPDATE_VITA
    char dst[340];
    snprintf(dst, sizeof dst, UPDATE_DATA_DIR "/tweebbo.vpk");
    ok = install_nro(path, dst, err, sizeof err);
    if (ok) copy_text(U.installed, sizeof U.installed, dst);
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
  if (ok) { forget_file(patch); forget_file(path); }
  if (!ok) copy_text(U.error, sizeof U.error, err[0] ? err : "The update failed.");
  U.state = ok ? UPD_INSTALLED : UPD_FAILED;
  U.thread_active = false;
  dbg_log("update %s: %s %s", U.version, ok ? "installed" : "failed", ok ? "" : U.error);
  return NULL;
}

void update_start(void) {
  if ((U.state != UPD_AVAILABLE && !(U.state == UPD_FAILED && U.version[0])) || U.thread_active) return;
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
const char *update_status(void) { return U.phase; }
bool update_full_fallback(void) { return U.full_fallback; }
bool update_small(void) { return !U.full_fallback && U.small.bytes && U.small.bytes < (unsigned long)U.size; }
long update_download_bytes(void) { return update_small() ? (long)U.small.bytes : U.size; }
long update_full_bytes(void) { return U.size; }

float update_progress(void) {
  if (U.progress_total <= 0) return 0;
  float p = (float)U.done / (float)U.progress_total;
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
