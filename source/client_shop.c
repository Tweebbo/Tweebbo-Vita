#include "client_shop.h"
#include "client_build.h"
#include "client_ui.h"
#include "client_casino.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "show.h"
#include "world.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { CATEGORIES, OFFERS, OFFER, INVENTORY, OWNED, ROOM_ITEMS, ROOM_ITEM, BUILD, CONFIRM_ITEM, SPACES, CONFIRM_FINISH, INTERACT };
enum { SELECT_CATEGORY=1000, TURN_PAGE, BACK, SELECT_OFFER, BUY, SELECT_OWNED, PLACE,
       SELECT_ITEM, MOVE_ITEM, ROTATE_ITEM, PICKUP_ITEM, REDEEM_ITEM, CONFIRM_ITEM_ACTION, ROTATE_GHOST, PLACE_GHOST, CANCEL_GHOST,
       FINISH_KIND, FINISH_STEP, FINISH_PREVIEW, FINISH_BUY, FINISH_CONFIRM,
       SEARCH_OFFERS, INTERACTIONS, USE_ITEM, CLOSE_DICE, DRINK_STEP, GET_DRINK, OPEN_CASINO };
static struct {
  int panel, page, category, offer, confirm;
  u32 item;
  int pending;
  u32 expected, baseline;
  u64 sent_at;
  bool timed_out;
  char status[160];
  BuildGhost ghost;
  int finish;
  bool visible, preview;
  char query[128];
  int drink;
} S;

static const FinishChoice *finish(void) {return S.finish>=0 && S.finish<finish_choice_count?&finish_choices[S.finish]:NULL;}
void client_shop_visible(bool visible) {S.visible=visible;}
static bool previewing(int kind) {return S.visible && S.preview && (S.panel==SPACES || S.panel==CONFIRM_FINISH) && finish() && finish()->kind==kind && W.in_room && W.room_owner;}
const char *client_shop_wall(void) {return previewing(1)?finish()->id:W.wall;}
const char *client_shop_floor(void) {return previewing(2)?finish()->id:W.floor;}
static bool can_buy_finish(void) {
  const FinishChoice *f=finish();
  return f && W.in_room && net_online() && W.room_owner && W.credits>=(u32)finish_price &&
    strcmp(f->id,f->kind==1?W.wall:W.floor) && !S.pending;
}

