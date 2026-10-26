#include "language.h"
#include "client_identity.h"
#include "client_ui.h"
#include "net.h"
#include "world.h"
#include "gfx.h"
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static struct {
  u32 guest,job_guest;int job,kind;
  bool vip,staff,club,gold,restore,picking;
  int count,choice;char codes[8][16];
  u64 reveal_until;
  char token[80],name[80],status[128];
} I;
enum { REFRESH=10000, REVEAL, RESTORE, APPLY, CANCEL, VISIBLE, PRESENCE, PICK_BADGE, CHOOSE_BADGE, SAVE_BADGE };
static void wipe(char *s,size_t n){volatile char *p=s;while(n--)*p++=0;}
void client_identity_close(void){I.reveal_until=0;I.restore=false;I.picking=false;wipe(I.token,sizeof I.token);}
void client_identity_reset(void){wipe((char *)&I,sizeof I);}
static void refresh(void){
  if(I.job||!net_online())return;
  char path[64];snprintf(path,sizeof path,"/api/badges?guest=%u",I.guest);
  I.job=net_http("GET",path,NULL,false);I.kind=0;I.job_guest=I.guest;
  if(!I.job)snprintf(I.status,sizeof I.status,"%s",TR("Could not request badges. Try Refresh."));
}
void client_identity_open(u32 guest){client_identity_close();I.guest=guest;I.vip=I.staff=I.club=I.gold=false;I.status[0]=0;refresh();}
static bool restore_submit(const char *code){
  if(code && !strncmp(code,"TWEEBBO-",8))code+=8;
  if(I.job||!net_online()||!code||strlen(code)<16||strlen(code)>=sizeof I.token)return false;
  json_t *j=json_pack("{s:s}","token",code);char *body=j?json_dumps(j,JSON_COMPACT):NULL;json_decref(j);
  int job=body?net_http("POST","/api/identity",body,false):0;
  if(body){wipe(body,strlen(body));free(body);}
  if(!job)return false;
  snprintf(I.token,sizeof I.token,"%s",code);I.job=job;I.kind=1;I.restore=false;return true;
}
void client_identity_update(void){
  if(!net_online())client_identity_close();
  HttpResult r;if(!I.job||!net_http_result(I.job,&r))return;
  I.job=0;json_t *j=r.body?json_loads(r.body,0,NULL):NULL;
  if(I.kind==0){
    if(I.job_guest!=I.guest){json_decref(j);free(r.body);refresh();return;}
    I.vip=I.staff=I.club=I.gold=false;I.count=I.choice=0;
    if(r.status==200){size_t n;json_t *code;json_array_foreach(json_object_get(j,"badges"),n,code){const char *s=json_string_value(code);if(!s)continue;if(I.count<8)snprintf(I.codes[I.count++],16,"%s",s);I.vip|=!strcmp(s,"VIP");I.staff|=!strcmp(s,"ADM");I.club|=!strcmp(s,"CLUB1");I.gold|=!strcmp(s,"CLUB2");}}
    else snprintf(I.status,sizeof I.status,"%s",TR("Could not load badges. Try Refresh."));
  }else if(I.kind==1){
    const char *name=json_string_value(json_object_get(j,"name"));
    if(r.status==200&&name&&I.token[0]&&net_online()){snprintf(I.name,sizeof I.name,"%s",name);I.restore=true;}
    else{wipe(I.token,sizeof I.token);snprintf(I.status,sizeof I.status,"%s",TR("Could not verify that character code."));}
  }else if(I.kind==3){if(r.status==200){I.picking=false;refresh();}snprintf(I.status,sizeof I.status,r.status==200?"Profile badge saved.":"Could not save badge. Try again.");}
  else snprintf(I.status,sizeof I.status,"%s",r.status==200?TR("Identity updated."):TR("Could not update identity. Refresh and retry."));
  json_decref(j);if(r.body){wipe(r.body,strlen(r.body));free(r.body);}client_ui_changed();
}
void client_identity_action(int cmd,int arg){
  (void)arg;if(!net_online())return;
  if(cmd==REFRESH){refresh();return;}
  if(I.guest!=W.guest_id)return;
  if(cmd==PICK_BADGE&&!I.job){I.picking=!I.picking;I.choice=0;client_ui_changed();return;}
  if(cmd==CHOOSE_BADGE&&!I.job&&arg>=0&&arg<I.count){I.choice=arg;client_ui_changed();return;}
  if(cmd==SAVE_BADGE&&!I.job&&I.count){json_t *j=json_pack("{s:s}","code",I.codes[I.choice]);char *body=json_dumps(j,JSON_COMPACT);json_decref(j);I.job=net_http("POST","/api/badges",body,true);I.kind=3;free(body);return;}
  if(cmd==CANCEL){client_identity_close();client_ui_changed();return;}
  if(cmd==REVEAL){I.reveal_until=I.reveal_until?0:now_ms()+15000;client_ui_changed();return;}
  if(cmd==RESTORE&&!I.job){client_identity_close();client_ui_edit_secret(TR("Restore character: private code"),restore_submit);return;}
  if(cmd==APPLY&&I.restore&&I.token[0]&&!I.job){
    net_use_token(I.token);client_identity_close();client_ui_reset();return;
  }
  if((cmd==VISIBLE||cmd==PRESENCE)&&!I.job){
    json_t *owners=W.owners_json?json_loads(W.owners_json,0,NULL):NULL,*self=json_object_get(owners,"self");
    if(json_is_object(self)){
      bool visible=json_is_true(json_object_get(self,"visible")),presence=json_is_true(json_object_get(self,"presence"));
      if(cmd==VISIBLE)visible=!visible;else if(visible)presence=!presence;else{json_decref(owners);return;}
      char body[80];snprintf(body,sizeof body,"{\"visible\":%s,\"presence\":%s}",visible?"true":"false",presence?"true":"false");
      I.job=net_http("POST","/api/owner-profile",body,true);I.kind=2;
    }json_decref(owners);
  }
}
void client_identity_draw(float x,float y,float w){
  if(!net_online()){client_ui_label(TR("Connect to view or restore your identity."),x+10,y+35,w-20,0x18272a);return;}
  if(I.restore){
    char text[180];snprintf(text,sizeof text,TR("Use %s on this console? Save your current code before switching."),I.name);
    client_ui_paragraph(text,x+10,y+38,w-20,5,0x18272a);
    client_ui_button(x+8,y+180,140,26,TR("Cancel"),CANCEL,0);client_ui_button(x+156,y+180,146,26,TR("Use character"),APPLY,0);return;
  }
  if(I.picking&&I.guest==W.guest_id){
    client_ui_label(TR("Choose your profile badge"),x+10,y+32,w-20,0x18272a);
    for(int k=0;k<I.count;k++){char label[40];snprintf(label,sizeof label,"%s%s",I.choice==k?"> ":"",I.codes[k]);client_ui_button(x+8+(k%2)*148,y+62+(k/2)*28,146,25,label,!I.job?CHOOSE_BADGE:0,k);}
    client_ui_button(x+8,y+180,140,25,TR("Back"),!I.job?PICK_BADGE:0,0);client_ui_button(x+156,y+180,146,25,TR("Show this badge"),!I.job&&I.count?SAVE_BADGE:0,0);client_ui_label(I.status,x+10,y+208,w-20,0x18272a);return;
  }
  char badges[100];snprintf(badges,sizeof badges,"%s%s%s%s",I.vip?"VIP  ":"",I.staff?"Staff  ":"",I.gold?"Club Gold":I.club?"Club":"",!I.vip&&!I.staff&&!I.club&&!I.gold?(I.job?"Loading badges...":"No badges to display."):"");
  const char *keys[]={"badge-VIP","badge-ADM",I.gold?"badge-CLUB2":"badge-CLUB1"};
  bool flags[]={I.vip,I.staff,I.gold||I.club};float bx=x+10;
  for(int k=0;k<3;k++)if(flags[k])for(int i=0;i<ui_sprite_count;i++)if(!strcmp(ui_sprites[i].name,keys[k])){const UiSprite *sp=&ui_sprites[i];float scale=1;gfx_sprite(sp->page,sp->x,sp->y,sp->w,sp->h,bx,y+29,scale,false,0xffffffff);bx+=40;break;}
  client_ui_label(badges,bx,y+31,w-(bx-x)-10,0x18272a);
  client_ui_button(x+8,y+67,I.guest==W.guest_id?140:w-16,24,TR("Refresh badges"),!I.job?REFRESH:0,0);
  if(I.guest==W.guest_id)client_ui_button(x+156,y+67,146,24,TR("Choose badge"),!I.job&&I.count?PICK_BADGE:0,0);
  if(I.guest==W.guest_id){
    if(I.reveal_until&&now_ms()>=I.reveal_until)I.reveal_until=0;
    if(I.reveal_until){
      const char *token=net_token();char first[41];snprintf(first,sizeof first,"%.40s",token);
      client_ui_label(first,x+10,y+98,w-20,0x18272a);client_ui_label(strlen(token)>40?token+40:TR("Keep this code private."),x+10,y+114,w-20,0x18272a);
    }else client_ui_label(TR("Character code: hidden"),x+10,y+101,w-20,0x18272a);
    client_ui_button(x+8,y+139,140,25,I.reveal_until?TR("Hide code"):TR("Show for 15 sec"),REVEAL,0);
    client_ui_button(x+156,y+139,146,25,TR("Restore character"),!I.job?RESTORE:0,0);
    json_t *j=W.owners_json?json_loads(W.owners_json,0,NULL):NULL,*self=json_object_get(j,"self");
    if(json_is_object(self)){bool visible=json_is_true(json_object_get(self,"visible")),presence=json_is_true(json_object_get(self,"presence"));
      client_ui_button(x+8,y+173,140,26,visible?TR("Go anonymous"):TR("Show Founder"),!I.job?VISIBLE:0,0);
      client_ui_button(x+156,y+173,146,26,presence?TR("Presence: on"):TR("Presence: off"),!I.job&&visible?PRESENCE:0,0);
    }json_decref(j);
  }
  client_ui_label(I.status,x+10,y+204,w-20,0x18272a);
}
