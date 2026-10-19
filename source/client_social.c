#include "client_social.h"
#include "client_ui.h"
#include "net.h"
#include "world.h"
#include "show.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FRIENDS, INCOMING, OUTGOING, BLOCKED, RESULTS, CONTACT, CONVERSATION, CONFIRM };
enum { TAB=2000, SELECT, BACK, PAGE, SEARCH, REQUEST, ACCEPT, REJECT, CANCEL, REMOVE, BLOCK, UNBLOCK, CHAT, WRITE, VISIT, CONFIRM_ACTION, REFRESH, TEXT_PAGE };
enum { LOAD=1, FIND, MESSAGES, ACTION, READ };
static struct {
  int view, from, page, message_page, job, kind, confirm;
  u32 peer, job_peer, last_read, draft_peer;
  u64 next_poll;
  bool enabled;
  size_t text_offset, next_text;
  json_t *contacts, *results, *messages;
  char status[160], query[80], draft[256];
} F;
static const char *string(json_t *j,const char *key) {const char *s=json_string_value(json_object_get(j,key));return s?s:"";}
static u32 id(json_t *j) {return (u32)json_integer_value(json_object_get(j,"id"));}
static json_t *list(int view) {
  static const char *keys[]={"friends","incoming","outgoing","blocked"};
  return view==RESULTS?F.results:view>=FRIENDS&&view<=BLOCKED?json_object_get(F.contacts,keys[view]):NULL;
}
static json_t *find(int view,u32 peer) {
  size_t i;json_t *v;json_array_foreach(list(view),i,v)if(id(v)==peer)return v;return NULL;
}
bool client_social_blocked(u32 guest) {return find(BLOCKED,guest)!=NULL;}
static void status(const char *s) {snprintf(F.status,sizeof F.status,"%s",s);}
static void view(int v) {F.view=v;F.page=0;F.status[0]=0;client_ui_changed();}
void client_social_reset(void) {json_decref(F.contacts);json_decref(F.results);json_decref(F.messages);memset(&F,0,sizeof F);}
void client_social_open(void) {F.enabled=true;view(FRIENDS);F.peer=0;F.next_poll=0;}
void client_social_push(void) {F.next_poll=0;}
static bool request(int kind,const char *method,const char *path,const char *body) {
  if(F.job || !net_online() || !*net_token())return false;
  F.job=net_http(method,path,body,true);
  if(!F.job) {status("Could not connect. Try Refresh.");return false;}
  F.kind=kind;F.job_peer=F.peer;return true;
}
static bool act(const char *action,const char *text) {
  json_t *j=json_pack("{s:s,s:I,s:s}","action",action,"peer",(json_int_t)F.peer,"text",text?text:"");
  char *body=json_dumps(j,JSON_COMPACT);json_decref(j);
  bool ok=body && request(ACTION,"POST","/api/social",body);free(body);
  if(ok)status("Waiting for the hotel...");
  return ok;
}
static bool search_done(const char *s) {
  if(!*s)return false;
  char path[800]="/api/social?q=";size_t n=strlen(path);
  for(const unsigned char *p=(const unsigned char *)s;*p && n+4<sizeof path;p++)n+=(size_t)snprintf(path+n,sizeof path-n,"%%%02X",*p);
  if(!request(FIND,"GET",path,NULL))return false;
  snprintf(F.query,sizeof F.query,"%s",s);view(RESULTS);return true;
}
static bool message_done(const char *s) {
  if(!*s || !find(FRIENDS,F.peer))return false;
  snprintf(F.draft,sizeof F.draft,"%s",s);
  F.draft_peer=F.peer;
  return act("message",s);
}
static void message_cancelled(const char *s) {snprintf(F.draft,sizeof F.draft,"%s",s);F.draft_peer=F.peer;}
void client_social_action(int command,int arg) {
  if(command==TAB) {view(arg);F.peer=0;return;}
  if(command==PAGE) {if(F.view==CONVERSATION){F.message_page+=arg;F.text_offset=0;}else F.page+=arg;client_ui_changed();return;}
  if(command==TEXT_PAGE) {F.text_offset=arg?F.next_text:0;client_ui_changed();return;}
  if(command==REFRESH) {F.next_poll=0;return;}
  if(command==BACK) {view(F.view==CONFIRM || F.view==CONVERSATION?CONTACT:F.from);return;}
  if(command==SELECT) {if(F.draft_peer!=(u32)arg)F.draft[0]=0;F.peer=(u32)arg;F.from=F.view;F.last_read=0;view(CONTACT);return;}
  if(command==CHAT) {if(find(FRIENDS,F.peer)){view(CONVERSATION);F.message_page=0;F.text_offset=0;json_decref(F.messages);F.messages=NULL;F.next_poll=0;}return;}
  if(command==VISIT) {json_t *c=find(FRIENDS,F.peer);const char *room=string(c,"room");if(*room)net_goto(room,"");return;}
  if(F.job)return;
  if(command==SEARCH) {client_ui_edit("Find a player",F.query,search_done);return;}
  if(command==WRITE) {client_ui_edit_draft("Private message",F.draft,message_done,message_cancelled);return;}
  if(command==REMOVE || command==BLOCK) {F.confirm=command;view(CONFIRM);return;}
  const char *action=command==REQUEST?"request":command==ACCEPT?"accept":command==REJECT?"reject":command==CANCEL?"cancel":command==UNBLOCK?"unblock":NULL;
  if(command==CONFIRM_ACTION)action=F.confirm==REMOVE?"remove":"block";
  if(action && F.peer && F.peer!=W.guest_id)act(action,"");
}
void client_social_update(bool visible) {
  if(!F.enabled)return;
  HttpResult r;
  if(F.job && net_http_result(F.job,&r)) {
    F.job=0;json_t *j=r.body?json_loads(r.body,0,NULL):NULL;
    if(r.status==200 && j) {
      if(F.kind==LOAD && json_is_object(j)) {json_decref(F.contacts);F.contacts=json_incref(j);}
      if(F.kind==FIND && json_is_array(j)) {json_decref(F.results);F.results=json_incref(j);status(json_array_size(j)?"":"No matching players.");}
      if(F.kind==MESSAGES && F.job_peer==F.peer && json_is_array(j)) {
        u32 selected=id(json_array_get(F.messages,json_array_size(F.messages)?json_array_size(F.messages)-1-F.message_page:0));
        json_decref(F.messages);F.messages=json_incref(j);
        if(F.message_page || F.text_offset) {
          size_t i;json_t *m;json_array_foreach(j,i,m)if(id(m)==selected){F.message_page=(int)(json_array_size(j)-1-i);break;}
        }
      }
      if(F.kind==ACTION) {F.next_poll=0;F.draft[0]=0;status("Updated.");if(F.view==CONFIRM)view(FRIENDS);}
    } else {const char *err=string(j,"error");status(*err?err:r.err[0]?r.err:"Could not load Friends. Try Refresh.");show_toast(F.status,now_ms());}
    json_decref(j);free(r.body);if(visible)client_ui_changed();
    if(F.kind==LOAD && visible && F.view==CONVERSATION && find(FRIENDS,F.peer)) {
      char path[80];snprintf(path,sizeof path,"/api/social?peer=%u",F.peer);request(MESSAGES,"GET",path,NULL);
    }
  }
  if(F.job || !net_online())return;
  if(visible && F.view==CONVERSATION && json_array_size(F.messages)) {
    u32 last=id(json_array_get(F.messages,json_array_size(F.messages)-1));
    if(last>F.last_read) {
      char body[128];snprintf(body,sizeof body,"{\"action\":\"read\",\"peer\":%u,\"through\":%u}",F.peer,last);
      if(request(READ,"POST","/api/social",body))F.last_read=last;
      return;
    }
  }
  if(now_ms()>=F.next_poll) {request(LOAD,"GET","/api/social",NULL);F.next_poll=now_ms()+(visible?3000:30000);}
}
void client_social_draw(float x,float y,float w) {
  char line[256];
  if(!net_online()) {client_ui_paragraph("Connect to the hotel to use Friends and private messages.",x+10,y+40,w-20,3,0x18272a);return;}
  if(F.view<=RESULTS) {
    static const char *tabs[]={"Friends","Received","Sent","Blocked"};
    for(int i=0;i<4;i++)client_ui_button(x+8+i*74,y+28,70,23,tabs[i],TAB,i);
    client_ui_field(x+8,y+55,142,23,F.query,"Find a player",!F.job?SEARCH:0,0);
    client_ui_button(x+156,y+55,146,23,F.job?"Loading...":"Refresh",!F.job?REFRESH:0,0);
    json_t *rows=list(F.view);int n=(int)json_array_size(rows),pages=(n+3)/4;if(pages<1)pages=1;
    if(F.page<0)F.page=0;
    if(F.page>=pages)F.page=pages-1;
    for(int row=0;row<4 && F.page*4+row<n;row++) {
      json_t *c=json_array_get(rows,F.page*4+row);
      int unread=(int)json_integer_value(json_object_get(c,"unread"));
      snprintf(line,sizeof line,"%s%s%s",string(c,"name"),*string(c,"room")?" (online)":"",unread?" *":"");
      client_ui_button(x+8,y+83+row*25,w-16,23,line,SELECT,(int)id(c));
    }
    if(!n)client_ui_label(F.job?"Loading...":*F.status?F.status:"No players in this list.",x+10,y+93,w-20,0x26373a);
    client_ui_button(x+8,y+188,80,24,"Previous",F.page?PAGE:0,-1);
    snprintf(line,sizeof line,"%d / %d",F.page+1,pages);client_ui_label(line,x+119,y+194,80,0x18272a);
    client_ui_button(x+w-88,y+188,80,24,"Next",F.page+1<pages?PAGE:0,1);
    return;
  }
  json_t *c=find(F.from,F.peer);if(!c)c=find(FRIENDS,F.peer);
  client_ui_label(c?string(c,"name"):"Player",x+10,y+32,w-100,0x18272a);
  client_ui_button(x+w-88,y+27,80,24,"Back",BACK,0);
  bool enabled=!F.job && F.peer!=W.guest_id;
  if(F.view==CONVERSATION) {
    int n=(int)json_array_size(F.messages),pages=n?n:1;
    if(F.message_page<0)F.message_page=0;
    if(F.message_page>=pages)F.message_page=pages-1;
    json_t *m=n?json_array_get(F.messages,n-1-F.message_page):NULL;
    client_ui_label(m?(json_integer_value(json_object_get(m,"sender"))==W.guest_id?"You":string(c,"name")):F.job?"Loading...":"No messages yet.",x+10,y+60,w-20,0x26373a);
    const char *body=string(m,"text");size_t length=strlen(body);if(F.text_offset>length)F.text_offset=0;
    F.next_text=F.text_offset+client_ui_paragraph(body+F.text_offset,x+10,y+81,w-20,4,0x18272a);
    if(F.text_offset)client_ui_button(x+8,y+155,130,23,"Message start",TEXT_PAGE,0);
    if(F.next_text<length)client_ui_button(x+150,y+155,152,23,"Read more",TEXT_PAGE,1);
    client_ui_button(x+8,y+188,80,24,"Older",F.message_page+1<pages?PAGE:0,1);
    client_ui_button(x+94,y+188,80,24,"Newer",F.message_page?PAGE:0,-1);
    client_ui_button(x+180,y+188,w-188,24,"Write",!F.job&&find(FRIENDS,F.peer)?WRITE:0,0);
    return;
  }
  if(F.view==CONFIRM) {
    client_ui_paragraph(F.confirm==REMOVE?"Remove this player from your friends?":"Block this player and their messages?",x+10,y+70,w-20,3,0x18272a);
    client_ui_button(x+8,y+155,w-16,26,"Confirm",enabled?CONFIRM_ACTION:0,0);
  } else if(find(BLOCKED,F.peer))client_ui_button(x+8,y+65,w-16,26,"Unblock player",enabled?UNBLOCK:0,0);
  else {
    if(find(FRIENDS,F.peer)) {
      client_ui_button(x+8,y+60,142,26,"Private chat",CHAT,0);
      client_ui_button(x+156,y+60,146,26,"Visit room",*string(find(FRIENDS,F.peer),"room")?VISIT:0,0);
      client_ui_button(x+8,y+94,w-16,26,"Remove friend",enabled?REMOVE:0,0);
    } else if(find(INCOMING,F.peer)) {
      client_ui_button(x+8,y+60,142,26,"Accept",enabled?ACCEPT:0,0);client_ui_button(x+156,y+60,146,26,"Decline",enabled?REJECT:0,0);
    } else if(find(OUTGOING,F.peer))client_ui_button(x+8,y+60,w-16,26,"Cancel request",enabled?CANCEL:0,0);
    else client_ui_button(x+8,y+60,w-16,26,"Add friend",enabled?REQUEST:0,0);
    client_ui_button(x+8,y+134,w-16,26,"Block player",enabled?BLOCK:0,0);
  }
  client_ui_label(F.status,x+10,y+193,w-20,0x26373a);
}
