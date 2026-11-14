#include "language.h"
#include "client_social.h"
#include "client_ui.h"
#include "client_music.h"
#include "net.h"
#include "world.h"
#include "show.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "gfx.h"
#include "avatar.h"

enum { FRIENDS, INCOMING, OUTGOING, BLOCKED, RESULTS, CONTACT, CONVERSATION, CONFIRM, MY_INFO };
enum { TAB=2000, SELECT, BACK, PAGE, SEARCH, REQUEST, ACCEPT, REJECT, CANCEL, REMOVE, BLOCK, UNBLOCK, CHAT, WRITE, VISIT, CONFIRM_ACTION, REFRESH, TEXT_PAGE, EDIT_MISSION, MY_PROFILE };
enum { LOAD=1, FIND, MESSAGES, ACTION, READ };
static struct {
  int view, from, page, message_page, job, kind, confirm;
  u32 peer, job_peer, last_read, draft_peer;
  u64 next_poll;
  bool enabled, sent;
  size_t text_offset, next_text;
  json_t *contacts, *results, *messages;
  char status[160], query[80], draft[256];
  float scroll;  /* chat thread: pixels above the newest message */
  long long tz;  /* console clock minus server UTC, seconds */
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
static int unread_messages(void) {
  int n=0;size_t i;json_t *v;
  json_array_foreach(list(FRIENDS),i,v)n+=(int)json_integer_value(json_object_get(v,"unread"));
  return n;
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
static bool mission_done(const char *s) {
  return act("mission",s);
}
static bool message_done(const char *s) {
  if(!*s || !find(FRIENDS,F.peer))return false;
  snprintf(F.draft,sizeof F.draft,"%s",s);
  F.draft_peer=F.peer;
  F.sent=true;
  return act("message",s);
}
static void message_cancelled(const char *s) {snprintf(F.draft,sizeof F.draft,"%s",s);F.draft_peer=F.peer;}
void client_social_action(int command,int arg) {
  if(command==MY_PROFILE){view(MY_INFO);F.peer=0;return;}
  if(command==EDIT_MISSION){if(!F.job)client_ui_edit(TR("Mission (up to 50 characters)"),string(json_object_get(F.contacts,"self"),"mission"),mission_done);return;}
  if(command==TAB) {view(arg);F.peer=0;return;}
  if(command==PAGE) {if(F.view==CONVERSATION){F.scroll+=arg*-60.0f;if(F.scroll<0)F.scroll=0;}else F.page+=arg;client_ui_changed();return;}
  if(command==TEXT_PAGE) {F.text_offset=arg?F.next_text:0;client_ui_changed();return;}
  if(command==REFRESH) {F.next_poll=0;return;}
  if(command==BACK) {view(F.view==CONFIRM?CONTACT:F.view==CONVERSATION||(F.view==CONTACT&&find(FRIENDS,F.peer))?FRIENDS:F.from);if(F.view==FRIENDS)F.peer=0;return;}
  if(command==SELECT) {if(F.draft_peer!=(u32)arg)F.draft[0]=0;F.peer=(u32)arg;F.from=F.view;F.last_read=0;
    // A friend opens the chat directly; ⋯ in the chat shows the other actions.
    if(F.view==CONVERSATION){F.from=FRIENDS;view(CONTACT);return;}
    if(F.view==FRIENDS&&find(FRIENDS,F.peer)){view(CONVERSATION);F.scroll=0;json_decref(F.messages);F.messages=NULL;F.next_poll=0;return;}
    view(CONTACT);return;}
  if(command==CHAT) {if(find(FRIENDS,F.peer)){view(CONVERSATION);F.scroll=0;F.message_page=0;F.text_offset=0;json_decref(F.messages);F.messages=NULL;F.next_poll=0;}return;}
  if(command==VISIT) {json_t *c=find(FRIENDS,F.peer);const char *room=string(c,"room");if(*room)net_goto(room,"");return;}
  if(F.job)return;
  if(command==SEARCH) {client_ui_edit(TR("Find a player"),F.query,search_done);return;}
  if(command==WRITE) {client_ui_edit_draft(TR("Private message"),F.draft,message_done,message_cancelled);return;}
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
      if(F.kind==LOAD && json_is_object(j)) {long long server=(long long)json_integer_value(json_object_get(j,"now"));if(server>0)F.tz=llround((double)((long long)time(NULL)-server)/900.0)*900;int before=unread_messages();bool first=!F.contacts;json_decref(F.contacts);F.contacts=json_incref(j);if(!first && unread_messages()>before)client_music_ui_sound("message_new");}
      if(F.kind==FIND && json_is_array(j)) {json_decref(F.results);F.results=json_incref(j);status(json_array_size(j)?"":"No matching players.");}
      if(F.kind==MESSAGES && F.job_peer==F.peer && json_is_array(j)) {
        u32 selected=id(json_array_get(F.messages,json_array_size(F.messages)?json_array_size(F.messages)-1-F.message_page:0));
        (void)selected;json_decref(F.messages);F.messages=json_incref(j);
      }
      if(F.kind==ACTION) {if(F.sent)client_music_ui_sound("message_sent");F.sent=false;F.next_poll=0;F.draft[0]=0;status("Updated.");if(F.view==CONFIRM)view(FRIENDS);}
    } else {F.sent=false;const char *err=string(j,"error");status(*err?err:r.err[0]?r.err:"Could not load Friends. Try Refresh.");show_toast(F.status,now_ms());}
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

#define CH_YEL 0xffcb00
#define CH_INK 0x1d1400
#define CH_PAPER 0xfffdf3
#define CH_LINE 0xf1e3a6
#define CH_DIM 0x7a6a3a
static u32 rgb(u32 c){return client_ui_rgb(c);}
/** Native fonts have no emoji: drop 4-byte UTF-8 sequences, joiners and variation selectors. */
static void strip_emoji(char *s) {
  char *w = s;
  for (const unsigned char *r = (const unsigned char *)s; *r;) {
    if (*r >= 0xf0 && *r <= 0xf4) { r++; while ((*r & 0xc0) == 0x80) r++; continue; }
    if (r[0] == 0xef && r[1] == 0xb8 && r[2] == 0x8f) { r += 3; continue; }
    if (r[0] == 0xe2 && r[1] == 0x80 && r[2] == 0x8d) { r += 3; continue; }
    *w++ = (char)*r++;
  }
  while (w > s && w[-1] == ' ') w--;
  *w = 0;
}
/** A message or preview without emoji, in a rotating buffer (two can be in use at once). */
static const char *plain(const char *s){static char buf[2][2100];static int k;k^=1;snprintf(buf[k],sizeof buf[k],"%s",s);strip_emoji(buf[k]);return buf[k];}
/** Word-wrapped text: returns the lines it takes at `width` (and draws it when `draw`). */
static int wrap(const char *s,float width,float size,bool draw,float x,float y,u32 ink,float *widest) {
  char line[256];int lines=0;const char *p=s;if(widest)*widest=0;
  while(*p && lines<40) {
    int n=0,space=-1;
    while(p[n] && p[n]!='\n') {int next=n+1;while(p[next] && ((unsigned char)p[next]&0xc0)==0x80)next++;if(next>=(int)sizeof line)break;memcpy(line,p,(size_t)next);line[next]=0;if(text_width(line,size)>width)break;if(p[n]==' ')space=n;n=next;}
    int take=n;if(p[n] && p[n]!='\n' && space>0)take=space;
    if(!take && *p!='\n'){take=1;while(p[take] && ((unsigned char)p[take]&0xc0)==0x80)take++;}
    memcpy(line,p,(size_t)take);line[take]=0;
    if(widest){float w=text_width(line,size);if(w>*widest)*widest=w;}
    if(draw)text_draw(line,x,y+lines*(size+3),size,rgb(ink),0);
    lines++;p+=take;while(*p==' ')p++;if(*p=='\n')p++;
  }
  return lines?lines:1;
}
static void local_time(long long utc,struct tm *out){time_t t=(time_t)(utc+F.tz);struct tm *g=gmtime(&t);if(g)*out=*g;else memset(out,0,sizeof *out);}
static int day_key(long long utc){struct tm tm;local_time(utc,&tm);return (tm.tm_year+1900)*1000+tm.tm_yday;}
static void when(long long utc,bool time_only,char *out,size_t n){
  struct tm tm;local_time(utc,&tm);long long now=(long long)time(NULL)-F.tz;int d=day_key(utc);
  if(time_only||d==day_key(now)){strftime(out,n,"%H:%M",&tm);return;}
  if(d==day_key(now-86400)){snprintf(out,n,"%s",TR("Yesterday"));return;}
  snprintf(out,n,"%d/%d",tm.tm_mday,tm.tm_mon+1);
}
static void face(u64 look,float cx,float cy,float r,bool ring,int online) {
  if(ring){fill_circle(cx,cy,r+1,rgb(CH_INK));fill_circle(cx,cy,r,rgb(0xffe680));}
  float k=r/22.0f;avatar_draw_head(look,2,cx-ANCHOR_X*k,cy-36*k-1,k);
  if(online>=0){fill_circle(cx+r*0.72f,cy+r*0.72f,4,rgb(0xffffff));fill_circle(cx+r*0.72f,cy+r*0.72f,3,rgb(online?0x58c322:0x9e9e9e));}
}
/** Chat list row: face with presence, name, time, newest line and unread count. */
static void chat_row(json_t *c,float x,float y,float w,float h) {
  bool focus=client_ui_hot(x,y,w,h,SELECT,(int)id(c));
  int unread=(int)json_integer_value(json_object_get(c,"unread"));
  fill_rect(x,y,w,h,rgb(focus?0xffe680:unread?0xfff6cc:0xffffff));
  fill_rect(x+4,y+h-1,w-8,1,rgb(CH_LINE));
  bool online=*string(c,"room");
  face((u64)json_integer_value(json_object_get(c,"look")),x+14,y+h/2,10,true,online);
  json_t *last=json_object_get(c,"last");char at[24]="",name[64],preview[200];
  if(json_is_object(last))when((long long)json_integer_value(json_object_get(last,"created")),false,at,sizeof at);
  if(*at)text_draw(at,x+w-6,y+2,10,rgb(unread?CH_INK:CH_DIM),2);
  snprintf(name,sizeof name,"%s",string(c,"name"));text_fit(name,12,w-90);text_draw(name,x+30,y+1,12,rgb(CH_INK),0);
  if(json_is_object(last)){bool mine=(u32)json_integer_value(json_object_get(last,"sender"))==W.guest_id;snprintf(preview,sizeof preview,mine?TR("You: %s"):"%s",plain(string(last,"text")));}
  else snprintf(preview,sizeof preview,"%s",online?TR("Online"):TR("Say hi!"));
  text_fit(preview,10,w-(unread?70:40));text_draw(preview,x+30,y+h-13,10,rgb(unread?CH_INK:CH_DIM),0);
  if(unread){char u[8];snprintf(u,sizeof u,unread>99?"99+":"%d",unread);float uw=fmaxf(16,text_width(u,10)+8);fill_round(x+w-6-uw,y+h-14,uw,12,6,rgb(CH_INK));text_draw(u,x+w-6-uw/2,y+h-14,10,rgb(CH_YEL),1);}
}
/** The open chat: day separators, grouped bubbles (yours yellow on the right), faces and times. */
static void thread(json_t *c,float x,float y,float w,float h) {
  fill_rect(x,y,w,h,rgb(CH_PAPER));
  int n=(int)json_array_size(F.messages);
  u64 look=(u64)json_integer_value(json_object_get(c,"look"));
  if(!n){face(look,x+w/2,y+h/2-12,12,true,-1);char hi[160];snprintf(hi,sizeof hi,TR("No messages yet. Say hi to %s!"),string(c,"name"));text_fit(hi,10,w-12);text_draw(F.job?TR("Loading..."):hi,x+w/2,y+h/2+6,10,rgb(CH_DIM),1);return;}
  // Lay out bottom-up from the newest message; F.scroll lifts the view to older ones.
  const float size=10,lh=size+3,maxw=floorf(w*0.68f);
  float bottom=y+h-4+F.scroll,yy=bottom;int i=n-1;
  while(i>=0) {
    json_t *m=json_array_get(F.messages,i);
    long long at=(long long)json_integer_value(json_object_get(m,"created"));
    bool mine=(u32)json_integer_value(json_object_get(m,"sender"))==W.guest_id;
    json_t *next=i+1<n?json_array_get(F.messages,i+1):NULL;
    bool end=!next||(u32)json_integer_value(json_object_get(next,"sender"))!=(u32)json_integer_value(json_object_get(m,"sender"))||(long long)json_integer_value(json_object_get(next,"created"))-at>300||day_key((long long)json_integer_value(json_object_get(next,"created")))!=day_key(at);
    if(end){char hm[16];when(at,true,hm,sizeof hm);yy-=11;if(yy>y-12&&yy<y+h)text_draw(hm,mine?x+w-8:x+30,yy,10,rgb(CH_DIM),mine?2:0);}
    const char *body=plain(string(m,"text"));float widest;int lines=wrap(body,maxw-12,size,false,0,0,0,&widest);
    float bw=fminf(maxw,widest+12),bh=lines*lh+5;
    yy-=bh;float bx=mine?x+w-8-bw:x+30;
    if(yy+bh>y&&yy<y+h){fill_round(bx-1,yy-1,bw+2,bh+2,6,rgb(CH_INK));fill_round(bx,yy,bw,bh,5,rgb(mine?CH_YEL:0xffffff));wrap(body,maxw-12,size,true,bx+6,yy+2,CH_INK,NULL);}
    if(end&&!mine&&yy+bh>y&&yy+bh<y+h+20)face(look,x+14,yy+bh-10,12,false,-1);
    json_t *prev=i?json_array_get(F.messages,i-1):NULL;
    long long pat=prev?(long long)json_integer_value(json_object_get(prev,"created")):0;
    bool start=!prev||(u32)json_integer_value(json_object_get(prev,"sender"))!=(u32)json_integer_value(json_object_get(m,"sender"))||at-pat>300||day_key(pat)!=day_key(at);
    yy-=start?5:2;
    if(!prev||day_key(pat)!=day_key(at)){
      char label[40];long long now=(long long)time(NULL)-F.tz;
      if(day_key(at)==day_key(now))snprintf(label,sizeof label,"%s",TR("Today"));else if(day_key(at)==day_key(now-86400))snprintf(label,sizeof label,"%s",TR("Yesterday"));else{struct tm tm;local_time(at,&tm);snprintf(label,sizeof label,"%d/%d/%d",tm.tm_mday,tm.tm_mon+1,tm.tm_year+1900);}
      float lw=text_width(label,10)+14;yy-=16;
      if(yy+14>y&&yy<y+h){fill_round(x+(w-lw)/2-1,yy-1,lw+2,15,7,rgb(CH_INK));fill_round(x+(w-lw)/2,yy,lw,13,6,rgb(CH_YEL));text_draw(label,x+w/2,yy,10,rgb(CH_INK),1);}
    }
    i--;
  }
  // The top of the history stops the scroll.
  float top=bottom-yy;if(F.scroll>top-h+8&&top>h)F.scroll=top-h+8;if(top<=h)F.scroll=0;
}
void client_social_draw(float x,float y,float w) {
  char line[256];
  if(!net_online()) {client_ui_paragraph(TR("Connect to the hotel to use Friends and private messages."),x+10,y+40,w-20,3,0x18272a);return;}
  if(F.view==MY_INFO) {
    json_t *me=json_object_get(F.contacts,"self");
    client_ui_label(W.name,x+10,y+34,w-20,0x18272a);
    client_ui_label(TR("Mission"),x+10,y+60,w-20,0x26373a);
    client_ui_paragraph(string(me,"mission"),x+10,y+80,w-20,4,0x18272a);
    client_ui_button(x+8,y+151,w-16,26,TR("Edit mission"),!F.job?EDIT_MISSION:0,0);
    client_ui_button(x+8,y+188,w-16,24,TR("Friends"),TAB,FRIENDS);
    return;
  }
  if(F.view<=RESULTS) {
    static const char *tabs[]={"Friends","Received","Sent","Blocked"};
    for(int i=0;i<4;i++)client_ui_button(x+8+i*74,y+28,70,23,tabs[i],TAB,i);
    client_ui_field(x+8,y+55,142,23,F.query,TR("Find a player"),!F.job?SEARCH:0,0);
    client_ui_button(x+156,y+55,146,23,TR("My info"),MY_PROFILE,0);
    json_t *rows=list(F.view);int n=(int)json_array_size(rows),pages=(n+3)/4;if(pages<1)pages=1;
    if(F.page<0)F.page=0;
    if(F.page>=pages)F.page=pages-1;
    if(F.view==FRIENDS) {
      // Chats first: unread, then the newest conversation.
      json_t *sorted[256];int count=0;size_t k;json_t *v;json_array_foreach(rows,k,v)if(count<256)sorted[count++]=v;
      for(int a=1;a<count;a++)for(int b=a;b>0;b--){json_t *p=sorted[b-1],*q=sorted[b];
        long long pa=(long long)json_integer_value(json_object_get(json_object_get(p,"last"),"created")),qa=(long long)json_integer_value(json_object_get(json_object_get(q,"last"),"created"));
        int pu=json_integer_value(json_object_get(p,"unread"))>0,qu=json_integer_value(json_object_get(q,"unread"))>0;
        if(qu>pu||(qu==pu&&qa>pa)){sorted[b-1]=q;sorted[b]=p;}else break;}
      for(int row=0;row<4 && F.page*4+row<count;row++)chat_row(sorted[F.page*4+row],x+8,y+82+row*26,w-16,25);
      n=-n;
    }
    for(int row=0;row<4 && n>0 && F.page*4+row<n;row++) {
      json_t *c=json_array_get(rows,F.page*4+row);
      int unread=(int)json_integer_value(json_object_get(c,"unread"));
      snprintf(line,sizeof line,"%s%s%s",string(c,"name"),*string(c,"room")?" (online)":"",unread?" *":"");
      client_ui_button(x+8,y+83+row*25,w-16,23,line,SELECT,(int)id(c));
    }
    if(n<0)n=-n;
    else if(!n)client_ui_label(F.job?TR("Loading..."):*F.status?F.status:TR("No players in this list."),x+10,y+93,w-20,0x26373a);
    client_ui_button(x+8,y+188,80,24,TR("Previous"),F.page?PAGE:0,-1);
    snprintf(line,sizeof line,"%d / %d",F.page+1,pages);client_ui_label(line,x+119,y+194,80,0x18272a);
    client_ui_button(x+w-88,y+188,80,24,TR("Next"),F.page+1<pages?PAGE:0,1);
    return;
  }
  json_t *c=find(F.from,F.peer);if(!c)c=find(FRIENDS,F.peer);
  bool enabled=!F.job && F.peer!=W.guest_id;
  if(F.view==CONVERSATION && c) {
    // Thread first; the header and the composer are painted over its edges.
    thread(c,x+6,y+54,w-40,128);
    fill_rect(x+6,y+24,w-12,30,rgb(0xffffff));fill_rect(x+6,y+52,w-12,2,rgb(CH_YEL));
    client_ui_button(x+8,y+27,26,24,"<",BACK,0);
    bool online=*string(c,"room");
    face((u64)json_integer_value(json_object_get(c,"look")),x+52,y+39,11,true,online);
    char name[64];snprintf(name,sizeof name,"%s",string(c,"name"));text_fit(name,12,w-190);
    text_draw(name,x+68,y+25,12,rgb(CH_INK),0);text_draw(online?TR("Online"):TR("Offline"),x+68,y+38,10,rgb(online?0x2f7d0b:CH_DIM),0);
    if(online)client_ui_button(x+w-120,y+27,82,24,TR("Visit room"),VISIT,0);
    client_ui_button(x+w-34,y+27,26,24,"...",SELECT,(int)F.peer);
    client_ui_arrow(x+w-32,y+58,24,58,"^",PAGE,-1);client_ui_arrow(x+w-32,y+120,24,58,"v",PAGE,1);
    fill_rect(x+6,y+182,w-12,30,rgb(0xffffff));fill_rect(x+6,y+182,w-12,2,rgb(CH_YEL));
    char hint[96];snprintf(hint,sizeof hint,TR("Message %s…"),string(c,"name"));
    client_ui_field(x+8,y+187,w-96,24,F.draft_peer==F.peer?F.draft:"",hint,!F.job?WRITE:0,0);
    client_ui_button(x+w-84,y+187,76,24,TR("Send"),!F.job&&find(FRIENDS,F.peer)?WRITE:0,0);
    return;
  }
  client_ui_label(c?string(c,"name"):TR("Player"),x+10,y+32,w-100,0x18272a);
  client_ui_button(x+w-88,y+27,80,24,TR("Back"),BACK,0);
  if(F.view==CONFIRM) {
    client_ui_paragraph(F.confirm==REMOVE?TR("Remove this player from your friends?"):TR("Block this player and their messages?"),x+10,y+70,w-20,3,0x18272a);
    client_ui_button(x+8,y+155,w-16,26,TR("Confirm"),enabled?CONFIRM_ACTION:0,0);
  } else if(find(BLOCKED,F.peer))client_ui_button(x+8,y+65,w-16,26,TR("Unblock player"),enabled?UNBLOCK:0,0);
  else {
    if(find(FRIENDS,F.peer)) {
      client_ui_button(x+8,y+60,142,26,TR("Private chat"),CHAT,0);
      client_ui_button(x+156,y+60,146,26,TR("Visit room"),*string(find(FRIENDS,F.peer),"room")?VISIT:0,0);
      client_ui_button(x+8,y+94,w-16,26,TR("Remove friend"),enabled?REMOVE:0,0);
    } else if(find(INCOMING,F.peer)) {
      client_ui_button(x+8,y+60,142,26,TR("Accept"),enabled?ACCEPT:0,0);client_ui_button(x+156,y+60,146,26,TR("Decline"),enabled?REJECT:0,0);
    } else if(find(OUTGOING,F.peer))client_ui_button(x+8,y+60,w-16,26,TR("Cancel request"),enabled?CANCEL:0,0);
    else client_ui_button(x+8,y+60,w-16,26,TR("Add friend"),enabled?REQUEST:0,0);
    client_ui_button(x+8,y+126,w-16,26,TR("Block player"),enabled?BLOCK:0,0);
    client_ui_paragraph(string(c,"mission"),x+10,y+157,w-20,2,0x26373a);
  }
  client_ui_label(F.status,x+10,y+193,w-20,0x26373a);
}
