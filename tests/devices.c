#include "world.h"
#include "proto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u64 clock_ms=1000;
u64 now_ms(void) { return clock_ms; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
void on_room(void) {}
void on_chat(u32 id, const char *s) { (void)id; (void)s; }
void on_notice(const char *s) { (void)s; }
void on_kicked(const char *text) { (void)text; }
void on_transfer(const char *s, const char *a) { (void)s; (void)a; }
void on_alert(const char *s) { (void)s; }
void on_casino(const char *s) { (void)s; }
void on_trade(const char *s) { (void)s; }
void on_snow(const u8 *data,int len) { (void)data;(void)len; }
void on_social_changed(void) {}
void on_name_changed(u32 id) { (void)id; }
void on_item(u32 id) { (void)id; }

static void entity(Writer *w) {
  w_var(w, 7); w_str(w, "Player");
  w_u8(w, 1); w_u8(w, 1); w_u8(w, 2); w_u8(w, FLAG_3DS);
  w_var(w, 0); w_u16(w, 0); w_var(w, 77);
}
int main(void) {
  Writer w;
  w_begin(&w, S_DEVICE); w_var(&w, 1); w_var(&w, 7); w_u8(&w, DEVICE_SWITCH);
  world_handle(w.buf, w.len);
  assert(!world_ent(7));
  w_begin(&w, S_ROOM); w_str(&w, "fixture"); w_str(&w, "Fixture"); w_str(&w, "model_a");
  w_u8(&w, 2); w_u8(&w, 2); w_str(&w, "00\n00"); w_u8(&w, 0); w_u8(&w, 0);
  w_var(&w, 0); w_str(&w, ""); w_var(&w, 0); w_var(&w, 1); entity(&w);
  world_handle(w.buf, w.len);
  assert(world_ent(7) && world_ent(7)->device == DEVICE_SWITCH);
  world_handle(w.buf, w.len);
  assert(world_ent(7)->device == DEVICE_SWITCH);
  w_begin(&w, S_DEVICE); w_var(&w, 1); w_var(&w, 7); w_u8(&w, DEVICE_VITA);
  world_handle(w.buf, w.len);
  assert(world_ent(7)->device == DEVICE_VITA);
  w_begin(&w, S_ENTITY_REMOVE); w_var(&w, 7); world_handle(w.buf, w.len);
  w_begin(&w, S_ENTITY_ADD); entity(&w); world_handle(w.buf, w.len);
  assert(world_ent(7)->device == 0);
  W.me = 7;
  world_handle(w.buf, w.len);
  assert(world_ent(7)->device == TWEEBBO_DEVICE);
  puts("OK: device announcement before room snapshot, room refresh, device change and removal.");
  w_begin(&w,S_TRADE);w_str(&w,"{\"stage\":\"invite\"}");world_handle(w.buf,w.len);
  assert(W.trade_rev==1 && !strcmp(W.trade_json,"{\"stage\":\"invite\"}"));
  world_handle(w.buf,w.len-1);assert(W.trade_rev==2 && !W.trade_json);
  puts("OK: trade snapshots reach the client; truncated packets invalidate old confirmations.");
  W.items[0]=(Item){.used=true,.id=100,.def=61001,.x=2,.y=3};
  w_begin(&w,S_SLIDE);w_var(&w,1);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,3);w_u8(&w,3);w_u8(&w,0);w_u8(&w,3);w_var(&w,1);w_u16(&w,0);
  world_handle(w.buf,w.len);
  float x,y,z;item_pos(world_item(100),1250,&x,&y,&z);
  assert(x==2.5f && y==3 && z==0);
  clock_ms=1250;
  w_begin(&w,S_ITEM_UPDATE);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,3);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,0);
  world_handle(w.buf,w.len);item_pos(world_item(100),1250,&x,&y,&z);
  assert(x==2.5f && world_item(100)->state==2);
  w_begin(&w,S_SLIDE);w_var(&w,1);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,4);w_u8(&w,3);w_u8(&w,0);w_u8(&w,2);w_var(&w,1);w_u16(&w,32);
  world_handle(w.buf,w.len);item_pos(world_item(100),1500,&x,&y,&z);
  assert(x==3.25f && y==3 && z==16);
  item_pos(world_item(100),2000,&x,&y,&z);assert(x==4 && z==32);
  clock_ms=2000;
  w_begin(&w,S_ITEM_UPDATE);w_var(&w,100);w_u16(&w,61001);
  w_u8(&w,5);w_u8(&w,3);w_u8(&w,0);w_u8(&w,0);w_var(&w,1);w_u16(&w,0);
  world_handle(w.buf,w.len);item_pos(world_item(100),2000,&x,&y,&z);
  assert(x==5 && z==0 && !world_item(100)->slide_at);
  world_handle(w.buf,w.len-1);assert(world_item(100)->x==5);
  puts("OK: slides interpolate position and height, survive state updates, restart from the current position and ignore truncated packets.");
}

void on_activity(const char *json){(void)json;}
void on_music(const char *json){(void)json;}
