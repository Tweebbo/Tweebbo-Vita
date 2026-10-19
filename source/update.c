// Update notice (see update.h). One request per session, made once the hotel answers.
#include "update.h"
#include "net.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TWEEBBO_DEVICE == 3
#define UPDATE_KEY "vita"
#else
#define UPDATE_KEY "switch"
#endif

static struct { int job; bool asked, dismissed; char version[24], url[200]; } U;

/** true when a (major.minor.patch) is newer than b; suffixes like "-dev" are ignored. */
static bool newer(const char *a, const char *b) {
  int x[3] = {0}, y[3] = {0};
  sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
  return false;
}

void update_poll(u64 now) {
  (void)now;
  if (!U.asked && net_online()) { U.asked = true; U.job = net_http("GET", "/static/consoles/latest.json", NULL, false); }
  HttpResult r;
  if (!U.job || !net_http_result(U.job, &r)) return;
  U.job = 0;
  json_error_t err;
  json_t *j = r.body && r.status == 200 ? json_loads(r.body, 0, &err) : NULL;
  json_t *mine = json_object_get(j, UPDATE_KEY);
  const char *version = json_string_value(json_object_get(mine, "version"));
  const char *url = json_string_value(json_object_get(mine, "url"));
  if (version && url && newer(version, APP_VERSION) && strlen(version) < sizeof U.version && strlen(url) < sizeof U.url) {
    snprintf(U.version, sizeof U.version, "%s", version);
    snprintf(U.url, sizeof U.url, "%s", url);
  }
  if (j) json_decref(j);
  free(r.body);
}

const char *update_available(void) { return U.version[0] ? U.version : NULL; }
const char *update_url(void) { return U.url; }
void update_dismiss(void) { U.dismissed = true; }
bool update_dismissed(void) { return U.dismissed; }
