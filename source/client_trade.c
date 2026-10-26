#include "language.h"
#include "client_trade.h"
#include "client_ui.h"
#include "client_social.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "show.h"
#include "world.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ACCEPT=5000, CANCEL, READY, INVENTORY, BACK, PAGE, INSPECT_MINE, INSPECT_PEER, INSPECT_INV, ADD, REMOVE, INVITE, DISMISS };
enum { EMPTY, INVITATION, OPEN, ENDED };
typedef struct {u32 id;u16 def;} OfferItem;
static struct {
  int stage,page,view,detail_side,pending;
  u32 seen,revision,peer,requested,item_id;
  OfferItem mine[12],theirs[12],detail;
  int mine_n,theirs_n;
  bool outgoing,ok_mine,ok_peer,confirming,was_online,interrupted,invalid,visible;
  u64 sent_at;
  char peer_name[80],note[192];
} T;

static void note(const char *text) {snprintf(T.note,sizeof T.note,"%s",text);}
static bool active(void) {return T.stage==INVITATION || T.stage==OPEN;}
static bool connected(void) {return W.in_room && net_online();}
static bool offered(u32 id) {
  for(int i=0;i<T.mine_n;i++)if(T.mine[i].id==id)return true;
  for(int i=0;i<T.theirs_n;i++)if(T.theirs[i].id==id)return true;
  return false;
}
static bool in_mine(u32 id) {for(int i=0;i<T.mine_n;i++)if(T.mine[i].id==id)return true;return false;}
static bool in_inventory(u32 id) {for(int i=0;i<W.inv_n;i++)if(W.inv[i].id==id)return true;return false;}
static void request(const char *action,int command,u32 id) {
  json_t *j=json_pack("{s:s,s:I,s:I,s:I}","action",action,"revision",(json_int_t)T.revision,
                      "target",(json_int_t)id,"id",(json_int_t)id);
  char *text=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
  if(!text){note("Could not prepare the request.");return;}
  send_trade(text);free(text);T.pending=command;T.requested=id;T.sent_at=now_ms();T.note[0]=0;if(T.visible)client_ui_changed();
}
void client_trade_reset(void) {memset(&T,0,sizeof T);T.seen=W.trade_rev;T.was_online=connected();}
void client_trade_invite(u32 target) {
  Ent *e=world_ent(target);
  if(!connected() || active() || T.pending || !e || target==W.me || !e->guest || e->guest==W.guest_id || e->guest>=4000000000u || client_social_blocked(e->guest))return;
  client_trade_reset();T.outgoing=true;T.peer=target;
  snprintf(T.peer_name,sizeof T.peer_name,"%s",e->name);
  request("invite",INVITE,target);
}
void client_trade_close(void) {
  if(connected() && (active() || T.pending || T.invalid) && T.pending!=CANCEL)request("cancel",CANCEL,0);
}
void client_trade_notice(const char *text) {
  if(T.pending==INVITE && (!strcmp(text,"That player cannot trade.") ||
      !strcmp(text,"That player needs the latest Tweebbo update on their console to trade.") ||
      !strcmp(text,"You or that player is already trading."))) {
    T.pending=0;T.stage=ENDED;T.outgoing=false;note(text);if(T.visible)client_ui_changed();
  } else if(T.pending==ADD && (!strcmp(text,"You can only trade furniture from your inventory.") ||
      !strcmp(text,"A trade holds at most 12 items per side."))) {
    T.pending=0;note(text);if(T.visible)client_ui_changed();
  }
}
static bool read_items(json_t *array,OfferItem *items,int *count) {
  if(!json_is_array(array) || json_array_size(array)>12)return false;
  *count=(int)json_array_size(array);
  for(int i=0;i<*count;i++) {
    json_t *v=json_array_get(array,i),*id=json_object_get(v,"id"),*def=json_object_get(v,"def");
    if(!json_is_integer(id) || !json_is_integer(def) || json_integer_value(id)<=0 || json_integer_value(id)>UINT32_MAX || json_integer_value(def)<0 || json_integer_value(def)>65535)return false;
    items[i]=(OfferItem){(u32)json_integer_value(id),(u16)json_integer_value(def)};
    for(int k=0;k<i;k++)if(items[k].id==items[i].id)return false;
  }
  return true;
}
static void receive(void) {
  json_t *j=W.trade_json?json_loads(W.trade_json,JSON_REJECT_DUPLICATES,NULL):NULL;
  const char *stage=json_string_value(json_object_get(j,"stage"));
  json_t *revision=json_object_get(j,"revision"),*peer=json_object_get(j,"peer");
  OfferItem mine[12]={0},theirs[12]={0};int mn=0,tn=0;
  bool valid=stage && (!strcmp(stage,"invite") || !strcmp(stage,"open") || !strcmp(stage,"done") || !strcmp(stage,"cancelled") || !strcmp(stage,"error")) &&
    json_is_integer(revision) && json_integer_value(revision)>0 && json_integer_value(revision)<=UINT32_MAX &&
    json_is_integer(peer) && json_integer_value(peer)>0 && json_integer_value(peer)<=UINT32_MAX &&
    json_is_boolean(json_object_get(j,"okMine")) && json_is_boolean(json_object_get(j,"okPeer")) && json_is_boolean(json_object_get(j,"confirming")) &&
    read_items(json_object_get(j,"mine"),mine,&mn) && read_items(json_object_get(j,"theirs"),theirs,&tn);
  for(int i=0;valid && i<mn;i++)for(int k=0;k<tn;k++)if(mine[i].id==theirs[k].id)valid=false;
  if(!valid) {T.invalid=true;note("Invalid trade update. Cancel and start again.");json_decref(j);return;}
  int old_stage=T.stage;u32 old_revision=T.revision;
  if(!strcmp(stage,"invite") && (T.stage!=INVITATION || T.peer!=(u32)json_integer_value(peer)))
    T.outgoing=T.pending==INVITE && T.requested==(u32)json_integer_value(peer);
  T.invalid=false;T.revision=(u32)json_integer_value(revision);T.peer=(u32)json_integer_value(peer);
  T.stage=!strcmp(stage,"invite")?INVITATION:!strcmp(stage,"open")?OPEN:ENDED;
  memcpy(T.mine,mine,sizeof mine);memcpy(T.theirs,theirs,sizeof theirs);T.mine_n=mn;T.theirs_n=tn;
  T.ok_mine=json_is_true(json_object_get(j,"okMine"));T.ok_peer=json_is_true(json_object_get(j,"okPeer"));T.confirming=json_is_true(json_object_get(j,"confirming"));
  const char *name=json_string_value(json_object_get(j,"peerName")),*message=json_string_value(json_object_get(j,"note"));
  snprintf(T.peer_name,sizeof T.peer_name,"%s",name?name:"Player");note(message?message:"");
  bool ack=T.stage==ENDED || (T.pending==INVITE && T.stage==INVITATION) || (T.pending==ACCEPT && T.stage==OPEN) ||
    (T.pending==ADD && in_mine(T.requested)) || (T.pending==REMOVE && !in_mine(T.requested)) ||
    (T.pending==READY && (T.ok_mine || T.revision!=old_revision));
  if(ack)T.pending=0;
  if(old_stage!=T.stage || old_revision!=T.revision){T.view=0;T.page=0;T.detail.id=0;}
  if(T.stage==ENDED)T.outgoing=false;
  json_decref(j);
}
void client_trade_update(bool visible) {
  T.visible=visible;
  bool online=connected();
  if(!online && T.was_online && (active() || T.pending))T.interrupted=true;
  T.was_online=online;
  if(T.seen!=W.trade_rev) {
    T.seen=W.trade_rev;receive();
    if(visible)client_ui_changed();
    Ent *peer=world_ent(T.peer);
    if(T.stage==INVITATION && peer && client_social_blocked(peer->guest))client_trade_close();
    if(!visible)show_toast("Trade updated. Open People > Trading.",now_ms());
  }
  if(online && T.interrupted) {
    T.interrupted=false;
    if(active() || T.pending)request("cancel",CANCEL,0);
  }
  if(T.pending && now_ms()-T.sent_at>10000)note("No reply. Cancel or reconnect.");
  if(visible && T.detail.id)gfx_furni_preload(T.detail.def);
}
void client_trade_action(int command,int arg) {
  if(command==PAGE){T.page+=arg;if(T.page<0)T.page=0;client_ui_changed();return;}
  if(command==BACK){T.detail.id=0;T.view=0;T.page=0;client_ui_changed();return;}
  if(command==INVENTORY && T.stage==OPEN){T.view=1;T.page=0;T.detail.id=0;client_ui_changed();return;}
  if(command==DISMISS && !active() && !T.pending){client_trade_reset();client_ui_changed();return;}
  if(command==CANCEL){client_trade_close();return;}
  if(command==INSPECT_MINE || command==INSPECT_PEER || command==INSPECT_INV) {
    OfferItem item={0};
    if(command==INSPECT_INV){for(int i=0;i<W.inv_n;i++)if(W.inv[i].id==(u32)arg)item=(OfferItem){W.inv[i].id,W.inv[i].def};}
    else {OfferItem *items=command==INSPECT_MINE?T.mine:T.theirs;int n=command==INSPECT_MINE?T.mine_n:T.theirs_n;for(int i=0;i<n;i++)if(items[i].id==(u32)arg)item=items[i];}
    if(item.id){T.detail=item;T.detail_side=command;client_ui_changed();}return;
  }
  if(!connected() || T.pending || T.invalid)return;
  if(command==INVITE){client_trade_invite((u32)arg);return;}
  if(command==ACCEPT && T.stage==INVITATION && !T.outgoing)request("accept",ACCEPT,0);
  if(T.stage!=OPEN)return;
  if(command==READY && !T.ok_mine && (T.mine_n || T.theirs_n) && T.revision)request(T.confirming?"confirm":"ready",READY,0);
  if(command==ADD && T.mine_n<12 && in_inventory((u32)arg) && !offered((u32)arg))request("add",ADD,(u32)arg);
  if(command==REMOVE && in_mine((u32)arg))request("remove",REMOVE,(u32)arg);
}
static const char *item_name(u16 def) {const FurniDef *d=def_get(def);return d?d->name:"Unknown furniture";}
static void pages(float x,float y,float w,int count,int size) {
  int n=(count+size-1)/size;if(n<1)n=1;if(T.page>=n)T.page=n-1;
  client_ui_button(x+8,y,w/2-12,23,TR("Previous"),T.page?PAGE:0,-1);
  client_ui_button(x+w/2+4,y,w/2-12,23,TR("Next"),T.page+1<n?PAGE:0,1);
}
void client_trade_draw(float x,float y,float w) {
  bool enabled=connected() && !T.pending && !T.invalid;char text[192];
  if(T.invalid) {
    client_ui_paragraph(T.note,x+10,y+35,w-20,5,0x18272a);
    client_ui_button(x+8,y+182,w-16,26,TR("Cancel trade"),connected()?CANCEL:0,0);return;
  }
  if(T.stage==EMPTY && !T.pending) {
    int n=0;u32 ids[MAX_ENTS];
    for(int i=0;i<MAX_ENTS;i++){Ent *e=&W.ents[i];if(e->used && e->id!=W.me && e->guest && e->guest!=W.guest_id && e->guest<4000000000u && !client_social_blocked(e->guest))ids[n++]=e->id;}
    client_ui_label(TR("Choose someone in this room."),x+10,y+30,w-20,0x26373a);
    pages(x,y+188,w,n,4);
    for(int i=0;i<4 && T.page*4+i<n;i++){Ent *e=world_ent(ids[T.page*4+i]);client_ui_button(x+8,y+56+i*29,w-16,26,e->name,enabled?INVITE:0,(int)e->id);}
    if(!n)client_ui_paragraph(TR("No other players available. Invite a friend into this room."),x+10,y+70,w-20,4,0x26373a);
    return;
  }
  if(T.stage==INVITATION || (T.stage==EMPTY && T.pending)) {
    snprintf(text,sizeof text,T.outgoing?TR("Waiting for %s to accept your invitation."):TR("%s wants to trade furniture with you."),T.peer_name);
    client_ui_paragraph(text,x+10,y+38,w-20,4,0x18272a);
    client_ui_button(x+8,y+170,140,26,T.outgoing?TR("Cancel"):TR("Decline"),connected()?CANCEL:0,0);
    if(!T.outgoing)client_ui_button(x+156,y+170,146,26,TR("Accept invitation"),enabled?ACCEPT:0,0);
  } else if(T.stage==ENDED) {
    client_ui_paragraph(*T.note?T.note:TR("The trade ended."),x+10,y+38,w-20,6,0x18272a);
    client_ui_button(x+8,y+182,w-16,26,TR("Back to players"),DISMISS,0);return;
  } else if(T.detail.id) {
    const FurniDef *d=def_get(T.detail.def);
    client_ui_paragraph(item_name(T.detail.def),x+102,y+34,w-112,4,0x18272a);
    snprintf(text,sizeof text,TR("Item #%u"),T.detail.id);client_ui_label(text,x+102,y+106,w-112,0x26373a);
    if(d && d->icon_w<=88 && d->icon_h<=88) {
      FurniPart part={.page=d->icon_page,.x=d->icon_x,.y=d->icon_y,.w=d->icon_w,.h=d->icon_h,.alpha=255};
      gfx_furni(d->id,&part,x+10+(88-d->icon_w)/2,y+36+(88-d->icon_h)/2,1,0xffffffff);
    }
    client_ui_button(x+8,y+174,140,26,TR("Back to offers"),BACK,0);
    if(T.detail_side!=INSPECT_PEER)client_ui_button(x+156,y+174,146,26,T.detail_side==INSPECT_INV?TR("Add to offer"):TR("Remove from offer"),enabled?(T.detail_side==INSPECT_INV && T.mine_n<12 && in_inventory(T.detail.id) && !offered(T.detail.id)?ADD:T.detail_side==INSPECT_MINE && in_mine(T.detail.id)?REMOVE:0):0,(int)T.detail.id);
  } else if(T.view==1) {
    InvItem items[MAX_INV];int n=0;
    for(int i=0;i<W.inv_n;i++)if(!offered(W.inv[i].id))items[n++]=W.inv[i];
    client_ui_label(TR("Inventory: choose an item to inspect."),x+10,y+29,w-20,0x26373a);
    pages(x,y+155,w,n,4);
    for(int i=0;i<4 && T.page*4+i<n;i++){InvItem it=items[T.page*4+i];client_ui_button(x+8,y+49+i*26,w-16,24,item_name(it.def),INSPECT_INV,(int)it.id);}
    if(!n)client_ui_label(TR("No unoffered furniture in inventory."),x+10,y+64,w-20,0x26373a);
    client_ui_button(x+8,y+184,w-16,26,TR("Back to offers"),BACK,0);
  } else {
    snprintf(text,sizeof text,TR("You: %d / 12"),T.mine_n);client_ui_label(text,x+10,y+29,140,0x18272a);
    client_ui_label(T.peer_name,x+160,y+29,w-170,0x18272a);
    int n=T.mine_n>T.theirs_n?T.mine_n:T.theirs_n;pages(x,y+131,w,n,3);
    for(int i=0;i<3;i++)for(int side=0;side<2;side++) {
      int index=T.page*3+i,count=side?T.theirs_n:T.mine_n;OfferItem *list=side?T.theirs:T.mine;
      client_ui_button(x+8+side*148,y+49+i*26,146,24,index<count?item_name(list[index].def):TR("(empty)"),index<count?(side?INSPECT_PEER:INSPECT_MINE):0,index<count?(int)list[index].id:0);
    }
    snprintf(text,sizeof text,TR("You: %s  Them: %s"),T.ok_mine?"accepted":"waiting",T.ok_peer?"accepted":"waiting");client_ui_label(text,x+10,y+158,w-20,0x26373a);
    client_ui_button(x+8,y+177,92,26,TR("Inventory"),INVENTORY,0);
    client_ui_button(x+104,y+177,112,26,T.confirming?TR("Confirm trade"):TR("Accept offer"),enabled&&!T.ok_mine&&(T.mine_n||T.theirs_n)?READY:0,0);
    client_ui_button(x+220,y+177,82,26,TR("Cancel"),connected()?CANCEL:0,0);
    if(connected() && !T.pending && !*T.note)client_ui_label(T.confirming?TR("Final review: changes reset both acceptances."):TR("Both players must accept, then confirm."),x+10,y+207,w-20,0x26373a);
  }
  if(!connected())client_ui_label(TR("Disconnected. Reconnect to cancel safely."),x+10,y+211,w-20,0x26373a);
  else if(*T.note || T.pending)client_ui_label(*T.note?T.note:TR("Waiting for the hotel..."),x+10,y+211,w-20,0x26373a);
}
