#include "stability_client.h"
#if STABILITY_DIAGNOSTICS
#include "stability_core.h"
#include "net.h"
#include "world.h"
#ifdef __3DS__
#include "ui_int.h"
#else
#include "client_ui.h"
#endif
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef __3DS__
#include <3ds.h>
#endif
static Stability state;
static bool initialized;
static unsigned memory_kib,power,last_panel;
static unsigned previous_owner,marked_owner;
static bool marker_loaded;
void stability_client_metrics(unsigned memory,unsigned flags){memory_kib=memory;power=flags;}
static void send_snapshot(uint64_t now,const char *reason){
 stability_attempted(&state,now);
#ifdef __3DS__
 state.memory_kib=linearSpaceFree()/1024u;
#endif
 unsigned char packet[STABILITY_JSON_MAX+1];packet[0]=29;
 size_t n=stability_json(&state,now,reason,(char *)packet+1,sizeof packet-1);
 if(n&&net_stability_send(packet,(int)n+1))stability_submitted(&state,now);
}
void stability_client_tick(uint64_t now){
 unsigned owner=W.guest_id;
 if(!marker_loaded){char value[32];if(store_load("stability-active",value,sizeof value))sscanf(value,"%u",&previous_owner);marker_loaded=true;}
 if(!initialized||state.owner!=owner){
  char session[33];snprintf(session,sizeof session,"%016llx%016llx",(unsigned long long)now,(unsigned long long)time(NULL));
  const char *device="web";
#if defined(__3DS__)
  bool model=false;APT_CheckNew3DS(&model);device=model?"new3ds":"old3ds";
  snprintf(session,sizeof session,"%016llx%016llx",(unsigned long long)svcGetSystemTick(),(unsigned long long)osGetTime());
#elif defined(__SWITCH__) || TWEEBBO_DEVICE==2
  device="switch";
#elif defined(__vita__) || TWEEBBO_DEVICE==3
  device="vita";
#endif
  stability_init(&state,now,session,APP_VERSION "+stability-private",device,owner&&owner==previous_owner);state.owner=owner;initialized=true;
 }
#ifdef __3DS__
 unsigned panel=ui_panel();
 const char *activity=panel==P_SNOW?"snowstorm":panel==P_FASTFOOD?"fastfood":panel==P_BATTLE?"battleball":panel==P_CASINO?"casino":panel==P_ROOM?"room":"menu";
#else
 unsigned panel=0;const char *activity=client_ui_stability_activity(&panel);
#endif
 if(panel!=last_panel){last_panel=panel;stability_event(&state,now,ST_PANEL,panel);}
 stability_context(&state,now,net_room(),activity);
 if(net_state()!=state.network&&net_state()==NET_RETRYING)stability_event(&state,now,ST_ERROR,1000u+NET_RETRYING);
 stability_frame(&state,now,memory_kib,power,net_state());
 if(net_online()&&owner&&marked_owner!=owner){char value[32];snprintf(value,sizeof value,"%u",owner);store_save("stability-active",value);marked_owner=owner;}
 if(net_online()&&owner&&stability_due(&state,now))send_snapshot(now,!state.sent?"start":state.pending_error?"error":"sample");
}
void stability_client_close(void){if(initialized&&net_online())send_snapshot(state.last_frame,"close");if(marked_owner)store_save("stability-active","0");}
#endif