static void status(const char *s) {snprintf(S.status,sizeof S.status,"%s",s);show_toast(s,now_ms());}
static void panel(int p) {S.panel=p;S.page=0;S.status[0]=0;client_ui_changed();}
static bool connected(void) {return W.in_room && net_online();}
static bool interaction_ready(const FurniDef *d) {
  if(!W.in_room || !d)return false;
  if(net_online())return true;
  return net_state()==NET_OFFLINE && !(d->flags&(DEF_LAY|DEF_TELEPORTER)) &&
    (d->drink || d->kind==KIND_SIT || d->kind==KIND_FLOOR || d->kind==KIND_TOGGLE);
}
static const CatOffer *offer(void) {return S.offer>=0 && S.offer<cat_offer_count?&cat_offers[S.offer]:NULL;}
static InvItem *owned(void) {for(int i=0;i<W.inv_n;i++)if(W.inv[i].id==S.item)return &W.inv[i];return NULL;}
static int inventory_count(u16 def) {int n=0;for(int i=0;i<W.inv_n;i++)n+=W.inv[i].def==def;return n;}
static bool matches(const char *text,const char *query) {
  if(!*query)return true;
  for(const unsigned char *p=(const unsigned char *)(text?text:"");*p;p++) {
    const unsigned char *a=p,*b=(const unsigned char *)query;
    while(*a && *b && tolower(*a)==tolower(*b)){a++;b++;}
    if(!*b)return true;
  }return false;
}
static bool search_offers(const char *text) {
  if(strlen(text)>=sizeof S.query)return false;
  snprintf(S.query,sizeof S.query,"%s",text);S.category=-1;panel(OFFERS);return true;
}
static int drinks(const FurniDef *d,int *ids) {
  int n=0;const char *p=d?d->drinks:NULL;
  while(p && *p && n<32) {
    char *end;long id=strtol(p,&end,10);
    if(end==p || id<=0 || id>255 || (*end && *end!=','))return 0;
    ids[n++]=(int)id;p=*end?end+1:end;
  }return n;
}
static const char *drink_label(int id) {for(int i=0;i<drink_name_count;i++)if(drink_names[i].id==id)return drink_names[i].name;return "Drink";}
static const char *use_label(const FurniDef *d,const Item *it) {
  if(!d || d->casino)return NULL;
  if(d->flags&DEF_TELEPORTER)return "Enter teleporter";
  if(d->flags&DEF_LAY)return "Lie down";
  if(d->kind==KIND_SIT)return "Sit on furniture";
  if(d->kind==KIND_FLOOR)return "Walk here";
  if(d->kind==KIND_DICE)return it->state==7?"Rolling...":"Roll dice";
  if(d->kind==KIND_TOGGLE || (d->kind==KIND_WALL && d->states>1))return d->states>2?"Change state":it->state?"Turn off / close":"Turn on / open";
  return NULL;
}
static int page_offers(int *out, int cap) {
  if(S.category==-1) {
    int n=0;for(int i=0;i<cat_offer_count && n<cap;i++)if(matches(cat_offers[i].name,S.query) || matches(cat_offers[i].desc,S.query))out[n++]=i;
    return n;
  }
  if(S.category<0 || S.category>=cat_page_count)return 0;
  char key[128];snprintf(key,sizeof key,",%s,",cat_pages[S.category].id);
  int n=0;for(int i=0;i<cat_offer_count && n<cap;i++)if(strstr(cat_offers[i].pages,key))out[n++]=i;
  return n;
}
static void pending(int command, u32 expected, u32 baseline) {
  S.pending=command;S.expected=expected;S.baseline=baseline;S.sent_at=now_ms();S.timed_out=false;
  status("Waiting for the hotel...");client_ui_changed();
}
void client_shop_reset(void) {memset(&S,0,sizeof S);}
void client_shop_open(int section) {
  if(S.ghost.on) {panel(BUILD);return;}
  panel(section==SHOP_CATALOGUE?CATEGORIES:section==SHOP_INVENTORY?INVENTORY:ROOM_ITEMS);
}
void client_shop_inspect(u32 item) {if(S.ghost.on || !world_item(item))return;S.item=item;S.drink=0;panel(INTERACT);}
bool client_shop_building(void) {return S.ghost.on && S.panel==BUILD;}
const BuildGhost *client_shop_ghost(void) {return &S.ghost;}
static void start_build(u16 def, u32 id, bool moving) {
  const FurniDef *d=def_get(def);Item *it=moving?world_item(id):NULL;
  if(!d || !connected() || !world_can_decorate() || (moving && (!it || !world_can_arrange(it)))) {status("You cannot arrange furniture here.");return;}
  S.item=id;
  S.ghost=(BuildGhost){.on=true,.def=def,.wall=d->kind==KIND_WALL,.ignore=moving?id:0};
  Ent *me=world_ent(W.me);
  S.ghost.x=it?it->x:me?me->x:0;S.ghost.y=it?it->y:me?me->y:0;
  S.ghost.rot=it?it->rot:def_rotation(def,0);S.ghost.z=it?it->z:60;
  if(S.ghost.wall)client_build_wall(&S.ghost,0,true);
  S.ghost.ok=client_build_valid(&S.ghost);panel(BUILD);
}
static void rotate(int step) {
  if(S.pending)return;
  if(S.ghost.wall) {S.ghost.z+=step*6;if(S.ghost.z<8)S.ghost.z=8;if(S.ghost.z>110)S.ghost.z=110;}
  else {
    for(int i=1;i<=4;i++) {
      int r=(S.ghost.rot+step*i+16)%4;
      if(def_rotation(S.ghost.def,r)==r) {S.ghost.rot=r;break;}
    }
  }
  S.ghost.ok=client_build_valid(&S.ghost);
}
void client_shop_action(int cmd, int arg) {
  if(cmd==TURN_PAGE) {S.page+=arg;if(S.page<0)S.page=0;client_ui_changed();return;}
  if(cmd==BACK) {
    if(S.ghost.on) {if(S.pending)return;S.ghost.on=false;panel(S.ghost.ignore?ROOM_ITEMS:INVENTORY);return;}
    panel(S.panel==INTERACT?ROOM_ITEM:S.panel==OFFER?OFFERS:S.panel==OWNED?INVENTORY:S.panel==ROOM_ITEM?ROOM_ITEMS:S.panel==CONFIRM_ITEM?ROOM_ITEM:S.panel==CONFIRM_FINISH?SPACES:CATEGORIES);return;
  }
  if(cmd==SEARCH_OFFERS){client_ui_edit("Search catalogue",S.query,search_offers);return;}
  if(cmd==SELECT_CATEGORY && arg>=0 && arg<cat_page_count) {S.category=arg;panel(!strcmp(cat_pages[arg].layout,"ctlg_spaces")?SPACES:OFFERS);return;}
  if(cmd==SELECT_OFFER && arg>=0 && arg<cat_offer_count) {S.offer=arg;panel(OFFER);return;}
  if(cmd==SELECT_OWNED) {S.item=(u32)arg;panel(OWNED);return;}
  if(cmd==SELECT_ITEM) {S.item=(u32)arg;panel(ROOM_ITEM);return;}
  if(S.pending)return;
  const CatOffer *o=offer();Item *it=world_item(S.item);InvItem *inv=owned();
  switch(cmd) {
    case INTERACTIONS:client_shop_inspect(S.item);break;
    case OPEN_CASINO:client_casino_open(S.item);break;
    case DRINK_STEP: {
      int ids[32],n=drinks(it?def_get(it->def):NULL,ids);
      if(n && (arg==1 || arg==-1))S.drink=(S.drink+arg+n)%n;
      break;
    }
    case GET_DRINK: {
      int ids[32],n=drinks(it?def_get(it->def):NULL,ids);
      if(interaction_ready(it?def_get(it->def):NULL) && n && S.drink>=0 && S.drink<n){send_drink(it->id,ids[S.drink]);client_ui_close();}
      break;
    }
    case USE_ITEM: case CLOSE_DICE: {
      const FurniDef *d=it?def_get(it->def):NULL;
      if(!interaction_ready(d) || !use_label(d,it))break;
      if(d->kind==KIND_DICE && (it->state==7 || (cmd==CLOSE_DICE && !it->state)))break;
      if(cmd==CLOSE_DICE && d->kind!=KIND_DICE)break;
      if(net_state()==NET_OFFLINE && (d->kind==KIND_SIT || d->kind==KIND_FLOOR))send_walk(it->x,it->y);
      else send_use_op(it->id,cmd==CLOSE_DICE?1:0);
      client_ui_close();break;
    }
    case FINISH_KIND:
      for(int i=0;i<finish_choice_count;i++)if(finish_choices[i].kind==arg){S.finish=i;break;}
      client_ui_changed();break;
    case FINISH_STEP:
      if(finish() && (arg==1 || arg==-1)) {
        int kind=finish()->kind;
        for(int n=1;n<=finish_choice_count;n++) {
          int i=(S.finish+arg*n+finish_choice_count)%finish_choice_count;
          if(finish_choices[i].kind==kind){S.finish=i;break;}
        }
      }client_ui_changed();break;
    case FINISH_PREVIEW:S.preview=!S.preview;client_ui_changed();break;
    case FINISH_BUY:if(can_buy_finish())panel(CONFIRM_FINISH);break;
    case FINISH_CONFIRM:
      if(S.panel==CONFIRM_FINISH && can_buy_finish()) {
        pending(cmd,(u32)S.finish,W.finish_rev);send_decorate(finish()->kind,finish()->id);
      }break;
    case BUY:
      if(connected() && o && o->available && W.credits>=o->price) {pending(BUY,o->def,inventory_count(o->def));send_buy(o->id);}break;
    case PLACE: if(inv)start_build(inv->def,inv->id,false);break;
    case MOVE_ITEM: if(it)start_build(it->def,it->id,true);break;
    case ROTATE_ITEM:
      if(connected() && it && world_can_decorate() && world_can_arrange(it)) {pending(cmd,it->id,it->rot);send_rotate(it->id);}break;
    case PICKUP_ITEM: case REDEEM_ITEM:
      if(connected() && it && world_can_manage(it) && (cmd!=REDEEM_ITEM || it->owner==W.guest_id)) {S.confirm=cmd;panel(CONFIRM_ITEM);}break;
    case CONFIRM_ITEM_ACTION:
      if(connected() && it && world_can_manage(it)) {
        const FurniDef *d=def_get(it->def);
        if(S.confirm==REDEEM_ITEM && (!d || !((d->flags&DEF_REDEEM) || d->chips) || it->owner!=W.guest_id))break;
        pending(S.confirm,it->id,0);
        if(S.confirm==REDEEM_ITEM)send_redeem(it->id);else send_pickup(it->id);
      }break;
    case ROTATE_GHOST:rotate(arg);break;
    case PLACE_GHOST:
      if(!connected()) {status("Wait until the room reconnects.");break;}
      S.ghost.ok=client_build_valid(&S.ghost);
      if(!S.ghost.ok) {status("Choose a free spot.");break;}
      if(!S.ghost.ignore && !owned()) {status("This item is no longer in your inventory.");break;}
      pending(cmd,S.item,0);
      if(S.ghost.ignore)send_move(S.item,S.ghost.x,S.ghost.y,S.ghost.rot,S.ghost.z);
      else send_place(S.item,S.ghost.x,S.ghost.y,S.ghost.rot,S.ghost.z);
      break;
    case CANCEL_GHOST:S.ghost.on=false;panel(S.ghost.ignore?ROOM_ITEMS:INVENTORY);break;
  }
}
void client_shop_item(u32 id) {
  if(!S.pending || id!=S.expected)return;
  Item *it=world_item(id);
  if(!it)return;
  bool placed=S.pending==PLACE_GHOST && it->x==S.ghost.x && it->y==S.ghost.y && it->rot==S.ghost.rot && it->z==S.ghost.z;
  if(placed || S.pending==ROTATE_ITEM) {
    S.pending=0;if(placed){S.ghost.on=false;panel(ROOM_ITEM);}status("Furniture updated.");
  }
}
void client_shop_notice(const char *text) {
  if(!text || !S.pending)return;
  bool purchase_error=!strcmp(text,"You do not have enough credits.") || !strcmp(text,"This item's resources are not all available yet.");
  bool furniture_error=!strcmp(text,"This item is occupied.") || !strcmp(text,"The bed is occupied.")
    || !strcmp(text,"Remove the items stacked on top first.") || !strcmp(text,"The item does not fit there.")
    || !strcmp(text,"There is no room to rotate it.") || !strncmp(text,"Furniture can only be ",22)
    || !strcmp(text,"Only the player who placed this item or the room owner can pick it up.");
  bool finish_error=purchase_error || !strcmp(text,"This room already has it.") || !strcmp(text,"Only the room owner can change the wallpaper and the floor.");
  if((S.pending==BUY && purchase_error) || (S.pending==FINISH_CONFIRM && finish_error) || (S.pending!=BUY && furniture_error)) {
    S.pending=0;S.timed_out=false;status(text);client_ui_changed();
  }
}
void client_shop_update(void) {
  if(S.pending==FINISH_CONFIRM && W.finish_rev!=S.baseline) {
    const FinishChoice *f=&finish_choices[S.expected];
    bool applied=!strcmp(f->id,f->kind==1?W.wall:W.floor);
    if(applied) {S.pending=0;panel(SPACES);status("Room finish purchased.");}
  }
  if(S.pending==BUY && inventory_count((u16)S.expected)>(int)S.baseline) {S.pending=0;status("Purchased. Open Inventory to place it.");client_ui_changed();}
  if((S.pending==PICKUP_ITEM || S.pending==REDEEM_ITEM) && !world_item(S.expected)) {
    S.pending=0;panel(ROOM_ITEMS);status("Furniture updated.");
  }
  if(S.pending && now_ms()-S.sent_at>10000 && !S.timed_out) {
    S.timed_out=true;status("No reply yet. Reconnect before retrying.");
  }
  if(S.ghost.on)S.ghost.ok=client_build_valid(&S.ghost);
  u16 def=0;
  if(S.panel==OFFER && offer())def=offer()->def;
  else if(S.panel==OWNED && owned())def=owned()->def;
  else if((S.panel==ROOM_ITEM || S.panel==INTERACT || S.panel==CONFIRM_ITEM) && world_item(S.item))def=world_item(S.item)->def;
  else if(S.ghost.on)def=S.ghost.def;
  if(def)gfx_furni_preload(def);
}
bool client_shop_move(int dx, int dy) {
  if(!client_shop_building())return false;
  if(S.pending)return true;
  if(S.ghost.wall)client_build_wall(&S.ghost,dx+dy>0?1:-1,false);
  else {int x=S.ghost.x+dx,y=S.ghost.y+dy;if(x>=0 && y>=0 && x<W.w && y<W.h){S.ghost.x=x;S.ghost.y=y;}}
  S.status[0]=0;S.ghost.ok=client_build_valid(&S.ghost);return true;
}
bool client_shop_key(int action) {
  if(!client_shop_building())return false;
  if(action==SHOW_A)client_shop_action(PLACE_GHOST,0);
  if(action==SHOW_B)client_shop_action(CANCEL_GHOST,0);
  if(action==SHOW_L || action==SHOW_R)rotate(action==SHOW_L?-1:1);
  return action!=SHOW_OFFLINE;
}
void client_shop_tile(int x, int y) {
  if(!client_shop_building() || S.pending)return;
  S.ghost.x=x;S.ghost.y=y;
  if(S.ghost.wall)client_build_wall(&S.ghost,0,true);
  S.ghost.ok=client_build_valid(&S.ghost);
}
static void icon(u16 def, float x, float y) {
  const FurniDef *d=def_get(def);if(!d || d->icon_w>88 || d->icon_h>88)return;
  FurniPart p={.page=d->icon_page,.x=d->icon_x,.y=d->icon_y,.w=d->icon_w,.h=d->icon_h,.alpha=255};
  gfx_furni(def,&p,x+(88-d->icon_w)/2,y+(88-d->icon_h)/2,1,0xffffffff);
}
static void pages(float x, float y, float w, int count) {
  int n=(count+4)/5;if(n<1)n=1;if(S.page>=n)S.page=n-1;
  char s[32];snprintf(s,sizeof s,"%d / %d",S.page+1,n);
  client_ui_button(x+8,y+188,80,24,"Previous",S.page?TURN_PAGE:0,-1);
  client_ui_label(s,x+115,y+194,90,0x18272a);
  client_ui_button(x+w-88,y+188,80,24,"Next",S.page+1<n?TURN_PAGE:0,1);
}
void client_shop_draw(float x, float y, float w) {
  char line[256];const CatOffer *o=offer();Item *it=world_item(S.item);InvItem *inv=owned();
  if(S.panel==INTERACT) {
    const FurniDef *d=it?def_get(it->def):NULL;int ids[32],n=drinks(d,ids);
    client_ui_label(d?d->name:"Item no longer available",x+10,y+30,w-20,0x18272a);
    icon(it?it->def:0,x+8,y+55);
    if(n) {
      if(S.drink<0 || S.drink>=n)S.drink=0;
      client_ui_paragraph(drink_label(ids[S.drink]),x+104,y+59,w-114,3,0x18272a);
      client_ui_button(x+104,y+114,94,24,"Previous",n>1?DRINK_STEP:0,-1);
      client_ui_button(x+204,y+114,w-212,24,"Next",n>1?DRINK_STEP:0,1);
      client_ui_button(x+104,y+148,w-112,26,"Get a drink",interaction_ready(d)&&!S.pending?GET_DRINK:0,0);
    } else {
      const char *action=use_label(d,it);
      if(d && d->casino)client_ui_button(x+104,y+60,w-112,26,"Open casino game",connected()?OPEN_CASINO:0,0);
      else if(action)client_ui_button(x+104,y+60,w-112,26,action,interaction_ready(d)&&!S.pending&&!(d->kind==KIND_DICE && it->state==7)?USE_ITEM:0,0);
      else client_ui_paragraph("No direct action for this furniture.",x+104,y+60,w-114,4,0x26373a);
      if(d && d->kind==KIND_DICE && it->state && it->state!=7)client_ui_button(x+104,y+94,w-112,26,"Close dice",connected()&&!S.pending?CLOSE_DICE:0,0);
      if(d && d->desc)client_ui_paragraph(d->desc,x+10,y+150,w-20,2,0x26373a);
      if(d && net_state()==NET_OFFLINE && !interaction_ready(d))client_ui_label("This action requires a hotel connection.",x+10,y+174,w-20,0x26373a);
    }
    client_ui_button(x+8,y+188,w-16,24,"Furniture details",BACK,0);return;
  }
  if(S.panel==SPACES || S.panel==CONFIRM_FINISH) {
    const FinishChoice *f=finish();if(!f)return;
    client_ui_label("Spaces",x+10,y+30,w-20,0x18272a);
    client_ui_button(x+8,y+53,140,24,"Wallpaper",!S.pending?FINISH_KIND:0,1);
    client_ui_button(x+156,y+53,146,24,"Floor",!S.pending?FINISH_KIND:0,2);
    fill_rect(x+12,y+88,32,32,0xff000000|((f->color&255)<<16)|(f->color&0xff00)|((f->color>>16)&255));
    snprintf(line,sizeof line,"%s %s",f->kind==1?"Wallpaper":"Floor",f->id);
    client_ui_label(line,x+52,y+90,w-62,0x18272a);
    snprintf(line,sizeof line,"%d credits / balance %u",finish_price,W.credits);
    client_ui_label(line,x+52,y+109,w-62,0x26373a);
    if(S.panel==SPACES) {
      client_ui_button(x+8,y+130,72,24,"Previous",!S.pending?FINISH_STEP:0,-1);
      client_ui_button(x+86,y+130,72,24,"Next",!S.pending?FINISH_STEP:0,1);
      client_ui_button(x+164,y+130,138,24,S.preview?"End preview":"Room preview",W.room_owner&&!S.pending?FINISH_PREVIEW:0,0);
    } else client_ui_label("Apply this finish to the room?",x+10,y+135,w-20,0x18272a);
    const char *note=S.pending?S.status:!W.room_owner?"Enter a room you own to apply it.":!strcmp(f->id,f->kind==1?W.wall:W.floor)?"Already applied.":W.credits<(u32)finish_price?"Not enough credits.":"Purchase replaces the current finish.";
    client_ui_label(note,x+10,y+165,w-20,0x26373a);
    client_ui_button(x+8,y+188,80,24,"Back",BACK,0);
    client_ui_button(x+154,y+188,148,24,S.panel==SPACES?"Buy finish":"Confirm purchase",can_buy_finish()?(S.panel==SPACES?FINISH_BUY:FINISH_CONFIRM):0,0);
    return;
  }
  if(S.panel==CATEGORIES || S.panel==OFFERS || S.panel==INVENTORY || S.panel==ROOM_ITEMS) {
    int indices[4096],n=0;
    if(S.panel==CATEGORIES)n=cat_page_count;
    if(S.panel==OFFERS)n=page_offers(indices,COUNT(indices));
    if(S.panel==INVENTORY)n=W.inv_n;
    if(S.panel==ROOM_ITEMS)for(int i=0;i<MAX_ITEMS;i++)if(W.items[i].used)indices[n++]=i;
    int count=(n+4)/5;if(S.page>=count)S.page=count?count-1:0;
    const char *title=S.panel==CATEGORIES?"Catalogue":S.panel==OFFERS?(S.category==-1?"Search results":cat_pages[S.category].name):S.panel==INVENTORY?"Inventory":"Room furniture";
    client_ui_label(title,x+10,y+30,w-98,0x18272a);
    if(S.panel==OFFERS)client_ui_button(x+w-86,y+26,78,22,"Sections",BACK,0);
    if(S.panel==CATEGORIES)client_ui_button(x+w-86,y+26,78,22,"Search",SEARCH_OFFERS,0);
    if(!n)client_ui_label(S.panel==INVENTORY?"Your inventory is empty.":"No items here.",x+10,y+67,w-20,0x26373a);
    for(int row=0;row<5 && S.page*5+row<n;row++) {
      int i=S.page*5+row,cmd,arg;const char *name;
      if(S.panel==CATEGORIES){cmd=SELECT_CATEGORY;arg=i;name=cat_pages[i].name;}
      else if(S.panel==OFFERS){const CatOffer *a=&cat_offers[indices[i]];cmd=SELECT_OFFER;arg=indices[i];snprintf(line,sizeof line,"%s - %u credits",a->name,a->price);name=line;}
      else {
        u16 def;u32 id;
        if(S.panel==INVENTORY){def=W.inv[i].def;id=W.inv[i].id;cmd=SELECT_OWNED;}
        else {Item *a=&W.items[indices[i]];def=a->def;id=a->id;cmd=SELECT_ITEM;}
        const FurniDef *d=def_get(def);snprintf(line,sizeof line,"%s #%u",d?d->name:"Item",id);name=line;arg=(int)id;
      }
      client_ui_button(x+8,y+56+row*25,w-16,23,name,cmd,arg);
    }
    pages(x,y,w,n);
  } else {
    u16 def=S.panel==OFFER&&o?o->def:S.panel==OWNED&&inv?inv->def:S.panel==BUILD?S.ghost.def:it?it->def:0;
    const FurniDef *d=def_get(def);
    client_ui_label(S.panel==OFFER&&o?o->name:d?d->name:"Item no longer available",x+10,y+30,w-20,0x18272a);
    icon(def,x+8,y+53);
    if(S.panel==OFFER && o) {
      snprintf(line,sizeof line,"%u credits / %u item%s",o->price,o->amount,o->amount==1?"":"s");client_ui_label(line,x+104,y+57,w-114,0x18272a);
      snprintf(line,sizeof line,"Balance: %u",W.credits);client_ui_label(line,x+104,y+81,w-114,0x26373a);
      client_ui_label(o->available?W.credits>=o->price?"Confirm to purchase.":"Not enough credits.":"Not available.",x+104,y+105,w-114,0x26373a);
      client_ui_paragraph(o->desc,x+10,y+145,w-20,2,0x26373a);
      client_ui_button(x+w-144,y+188,136,24,"Confirm purchase",connected()&&o->available&&W.credits>=o->price&&!S.pending?BUY:0,0);
    } else if(S.panel==OWNED) {
      client_ui_label(world_can_decorate()?"Place this item in the room.":"Enter a room you can decorate.",x+104,y+62,w-114,0x26373a);
      client_ui_button(x+104,y+100,w-112,26,"Place",inv&&connected()&&world_can_decorate()&&!S.pending?PLACE:0,0);
    } else if(S.panel==BUILD) {
      snprintf(line,sizeof line,"Tile %d, %d / height %d",S.ghost.x,S.ghost.y,S.ghost.z);client_ui_label(line,x+104,y+57,w-114,0x18272a);
      client_ui_label(S.ghost.ok?"Free spot":"Choose a free spot",x+104,y+81,w-114,0x26373a);
      client_ui_label("D-pad / stick: move cursor",x+10,y+147,w-20,0x26373a);
      client_ui_label(S.ghost.wall?"L / R: lower / raise":"L / R: rotate",x+10,y+165,w-20,0x26373a);
      client_ui_button(x+96,y+188,102,24,S.ghost.wall?"Raise":"Rotate",!S.pending?ROTATE_GHOST:0,1);
      client_ui_button(x+204,y+188,w-212,24,"Place",S.ghost.ok&&!S.pending?PLACE_GHOST:0,0);
    } else if(S.panel==CONFIRM_ITEM) {
      // Chip tokens (Tweebbo Exchange) pay chips; the server keeps a token received in a trade
      // locked for 24 hours (it can still be traded) and says how long is left.
      const FurniDef *cd=it?def_get(it->def):NULL;
      char question[96];
      if(cd&&cd->chips)snprintf(question,sizeof question,"Redeem this token for %lu chips?",(unsigned long)cd->chips);
      client_ui_paragraph(S.confirm!=REDEEM_ITEM?"Return this item to inventory?":cd&&cd->chips?question:"Redeem this item for credits?",x+104,y+62,w-114,3,0x18272a);
      client_ui_label(S.confirm!=REDEEM_ITEM?"Its owner receives the item.":cd&&cd->chips?"Tokens received in a trade: 24 h before redeeming.":"The item will be consumed.",x+10,y+151,w-20,0x26373a);
      client_ui_button(x+w-144,y+188,136,24,"Confirm",it&&!S.pending?CONFIRM_ITEM_ACTION:0,0);
    } else if(S.panel==ROOM_ITEM && it) {
      bool arrange=connected()&&world_can_decorate()&&world_can_arrange(it)&&!S.pending,manage=connected()&&world_can_manage(it)&&!S.pending;
      client_ui_button(x+104,y+55,w-112,24,"Move",arrange?MOVE_ITEM:0,0);
      client_ui_button(x+104,y+83,w-112,24,"Rotate",arrange?ROTATE_ITEM:0,0);
      client_ui_button(x+104,y+111,w-112,24,"Pick up",manage?PICKUP_ITEM:0,0);
      if(d && ((d->flags&DEF_REDEEM) || d->chips))client_ui_button(x+104,y+139,w-112,24,"Redeem",manage&&it->owner==W.guest_id?REDEEM_ITEM:0,0);
      client_ui_button(x+96,y+188,w-104,24,"Interact",!S.pending?INTERACTIONS:0,0);
    }
    client_ui_button(x+8,y+188,80,24,"Back",S.ghost.on&&S.pending?0:BACK,0);
  }
}
