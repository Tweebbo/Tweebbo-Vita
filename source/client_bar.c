#include "language.h"
// After Hours Bar on the console: the menu comes from the server (prices and contents are
// decided there, server room/bar.rs); orders walk the guest to a free stool and Vic serves.
#include "client_bar.h"
#include "client_ui.h"
#include "avatar.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR_COUNTER 1729
#define MENU_MAX 24
#define STAFF_BASE 4001000000u
typedef struct { int id, price; bool smoke; char name[40], about[64]; } MenuItem;
typedef struct { u32 eid; int drink; u64 at; bool toast; } Gesture;
static struct {
  MenuItem menu[MENU_MAX];
  int menu_n;
  bool asked;
  Gesture gestures[32];
  int page, selected, job;
  u64 asked_at, pending;
  bool confirm;
  bool picking, menu_mode;
} B;

/** Hand items only the bar serves: no idle sipping, they move when their holder acts. */
static void copy(char *dst,size_t cap,const char *s){snprintf(dst,cap,"%s",s?s:"");}
static bool bar_only(int drink) {
  if(drink==19)return !strcmp(W.slug,"afterhours");
  static const int ids[] = {28, 29, 30, 31, 35, 40, 43, 44, 50, 53, 200, 201, 202};
  for (int i = 0; i < (int)COUNT(ids); i++) if (ids[i] == drink) return true;
  return false;
}
static bool smoke(int drink) { return drink == 201 || drink == 202; }
bool bar_room(void) { return !strcmp(W.slug, "afterhours"); }

static void command(const char *op, const char *extra) {
  char json[96];
  snprintf(json, sizeof json, "{\"op\":\"%s\"%s%s}", op, extra && extra[0] ? "," : "", extra ? extra : "");
  if(net_online())send_activity(json);
}

static void gesture(u32 eid, int drink, u64 now, bool toast) {
  int slot = 0;
  for (int i = 0; i < (int)COUNT(B.gestures); i++) {
    if (B.gestures[i].eid == eid) { slot = i; break; }
    if (B.gestures[i].at < B.gestures[slot].at) slot = i;
  }
  B.gestures[slot] = (Gesture){eid, drink, now, toast};
}

bool client_bar_receive(const char *text, u64 now) {
  json_t *j = json_loads(text, 0, NULL);
  const char *kind = json_string_value(json_object_get(j, "bar"));
  if (!kind) { json_decref(j); return false; }
  if (!strcmp(kind, "menu")) {
    B.menu_n = 0;B.asked=false;B.confirm=false;
    size_t i; json_t *v;
    json_array_foreach(json_object_get(j, "items"), i, v) {
      if (B.menu_n >= MENU_MAX) break;
      MenuItem *m = &B.menu[B.menu_n++];
      m->id = (int)json_integer_value(json_object_get(v, "id"));
      m->price = (int)json_integer_value(json_object_get(v, "price"));
      m->smoke = json_is_true(json_object_get(v, "smoke"));
      copy(m->name, sizeof m->name, json_string_value(json_object_get(v, "name")));
      copy(m->about, sizeof m->about, json_string_value(json_object_get(v, "about")));
    }
  } else if (!strcmp(kind, "act")) {
    gesture((u32)json_integer_value(json_object_get(j, "entity")), (int)json_integer_value(json_object_get(j, "drink")), now, false);
  } else if (!strcmp(kind, "toast")) {
    size_t i; json_t *v;
    json_array_foreach(json_object_get(j, "entities"), i, v) {
      Ent *e = world_ent((u32)json_integer_value(v));
      if (e && e->drink) gesture(e->id, e->drink, now, true);
    }
  }
  json_decref(j);
  return true;
}

int client_bar_sip(const Ent *e, u64 now, bool moving) {
  int drink = now < e->drink_until ? e->drink : 0;
  if (!bar_only(drink)) return -1;
  if (moving) return 0;
  for (int i = 0; i < (int)COUNT(B.gestures); i++) {
    const Gesture *g = &B.gestures[i];
    if (g->eid == e->id && g->drink == drink) return now - g->at < (g->toast ? 1500u : smoke(drink) ? 1100u : 900u);
  }
  return 0;
}

bool bar_holding(void) {
  Ent *me = world_ent(W.me);
  return me && bar_only(me->drink) && now_ms() < me->drink_until;
}

// ---------------------------------------------------------------- "Press A"

bool client_bar_context(char *label, int cap, u64 now) {
  Ent *me = world_ent(W.me);
  if (!bar_room() || !me || ent_moving(me, now)) return false;
  bool near = false;
  for (int i = 0; i < MAX_ITEMS && !near; i++) {
    const Item *it = &W.items[i];
    near = it->used && it->def == BAR_COUNTER && abs(it->x - me->x) <= 2 && abs(it->y - me->y) <= 2;
  }
  for (int i = 0; i < MAX_ENTS && !near; i++) {
    const Ent *e = &W.ents[i];
    near = e->used && e->guest >= STAFF_BASE && !strncmp(e->name, "Bartender", 9) && abs(e->x - me->x) <= 2 && abs(e->y - me->y) <= 2;
  }
  if (!near) return false;
  snprintf(label, cap, "%s", bar_holding() ? TR("Bar · what you are holding") : TR("Order at the bar"));
  return true;
}

