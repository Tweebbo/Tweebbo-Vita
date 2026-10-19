#include "client_pets.h"
#include "client_ui.h"
#include "pets.h"
#include "gfx.h"
#include "net.h"
#include "world.h"
#include "show.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LIST, DETAIL, ADOPT, CONFIRM };
enum { SELECT=4000, PAGE, BACK, NEW, SPECIES, BREED, COLOR, NAME, BUY, CONFIRM_BUY, COMMAND, PLACE, VISIT, REFRESH };
static struct {int panel,page;u32 selected,revision;bool pending;u64 sent_at;Pet preview;char name[80],status[160];json_t *rows;} P;
static int number(json_t *v,const char *k) {return (int)json_integer_value(json_object_get(v,k));}
static const char *string(json_t *v,const char *k) {const char *s=json_string_value(json_object_get(v,k));return s?s:"";}
static json_t *selected(void) {size_t i;json_t *v;json_array_foreach(P.rows,i,v)if((u32)number(v,"id")==P.selected)return v;return NULL;}
static void panel(int value) {P.panel=value;P.status[0]=0;client_ui_changed();}
static void status(const char *s) {snprintf(P.status,sizeof P.status,"%s",s);show_toast(s,now_ms());}
static bool send(json_t *j) {
  char *body=json_dumps(j,JSON_COMPACT);json_decref(j);if(!body)return false;
  if(net_online()){send_pet(body);P.pending=true;P.revision=W.pet_inv_rev;P.sent_at=now_ms();P.status[0]=0;}
  free(body);return P.pending;
}
void client_pets_reset(void) {json_decref(P.rows);memset(&P,0,sizeof P);P.preview.dir=2;snprintf(P.preview.action,sizeof P.preview.action,"std");}
void client_pets_open(void) {panel(LIST);if(net_online())send_pet("{\"action\":\"list\"}");}
static bool name_done(const char *text) {
  int chars=0;for(const unsigned char *p=(const unsigned char *)text;*p;p++)if((*p&0xc0)!=0x80)chars++;
  if(chars<2 || chars>16){status("Use 2 to 16 letters, spaces or hyphens.");return false;}
  snprintf(P.name,sizeof P.name,"%s",text);return true;
}
static bool empty(int x,int y) {
  if(!world_tile(x,y) || (x==W.door_x && y==W.door_y) || world_item_at(x,y))return false;
  for(int i=0;i<MAX_ENTS;i++)if(W.ents[i].used && W.ents[i].x==x && W.ents[i].y==y)return false;
  for(int i=0;i<pets_count();i++)if(pet_at(i)->x==x && pet_at(i)->y==y)return false;
  return true;
}
void client_pets_action(int command,int arg) {
  if(command==BACK){panel(P.panel==CONFIRM?ADOPT:LIST);return;}
  if(command==PAGE){P.page+=arg;client_ui_changed();return;}
  if(command==SELECT){P.selected=(u32)arg;panel(DETAIL);return;}
  if(command==REFRESH){if(net_online())send_pet("{\"action\":\"list\"}");return;}
  if(command==NEW){panel(ADOPT);return;}
  if(P.pending || !net_online())return;
  const PetKind *k=&pet_kinds[P.preview.kind];
  if(command==SPECIES){P.preview.kind=(P.preview.kind+arg+pet_kind_count)%pet_kind_count;P.preview.breed=P.preview.color=0;return;}
  if(command==BREED){P.preview.breed=(P.preview.breed+arg+k->breeds)%k->breeds;return;}
  if(command==COLOR){P.preview.color=(P.preview.color+arg+k->color_count)%k->color_count;return;}
  if(command==NAME){client_ui_edit("Pet name (2 to 16 letters)",P.name,name_done);return;}
  if(command==BUY){if(*P.name && W.credits>=(u32)pet_price)panel(CONFIRM);return;}
  if(command==CONFIRM_BUY){
    if(*P.name && W.credits>=(u32)pet_price)send(json_pack("{s:s,s:i,s:i,s:i,s:s}","action","adopt","kind",P.preview.kind,"breed",P.preview.breed,"color",P.preview.color,"name",P.name));
    return;
  }
  json_t *pet=selected();if(!pet || (u32)number(pet,"owner")!=W.guest_id)return;
  const char *room=string(pet,"room");
  if(command==VISIT && *room){net_goto(public_slug(room),"");return;}
  if(command==PLACE && !*room && world_can_decorate()) {
    Ent *me=world_ent(W.me);if(!me)return;
    for(int radius=1;radius<=2;radius++)for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++) {
      int x=me->x+dx,y=me->y+dy;if(empty(x,y)){send(json_pack("{s:s,s:i,s:i,s:i}","action","place","id",(int)P.selected,"x",x,"y",y));return;}
    }
    status("Move to an empty floor area first.");return;
  }
  if(command==COMMAND && !strcmp(public_slug(room),public_slug(W.slug))) {
    static const char *actions[]={"follow","stay","sit","sleep","play","feed","drink","pickup"};
    if(arg>=0 && arg<COUNT(actions))send(json_pack("{s:s,s:i}","action",actions[arg],"id",(int)P.selected));
  }
}
void client_pets_notice(const char *text) {
  if(!P.pending || !text)return;
  static const char *errors[]={"The name must be ","That pet appearance is unavailable.","You already have 20 pets.","Payment failed.","You do not have enough credits.","Place your pet in a room you own.","Choose an empty floor tile.","Pick up the pet from its room first.","Only the owner can give orders", "Could not save the change."};
  for(int i=0;i<COUNT(errors);i++)if(!strncmp(text,errors[i],strlen(errors[i]))){P.pending=false;status(text);break;}
}
void client_pets_update(void) {
  pets_sync(now_ms());for(int i=0;i<pets_count();i++)pet_prepare(pet_at(i));
  if(P.revision!=W.pet_inv_rev) {
    P.revision=W.pet_inv_rev;json_decref(P.rows);P.rows=W.pet_inv_json?json_loads(W.pet_inv_json,0,NULL):NULL;
    if(P.pending){P.pending=false;if(P.panel==CONFIRM)panel(LIST);status("Pets updated.");}
  }
  if(P.pending && now_ms()-P.sent_at>10000 && !*P.status)status("No reply yet. Refresh your pet list.");
  if(P.panel==ADOPT || P.panel==CONFIRM)pet_prepare(&P.preview);
}
void client_pets_draw(float x,float y,float w) {
  char text[128];
  if(!net_online()){client_ui_paragraph("Connect to the hotel to see your pets.",x+10,y+40,w-20,3,0x18272a);return;}
  bool ready=!P.pending;
  if(P.panel==LIST) {
    client_ui_button(x+8,y+29,142,25,"Adopt a pet",ready?NEW:0,0);
    client_ui_button(x+156,y+29,146,25,"Refresh",REFRESH,0);
    int n=(int)json_array_size(P.rows),pages=(n+3)/4;if(pages<1)pages=1;if(P.page<0)P.page=0;if(P.page>=pages)P.page=pages-1;
    for(int row=0;row<4 && P.page*4+row<n;row++) {
      json_t *v=json_array_get(P.rows,P.page*4+row);
      snprintf(text,sizeof text,"%s - %s",string(v,"name"),*string(v,"room")?"In a room":"With you");
      client_ui_button(x+8,y+65+row*28,w-16,26,text,SELECT,number(v,"id"));
    }
    if(!n)client_ui_paragraph("Adopt a companion and bring them to your room.",x+10,y+80,w-20,3,0x26373a);
    client_ui_button(x+8,y+188,80,24,"Previous",P.page?PAGE:0,-1);client_ui_button(x+w-88,y+188,80,24,"Next",P.page+1<pages?PAGE:0,1);return;
  }
  client_ui_button(x+w-88,y+28,80,23,"Back",BACK,0);
  if(P.panel==ADOPT || P.panel==CONFIRM) {
    pet_draw(&P.preview,now_ms(),x+248,y+110,1);
    if(P.panel==CONFIRM) {
      snprintf(text,sizeof text,"Adopt %s for %d credits?",P.name,pet_price);client_ui_paragraph(text,x+10,y+65,186,4,0x18272a);
      client_ui_button(x+8,y+188,w-16,26,P.pending?"Adopting...":"Confirm adoption",ready?CONFIRM_BUY:0,0);return;
    }
    const PetKind *k=&pet_kinds[P.preview.kind];
    const char *labels[]={k->name,"Coat","Color"};int commands[]={SPECIES,BREED,COLOR};
    for(int i=0;i<3;i++) {
      client_ui_button(x+8,y+58+i*29,26,24,"<",ready?commands[i]:0,-1);
      snprintf(text,sizeof text,i?"%s %d":"%s",labels[i],i==1?P.preview.breed+1:P.preview.color+1);
      client_ui_label(text,x+40,y+63+i*29,132,0x18272a);
      client_ui_button(x+174,y+58+i*29,26,24,">",ready?commands[i]:0,1);
    }
    client_ui_field(x+8,y+151,w-16,26,P.name,"Name your pet",ready?NAME:0,0);
    snprintf(text,sizeof text,"Adopt - %d credits",pet_price);client_ui_button(x+8,y+188,w-16,26,text,ready&&*P.name&&W.credits>=(u32)pet_price?BUY:0,0);return;
  }
  json_t *pet=selected();if(!pet){client_ui_label("This pet is no longer available.",x+10,y+65,w-20,0x26373a);return;}
  client_ui_label(string(pet,"name"),x+10,y+32,w-110,0x18272a);
  static const char *keys[]={"hunger","thirst","energy","happiness"},*labels[]={"Fullness","Water","Energy","Mood"};
  for(int i=0;i<4;i++){int value=number(pet,keys[i]);if(i<2)value=100-value;snprintf(text,sizeof text,"%s: %d%%",labels[i],value);client_ui_label(text,x+10,y+62+i*19,w-20,0x26373a);}
  const char *room=string(pet,"room");
  if(!*room)client_ui_button(x+8,y+175,w-16,28,"Place near me",ready&&world_can_decorate()?PLACE:0,0);
  else if(strcmp(public_slug(room),public_slug(W.slug)))client_ui_button(x+8,y+175,w-16,28,"Visit room",VISIT,0);
  else {
    static const char *actions[]={"Follow","Stay","Sit","Sleep","Play","Feed","Water","Pick up"};
    for(int i=0;i<8;i++)client_ui_button(x+8+(i%4)*74,y+151+(i/4)*30,70,26,actions[i],ready?COMMAND:0,i);
  }
}
