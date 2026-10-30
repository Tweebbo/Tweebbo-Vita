#include "update.h"
#include "conn.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *scenario;
static bool failed_rename;
u64 now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (u64)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
bool net_online(void) { return true; }
bool net_cancelled(void) { return false; }
void dbg_log(const char *format, ...) { (void)format; }
int rnd_range(int lo, int hi) { (void)hi; return lo; }

int __real_rename(const char *, const char *);
int __wrap_rename(const char *source, const char *target) {
  const char *installed = getenv("TWEEBBO_UPDATE_TARGET");
  if (!installed) installed = "update/tweebbo.vpk";
  if (!strcmp(scenario, "rename-no-backup") && (!strcmp(source, installed) || !strcmp(target, installed))) { errno = EACCES; return -1; }
  if ((!strcmp(scenario, "rename-fail") || !strcmp(scenario, "small-rename-retry")) &&
      !failed_rename && !strcmp(target, installed) && !strstr(source, ".bak")) { failed_rename = true; errno = EIO; return -1; }
  return __real_rename(source, target);
}
size_t __real_fwrite(const void *, size_t, size_t, FILE *);
size_t __wrap_fwrite(const void *data, size_t size, size_t count, FILE *file) {
  if (!strcmp(scenario, "write-fail")) { errno = ENOSPC; return 0; }
  return __real_fwrite(data, size, count, file);
}
static bool same(const char *a, const char *b) {
  FILE *x = fopen(a, "rb"), *y = fopen(b, "rb"); if (!x || !y) { if(x)fclose(x);if(y)fclose(y);return false; }
  bool ok = true; unsigned char p[4096], q[4096];
  for (;;) { size_t n = fread(p,1,sizeof p,x),m = fread(q,1,sizeof q,y);if(n!=m||memcmp(p,q,n)){ok=false;break;}if(!n)break; }
  ok = ok && !ferror(x) && !ferror(y); fclose(x);fclose(y);return ok;
}
static void wait_worker(void) {
  u64 until = now_ms() + 30000;
  while (update_state() == UPD_CHECKING || update_state() == UPD_DOWNLOADING) { assert(now_ms()<until); usleep(1000); }
  /* Publication of state precedes the detached thread's final bookkeeping. */
  usleep(30000);
}
int main(int argc, char **argv) {
  assert(argc==2);scenario=argv[1];char error[128];assert(conn_global_init(error,sizeof error));
  update_check();wait_worker();
  if (!strcmp(scenario,"bad-full-url") || !strcmp(scenario,"bad-version") || !strcmp(scenario,"prerelease")) {
    assert(update_state()==UPD_FAILED);assert(same("target.nro","previous.bin"));puts("PASS rejected offer");return 0;
  }
  assert(update_state()==UPD_AVAILABLE);
  bool small = !strncmp(scenario,"small-",6);
  assert(update_small()==small);assert(update_full_bytes()>0);
  if(small)assert(update_download_bytes()<update_full_bytes());
  update_dismiss();assert(update_dismissed());update_forget_dismiss();
  update_start();wait_worker();
  bool fallback = !strcmp(scenario,"small-base-missing") || !strcmp(scenario,"small-base-damaged") ||
                  !strcmp(scenario,"small-header") || !strcmp(scenario,"small-checksum") || !strcmp(scenario,"small-trailing");
  if(fallback){
    assert(update_state()==UPD_FAILED && update_full_fallback());assert(strstr(update_error(),"Full download"));
    assert(same("target.nro","previous.bin"));puts("WAIT_FULL");fflush(stdout);assert(getchar()=='\n');
    update_start();wait_worker();
  }
  if(!strcmp(scenario,"rename-fail") || !strcmp(scenario,"small-rename-retry")){
    assert(update_state()==UPD_FAILED);const char *old=getenv("TWEEBBO_UPDATE_TARGET");if(!old)old="update/tweebbo.vpk";
    assert(same(old,"previous.bin"));puts("WAIT_RETRY");fflush(stdout);assert(getchar()=='\n');update_start();wait_worker();
  }
  if(!strcmp(scenario,"write-fail") || !strcmp(scenario,"wrong-full-hash") || !strcmp(scenario,"rename-no-backup")) {
    assert(update_state()==UPD_FAILED);const char *old=getenv("TWEEBBO_UPDATE_TARGET");if(!old)old="update/tweebbo.vpk";
    assert(same(old,"previous.bin"));
  }else{
    if(update_state()!=UPD_INSTALLED)fprintf(stderr,"state %d: %s\n",update_state(),update_error());
    assert(update_state()==UPD_INSTALLED);assert(same(update_installed_path(),"payload.bin"));
  }
  assert(same("userdata/session.txt","saved-session.txt"));
  conn_global_exit();puts("PASS updater worker");return 0;
}
