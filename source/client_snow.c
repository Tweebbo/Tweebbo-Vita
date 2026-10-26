#include "language.h"
#include "language_js.h"
#include "client_snow.h"
#include "audio.h"
#include "client_ui.h"
#include "show.h"
#include "gfx.h"
#include "net.h"
#include "proto.h"
#include "world.h"
#include "quickjs.h"
#include <jansson.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <malloc.h>

static JSRuntime *runtime;
static JSContext *context;
static JSValue api;
static bool active, textures, create_page, menu, confirm_leave;
static float ox, oy;
static u64 deadline, snapshot_at, move_at;
static json_t *snapshot;
static int page, arena=1, teams=2, players=3, minutes=3, level=1, fill=2;
static char error[160];
typedef union { size_t size; max_align_t align; } Allocation;
static void *qmalloc(JSMallocState *s,size_t n) {
  if(!n||n>SIZE_MAX-sizeof(Allocation)||s->malloc_size+n+sizeof(Allocation)>s->malloc_limit){dbg_log("Snow allocation refused: %lu bytes; used %lu",(unsigned long)n,(unsigned long)s->malloc_size);return NULL;}
  Allocation *p=malloc(n+sizeof *p);if(!p){dbg_log("Snow heap allocation failed: %lu",(unsigned long)n);return NULL;}p->size=n;
  s->malloc_count++;s->malloc_size+=n+sizeof *p;return p+1;
}
static void qfree(JSMallocState *s,void *ptr) {
  if(!ptr)return;
  Allocation *p=(Allocation*)ptr-1;s->malloc_size-=p->size+sizeof *p;s->malloc_count--;free(p);
}
static void *qrealloc(JSMallocState *s,void *ptr,size_t n) {
  if(!ptr)return qmalloc(s,n);
  if(!n){qfree(s,ptr);return NULL;}
  Allocation *p=(Allocation*)ptr-1;size_t old=p->size;
  if(n>SIZE_MAX-sizeof *p||s->malloc_size-old+n>s->malloc_limit){dbg_log("Snow reallocation refused: %lu bytes; old %lu; used %lu",(unsigned long)n,(unsigned long)old,(unsigned long)s->malloc_size);return NULL;}
  p=realloc(p,n+sizeof *p);if(!p){dbg_log("Snow heap reallocation failed: %lu",(unsigned long)n);return NULL;}p->size=n;s->malloc_size=s->malloc_size-old+n;return p+1;
}
static size_t qusable(const void *ptr){return ptr?((const Allocation*)ptr-1)->size:0;}
static int interrupted(JSRuntime *rt,void *opaque){(void)rt;(void)opaque;return now_ms()>deadline;}
static double arg(JSContext *ctx,JSValueConst value){double n=0;JS_ToFloat64(ctx,&n,value);return isfinite(n)?n:0;}
static u32 color(double rgb,double alpha){unsigned c=(unsigned)rgb;return ((u32)fmax(0,fmin(255,alpha*255))<<24)|((c&255)<<16)|(c&0xff00)|(c>>16);}
static JSValue sprite(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<11)return JS_UNDEFINED;double a[11];for(int i=0;i<11;i++)a[i]=arg(ctx,argv[i]);
  gfx_snow_sprite(a[0],a[1]+ox,a[2]+oy,a[3],a[4],color(a[5],a[6]),a[7]+ox,a[8]+oy,a[9]+ox,a[10]+oy);return JS_UNDEFINED;
}
static JSValue rectangle(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<7)return JS_UNDEFINED;double a[7];for(int i=0;i<7;i++)a[i]=arg(ctx,argv[i]);
  fill_round(a[0]+ox,a[1]+oy,a[2],a[3],a[4],color(a[5],a[6]));return JS_UNDEFINED;
}
static JSValue text(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<6)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  text_draw(s,arg(ctx,argv[1])+ox,arg(ctx,argv[2])+oy,arg(ctx,argv[3]),color(arg(ctx,argv[4]),1),arg(ctx,argv[5]));JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static JSValue measure(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<2)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  double w=text_width(s,arg(ctx,argv[1]));JS_FreeCString(ctx,s);return JS_NewFloat64(ctx,w);
}
static JSValue send_packet(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<1)return JS_UNDEFINED;size_t n=0;u8 *b=JS_GetArrayBuffer(ctx,&n,argv[0]);
  if(b&&n>1&&n<=1024&&b[0]==C_SNOW)net_send(b,n);
  return JS_UNDEFINED;
}
static JSValue sound(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
  (void)self;if(argc<2)return JS_UNDEFINED;const char *s=JS_ToCString(ctx,argv[0]);if(!s)return JS_EXCEPTION;
  audio_snow_play(s,arg(ctx,argv[1])!=0);JS_FreeCString(ctx,s);return JS_UNDEFINED;
}
static JSValue stop_walk(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)ctx;(void)self;(void)argc;(void)argv;audio_stop(0);return JS_UNDEFINED;}
static void exception(void){
  JSValue e=JS_GetException(context);const char *s=JS_ToCString(context,e);
  dbg_log("Snow Storm: %s",s?s:"runtime error");
  JSValue stack=JS_GetPropertyStr(context,e,"stack");const char *trace=JS_ToCString(context,stack);
  if(trace)dbg_log("Snow Storm stack: %s",trace);
  JS_FreeCString(context,trace);JS_FreeValue(context,stack);
  snprintf(error,sizeof error,"%s",s&&strstr(s,"versions differ")?"Update Tweebbo to play Snow Storm with this hotel.":"Snow Storm stopped safely. Reopen it to reconnect to your match.");
  JS_FreeCString(context,s);JS_FreeValue(context,e);
}
static JSValue call(const char *name,int n,JSValue *args){
  if(!context)return JS_UNDEFINED;
  deadline=now_ms()+250;
  JSValue fn=JS_GetPropertyStr(context,api,name),result=JS_Call(context,fn,api,n,args);
  JS_FreeValue(context,fn);for(int i=0;i<n;i++)JS_FreeValue(context,args[i]);
  if(JS_IsException(result))exception();
  return result;
}
static void invoke(const char *name){JSValue r=call(name,0,NULL);if(context)JS_FreeValue(context,r);}
static void number_call(const char *name,double n){JSValue a=JS_NewFloat64(context,n),r=call(name,1,&a);JS_FreeValue(context,r);}
static bool init(void){
  static const JSMallocFunctions allocator={qmalloc,qfree,qrealloc,qusable};
  runtime=JS_NewRuntime2(&allocator,NULL);if(!runtime)return false;
  JS_SetMemoryLimit(runtime,12u*1024*1024);JS_SetMaxStackSize(runtime,128u*1024);JS_SetInterruptHandler(runtime,interrupted,NULL);
  JS_SetGCThreshold(runtime,512u*1024);
  deadline=now_ms()+5000;context=JS_NewContext(runtime);if(!context)return false;
  JSValue global=JS_GetGlobalObject(context);
  JS_SetPropertyStr(context,global,"nativeTranslate",JS_NewCFunction(context,tw_js_translate,"nativeTranslate",1));
  struct {const char *name;JSCFunction *fn;int argc;} fns[]={{"nativeSprite",sprite,11},{"nativeRect",rectangle,7},{"nativeText",text,6},{"nativeMeasure",measure,2},{"nativeSend",send_packet,1},{"nativeSound",sound,2},{"nativeStopWalk",stop_walk,0}};
  for(unsigned i=0;i<COUNT(fns);i++)JS_SetPropertyStr(context,global,fns[i].name,JS_NewCFunction(context,fns[i].fn,fns[i].name,fns[i].argc));
  { /* Guards against toolchain miscompiles of QuickJS (see Makefile): string iteration must end. */
    static const char check[]="[...'ab'].join('')+Array.from('cd').length";
    JSValue v=JS_Eval(context,check,sizeof check-1,"self-test",JS_EVAL_TYPE_GLOBAL);const char *t=JS_IsException(v)?NULL:JS_ToCString(context,v);
    bool ok=t&&!strcmp(t,"ab2");JS_FreeCString(context,t);if(JS_IsException(v))JS_FreeValue(context,JS_GetException(context));JS_FreeValue(context,v);
    if(!ok){dbg_log("Snow Storm: script engine self-test failed");JS_FreeValue(context,global);return false;}
  }
#if defined(__vita__)
  FILE *f=fopen("app0:snow/game.js","rb");
#else
  FILE *f=fopen("romfs:/snow/game.js","rb");
#endif
  if(!f){JS_FreeValue(context,global);return false;}
  fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);
  char *script=len>0&&len<2*1024*1024?malloc(len+1):NULL;
  if(!script){fclose(f);JS_FreeValue(context,global);return false;}
  size_t read=fread(script,1,len,f);fclose(f);script[read]=0;
  JSValue result=JS_Eval(context,script,read,"snow/game.js",JS_EVAL_TYPE_GLOBAL);free(script);
  bool ok=!JS_IsException(result);if(!ok)exception();JS_FreeValue(context,result);
  api=JS_GetPropertyStr(context,global,"Snow");JS_FreeValue(context,global);return ok&&JS_IsObject(api);
}
static int num(json_t *j,const char *key){return (int)json_integer_value(json_object_get(j,key));}
static const char *str(json_t *j,const char *key){const char *s=json_string_value(json_object_get(j,key));return s?s:"";}
static bool is_mode(const char *s){return !strcmp(str(snapshot,"mode"),s);}
bool client_snow_playing(void){return active && !error[0] && is_mode("game") && !menu;}
void client_snow_reset(void){
  audio_snow_free();
  if(context){JS_FreeValue(context,api);JS_FreeContext(context);context=NULL;}
  if(runtime){JS_FreeRuntime(runtime);runtime=NULL;}
  json_decref(snapshot);snapshot=NULL;active=false;snapshot_at=0;move_at=0;
  /* GPU resources are released by update, between drawing passes. */
}
void client_snow_open(void){
  if(active)return;
  audio_snow_load();active=true;error[0]=0;create_page=false;menu=false;confirm_leave=false;page=0;
}
void client_snow_close(void){
  if(context && !error[0] && (is_mode("game") || is_mode("waiting"))){menu=true;confirm_leave=true;client_ui_changed();return;}
  client_snow_reset();client_ui_close();
}
void client_snow_receive(const u8 *data,int len){
  if(!active||!context||error[0]||len<=0||len>65536)return;
  JSValue a[]={JS_NewArrayBufferCopy(context,data,len),JS_NewInt64(context,W.me),JS_NewFloat64(context,now_ms())};
  JSValue r=call("receive",3,a);JS_FreeValue(context,r);snapshot_at=0;
}
void client_snow_update(void){
  if(!active){if(textures){gfx_snow_free();textures=false;}return;}
  if(error[0])return;
  if(!context){
    if(!init()){if(!error[0])snprintf(error,sizeof error,"%s",TR("Snow Storm could not start. Close it and try again."));return;}
    number_call("open",W.me);
  }
  if(!textures){if(!gfx_snow_load()){snprintf(error,sizeof error,"%s",TR("Could not load Snow Storm graphics. Close and retry."));return;}textures=true;}
  u64 now=now_ms();number_call("tick",now);
  if(now-snapshot_at>=60){
    JSValue a=JS_NewFloat64(context,now),r=call("status",1,&a);const char *s=JS_ToCString(context,r);
    json_t *next=s?json_loads(s,0,NULL):NULL;JS_FreeCString(context,s);JS_FreeValue(context,r);
    if(next){bool changed=strcmp(str(next,"mode"),str(snapshot,"mode"))!=0;json_decref(snapshot);snapshot=next;if(changed){page=0;menu=false;confirm_leave=false;client_ui_changed();}}
    snapshot_at=now;
  }
}
bool client_snow_scene(u64 now){
  if(!active)return false;
  fill_rect(0,0,SCREEN_W,SCREEN_H,color(0xdfeaf2,1));
  ox=(SCREEN_W-400)/2;oy=(SCREEN_H-272)/2;
  if(context && textures && !error[0])number_call("draw",now);
  return true;
}
bool client_snow_key(int action){
  if(!client_snow_playing())return false;
  if(action==SHOW_B){menu=true;client_ui_changed();}
  if(action==SHOW_A)invoke("throw");
  if(action==SHOW_X)invoke("make");
  if(action==SHOW_Y)invoke("trajectory");
  if(action==SHOW_L)invoke("aim");
  if(action==SHOW_R)invoke("zoom");
  return true;
}
bool client_snow_move(int dx,int dy){
  if(!client_snow_playing())return false;
  u64 now=now_ms();if(now-move_at<180)return true;
  int x=dx+dy,y=dy-dx;
  JSValue a[]={JS_NewInt32(context,x>0?2:x<0?-2:0),JS_NewInt32(context,y>0?2:y<0?-2:0)},r=call("move",2,a);
  JS_FreeValue(context,r);move_at=now;return true;
}
enum { REFRESH=7000, MORE, CREATE, PRACTICE, JOIN, START, SETTING, SUBMIT, RESUME, LEAVE, CANCEL, CHAT, TARGET, MAKE, THROW, ARC, AIM, ZOOM, LOBBY, MENU, ROSTER_MORE };
static bool chat_submit(const char *message){
  if(!active||!context||error[0])return false;
  JSValue a=JS_NewString(context,message),r=call("chat",1,&a);JS_FreeValue(context,r);return true;
}
void client_snow_action(int cmd,int value){
  if(!active)return;
  if(cmd==LEAVE){if(context&&!error[0])invoke("leave");client_snow_reset();client_ui_close();return;}
  if(cmd==CANCEL){confirm_leave=false;menu=false;client_ui_changed();return;}
  if(!context||error[0])return;
  switch(cmd){
  case REFRESH:invoke("list");break;
  case MORE:case ROSTER_MORE:page++;break;
  case CREATE:create_page=true;break;
  case RESUME:menu=false;break;
  case MENU:menu=true;break;
  case LOBBY:invoke("leave");break;
  case CHAT:client_ui_edit(TR("Snow Storm chat"), "",chat_submit);break;
  case MAKE:invoke("make");break;
  case THROW:invoke("throw");break;
  case ARC:invoke("trajectory");break;
  case AIM:invoke("aim");break;
  case ZOOM:invoke("zoom");break;
  case TARGET:number_call("pick",value);break;
  case JOIN:if(net_online())number_call("join",value);break;
  case START:if(net_online())number_call("start",value);break;
  case PRACTICE:{
    Ent *e=world_ent(W.me);JSValue a[]={JS_NewInt64(context,W.me),JS_NewFloat64(context,e?e->look:0),JS_NewString(context,e?e->name:"You"),JS_NewFloat64(context,now_ms())};
    JSValue r=call("practice",4,a);JS_FreeValue(context,r);break;
  }
  case SETTING:{
    int row=abs(value)-1,change=value>0?1:-1;
    int *values[]={&arena,&teams,&players,&minutes,&level,&fill};
    if(row<0||row>=6)break;
    if(row==0){static const int a[]={1,2,3,7};int n=0;while(n<3&&a[n]!=arena)n++;arena=a[(n+change+4)%4];}
    else if(row==3){static const int a[]={2,3,5};int n=0;while(n<2&&a[n]!=minutes)n++;minutes=a[(n+change+3)%3];}
    else{int lo=row>=4?0:row==2&&teams==1?2:1,hi=row==1?4:row==2?(teams==1?8:4):row==4?2:10;*values[row]=lo+(*values[row]-lo+change+hi-lo+1)%(hi-lo+1);}
    if(teams==1&&players<2)players=2;
    if(teams>1&&players>4)players=4;
    break;
  }
  case SUBMIT:if(net_online()){
    JSValue a[]={JS_NewInt32(context,arena),JS_NewInt32(context,teams),JS_NewInt32(context,players),JS_NewInt32(context,minutes),JS_NewInt32(context,level),JS_NewInt32(context,fill)};
    JSValue r=call("create",6,a);JS_FreeValue(context,r);create_page=false;
  }break;
  }
  snapshot_at=0;client_ui_changed();
}
void client_snow_hud(void){
  json_t *s=json_object_get(snapshot,"state");char line[160];int left=num(s,"left");
  snprintf(line,sizeof line,TR("%d:%02d  Health %d  Snowballs %d  |  L: target  R: zoom"),left/60,left%60,num(s,"hp"),num(s,"balls"));
  text_fit(line,11,SCREEN_W-12);
  fill_rect(0,0,SCREEN_W,20,color(0x26373a,1));text_draw(line,SCREEN_W/2,3,11,color(0xffffff,1),1);
  float x=(SCREEN_W-460)/2,y=SCREEN_H-28;
  static const char *arcs[]={"Quick throw","Short arc","Long arc"};
  client_ui_button(x,y,100,24,TR("Snowball"),MAKE,0);
  client_ui_button(x+104,y,100,24,TR("Throw"),THROW,0);
  client_ui_button(x+208,y,120,24,TR(arcs[num(snapshot,"trajectory")%3]),ARC,0);
  client_ui_button(x+332,y,128,24,TR("Menu"),MENU,0);
#if TWEEBBO_DEVICE == 3
  const char *keys[]={"Square","Cross","Triangle","Circle"};
#else
  const char *keys[]={"X","A","Y","B"};
#endif
  const int offsets[]={4,108,212,336};
  for(int i=0;i<4;i++)client_ui_key_hint(keys[i],"",x+offsets[i],y-18,0xffffff,0xffffff);
}
void client_snow_draw(float x,float y,float w){
  char line[160];
  if(error[0]){client_ui_paragraph(error,x+10,y+35,w-20,5,0x18272a);return;}
  if(!snapshot){client_ui_label(TR("Opening Snow Storm..."),x+10,y+35,w-20,0x18272a);return;}
  if(confirm_leave){
    client_ui_paragraph(TR("Leave Snow Storm? A computer player will take your place in a running match."),x+10,y+40,w-20,4,0x18272a);
    client_ui_button(x+8,y+180,140,26,TR("Keep playing"),CANCEL,0);client_ui_button(x+156,y+180,146,26,TR("Leave"),LEAVE,0);return;
  }
  if(create_page){
    int values[]={arena,teams,players,minutes,level,fill};const char *labels[]={"Arena","Teams (1 = solo)","Players per team","Minutes","Bot level","Fill after (min)"};
    for(int i=0;i<6;i++){
      snprintf(line,sizeof line,"%s: %d",TR(labels[i]),values[i]);client_ui_label(line,x+10,y+32+i*24,w-90,0x18272a);
      client_ui_button(x+w-76,y+28+i*24,30,22,"-",SETTING,-(i+1));client_ui_button(x+w-40,y+28+i*24,30,22,"+",SETTING,i+1);
    }
    client_ui_button(x+8,y+182,w-16,26,TR("Create game"),net_online()?SUBMIT:0,0);return;
  }
  if(is_mode("list")){
    json_t *list=json_object_get(snapshot,"games");int n=json_array_size(list);if(page*4>=n)page=0;
    for(int i=0;i<4;i++){
      json_t *g=json_array_get(list,page*4+i);if(!g)break;
      snprintf(line,sizeof line,"%s | %u/%d",str(g,"name"),(unsigned)json_array_size(json_object_get(g,"roster")),num(g,"capacity"));
      client_ui_label(line,x+10,y+34+i*28,w-90,0x18272a);
      bool running=json_is_true(json_object_get(g,"running"));client_ui_button(x+w-78,y+29+i*28,70,24,running?TR("Playing"):TR("Join"),!running&&net_online()?JOIN:0,num(g,"id"));
    }
    if(!n)client_ui_paragraph(TR("No games in this room. Create one, or practise against computer players."),x+10,y+40,w-20,4,0x18272a);
    client_ui_button(x+8,y+148,90,24,TR("Refresh"),net_online()?REFRESH:0,0);
    client_ui_button(x+108,y+148,90,24,TR("More"),n>4?MORE:0,0);
    client_ui_button(x+208,y+148,94,24,TR("Create"),net_online()?CREATE:0,0);
    client_ui_button(x+8,y+182,w-16,26,TR("Practice (no credits)"),PRACTICE,0);return;
  }
  if(is_mode("waiting")){
    json_t *g=json_object_get(snapshot,"waiting"),*list=json_object_get(g,"roster");
    snprintf(line,sizeof line,TR("Arena %d | %d teams | %d min"),num(g,"arena"),num(g,"teams"),num(g,"minutes"));client_ui_label(line,x+10,y+32,w-20,0x18272a);
    int count=json_array_size(list);if(page*5>=count)page=0;
    for(int i=0;i<5&&page*5+i<count;i++){json_t *p=json_array_get(list,page*5+i);snprintf(line,sizeof line,TR("Team %d: %s"),num(p,"team")+1,str(p,"name"));client_ui_label(line,x+10,y+55+i*18,w-20,0x18272a);}
    if(count>5)client_ui_button(x+8,y+153,w-16,24,TR("More players"),ROSTER_MORE,0);
    client_ui_button(x+8,y+182,90,26,TR("Chat"),CHAT,0);client_ui_button(x+108,y+182,w-116,26,TR("Start with bots"),net_online()&&(u32)num(g,"host")==W.me?START:0,num(g,"id"));return;
  }
  json_t *avatars=json_object_get(json_object_get(snapshot,"state"),"avatars");
  if(is_mode("results")){
    client_ui_label(TR("Match finished"),x+10,y+32,w-20,0x18272a);
    int count=json_array_size(avatars);if(page*6>=count)page=0;
    for(int i=0;i<6&&page*6+i<count;i++){json_t *p=json_array_get(avatars,page*6+i);snprintf(line,sizeof line,TR("%s: %d points"),str(p,"name"),num(p,"score"));client_ui_label(line,x+10,y+54+i*17,w-20,0x18272a);}
    if(count>6)client_ui_button(x+8,y+157,w-16,22,TR("More scores"),ROSTER_MORE,0);
    client_ui_button(x+8,y+182,w-16,26,TR("Back to Snow Storm lobby"),LOBBY,0);return;
  }
  client_ui_label(TR("Choose a rival; choose again for auto-aim."),x+10,y+31,w-20,0x18272a);
  int enemies=0;for(size_t i=0;i<json_array_size(avatars);i++)if(json_is_true(json_object_get(json_array_get(avatars,i),"enemy")))enemies++;
  if(page*8>=enemies)page=0;
  int shown=0,skip=page*8;for(size_t i=0;i<json_array_size(avatars)&&shown<8;i++){
    json_t *p=json_array_get(avatars,i);if(!json_is_true(json_object_get(p,"enemy")))continue;
    if(skip){skip--;continue;}
    snprintf(line,sizeof line,"%s%s (%d)",json_is_true(json_object_get(p,"target"))?"> ":"",str(p,"name"),num(p,"hp"));
    client_ui_button(x+8+(shown%2)*148,y+54+(shown/2)*27,144,24,line,TARGET,i);shown++;
  }
  if(enemies>8)client_ui_button(x+8,y+162,w-16,20,TR("More rivals"),ROSTER_MORE,0);
  else client_ui_label(json_is_true(json_object_get(snapshot,"syncing"))?TR("Waiting for server turns..."):TR("Pad / tap: move. Play resumes below."),x+10,y+164,w-20,0x18272a);
  client_ui_button(x+8,y+186,140,26,TR("Resume"),RESUME,0);client_ui_button(x+156,y+186,146,26,TR("Chat"),CHAT,0);
}

bool client_snow_tap(float x,float y){
  if(!client_snow_playing())return false;
  x-=ox;y-=oy;
  if(x>=0&&x<400&&y>=0&&y<240){
    JSValue a[]={JS_NewFloat64(context,x),JS_NewFloat64(context,y),JS_NewInt32(context,0)},r=call("touch",3,a);
    JS_FreeValue(context,r);
  }
  return true;
}