enum { SELECT=9000, ORDER, PAGE, MENU, USE, TOAST, DROP, PICK, GIVE, CANCEL, REFRESH };
void client_bar_reset(void){memset(&B,0,sizeof B);}
void client_bar_open(void){B.confirm=false;B.picking=false;B.page=0;B.menu_mode=!bar_holding();B.asked=false;}
void client_bar_update(void){
  if(!net_online()||!bar_room()){client_bar_reset();return;}
  if(B.pending && now_ms()-B.pending>12000)B.pending=0;
}
static bool giveable(const Ent *e){
  Ent *me=world_ent(W.me);
  return me&&e&&e->used&&e->id!=W.me&&e->guest&&e->guest<STAFF_BASE&&!e->drink&&abs(e->x-me->x)<=2&&abs(e->y-me->y)<=2;
}
void client_bar_action(int cmd,int arg){
  if(!net_online()||!bar_room())return;
  if(cmd==CANCEL){B.confirm=false;client_ui_changed();return;}
  if(cmd==REFRESH){B.asked=false;B.menu_n=0;return;}
  if(cmd==PAGE){B.page+=arg;if(B.page<0)B.page=0;client_ui_changed();return;}
  if(cmd==MENU){B.menu_mode=true;B.picking=false;B.page=0;client_ui_changed();return;}
  if(cmd==SELECT && arg>=0&&arg<B.menu_n){B.selected=arg;B.confirm=true;client_ui_changed();return;}
  if(cmd==ORDER && B.confirm && B.selected<B.menu_n && !B.pending){
    MenuItem *m=&B.menu[B.selected];if(m->price<0 || (u32)m->price>W.credits)return;
    char extra[32];snprintf(extra,sizeof extra,"\"item\":%d",m->id);command("bar_order",extra);B.pending=now_ms();B.confirm=false;B.menu_mode=false;client_ui_close();return;
  }
  if(!bar_holding())return;
  if(cmd==USE)command("bar_use",NULL);
  if(cmd==TOAST&&!smoke(world_ent(W.me)->drink))command("bar_toast",NULL);
  if(cmd==DROP){command("bar_drop",NULL);client_ui_close();}
  if(cmd==PICK){B.picking=!B.picking;B.page=0;client_ui_changed();}
  if(cmd==GIVE&&giveable(world_ent((u32)arg))){char extra[32];snprintf(extra,sizeof extra,"\"target\":%u",(u32)arg);command("bar_give",extra);B.picking=false;client_ui_changed();}
}
void client_bar_draw(float x,float y,float w){
  if(!net_online()||!bar_room()){client_ui_paragraph(TR("Visit After Hours Bar while connected to order and use bar items."),x+10,y+36,w-20,4,0x18272a);return;}
  if(B.confirm){
    MenuItem *m=&B.menu[B.selected];char line[100];snprintf(line,sizeof line,TR("Order %s for %d credit%s?"),m->name,m->price,m->price==1?"":"s");
    client_ui_paragraph(line,x+10,y+38,w-20,3,0x18272a);
    client_ui_paragraph(TR("Vic serves at a free stool. You pay when served."),x+10,y+100,w-20,3,0x18272a);
    client_ui_button(x+8,y+180,140,26,TR("Cancel"),CANCEL,0);client_ui_button(x+156,y+180,146,26,TR("Order"),!B.pending&&m->price>=0&&(u32)m->price<=W.credits?ORDER:0,0);return;
  }
  if(bar_holding()&&!B.menu_mode){
    Ent *me=world_ent(W.me);avatar_draw_held(me->drink,x+28,y+48,2);
    client_ui_label(smoke(me->drink)?TR("Your smoke"):TR("Your drink"),x+54,y+34,w-64,0x18272a);
    client_ui_button(x+8,y+68,90,26,smoke(me->drink)?TR("Puff"):TR("Sip"),USE,0);
    client_ui_button(x+108,y+68,90,26,TR("Cheers!"),smoke(me->drink)?0:TOAST,0);
    client_ui_button(x+208,y+68,94,26,TR("Put down"),DROP,0);
    client_ui_button(x+8,y+100,144,26,TR("Hand over"),PICK,0);client_ui_button(x+158,y+100,144,26,TR("Order another"),MENU,0);
    if(B.picking){
      int ids[MAX_ENTS],n=0;for(int i=0;i<MAX_ENTS;i++)if(giveable(&W.ents[i]))ids[n++]=i;
      if(B.page*2>=n)B.page=0;
      for(int row=0;row<2&&B.page*2+row<n;row++){Ent *e=&W.ents[ids[B.page*2+row]];client_ui_button(x+8,y+132+row*28,w-16,25,e->name,GIVE,(int)e->id);}
      if(n>2)client_ui_button(x+8,y+190,w-16,23,TR("More players"),PAGE,1);
      if(!n)client_ui_label(TR("Stand by someone with an empty hand."),x+10,y+142,w-20,0x18272a);
    }return;
  }
  if(!B.menu_n){
    if(!B.asked){B.asked=true;B.asked_at=now_ms();command("bar_menu",NULL);}
    client_ui_label(TR("Loading the bar menu..."),x+10,y+40,w-20,0x18272a);
    client_ui_button(x+8,y+180,w-16,26,TR("Retry"),now_ms()-B.asked_at>8000?REFRESH:0,0);return;
  }
  if(B.page*4>=B.menu_n)B.page=0;
  for(int row=0;row<4&&B.page*4+row<B.menu_n;row++){
    int i=B.page*4+row;MenuItem *m=&B.menu[i];char line[100];snprintf(line,sizeof line,TR("%s / %d credit%s"),m->name,m->price,m->price==1?"":"s");
    avatar_draw_held(m->id,x+24,y+44+row*36,1);client_ui_button(x+44,y+28+row*36,w-52,31,line,!B.pending?SELECT:0,i);
  }
  client_ui_button(x+8,y+184,140,26,TR("Previous"),B.page?PAGE:0,-1);
  client_ui_button(x+156,y+184,146,26,TR("Next"),(B.page+1)*4<B.menu_n?PAGE:0,1);
}
