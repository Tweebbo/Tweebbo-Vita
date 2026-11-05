#include "../source/client_ui.c"
#include "../source/client_shop.c"
#include <assert.h>

World W;
static char activity_request[256],music_request[256];
static int restored;
static u64 clock_ms;
static int purchases, placements, pickups;
static int pet_requests;
static char pet_request[800];
u64 now_ms(void) {return clock_ms;}
bool world_tile(int x,int y) {return x>=0 && y>=0 && x<W.w && y<W.h;}
Item *world_item(u32 id) {for(int i=0;i<MAX_ITEMS;i++)if(W.items[i].used && W.items[i].id==id)return &W.items[i];return NULL;}
Item *world_item_at(int x,int y) {for(int i=0;i<MAX_ITEMS;i++)if(W.items[i].used && W.items[i].x==x && W.items[i].y==y)return &W.items[i];return NULL;}
const char *public_slug(const char *s) {return s;}
bool world_can_decorate(void) {return W.room_owner || W.room_rights;}
bool world_can_arrange(const Item *it) {return it && (world_can_decorate() || it->owner==W.guest_id);}
bool world_can_manage(const Item *it) {return it && (W.room_owner || it->owner==W.guest_id);}
bool ent_moving(const Ent *e,u64 now) {(void)e;(void)now;return false;}
void send_buy(u16 offer_id) {assert(offer_id);purchases++;}
static int finish_purchases;
void send_decorate(int kind,const char *id) {assert(kind==1 || kind==2);assert(id && *id);finish_purchases++;}
void send_place(u32 id,int x,int y,int rot,int z) {assert(id && x>=0 && y>=0 && rot<4 && z>=0);placements++;}
void send_move(u32 id,int x,int y,int rot,int z) {send_place(id,x,y,rot,z);}
void send_pickup(u32 id) {assert(id);pickups++;}
void send_rotate(u32 id) {assert(id);}
void send_redeem(u32 id) {assert(id);}
void send_game(int op,u32 a,int b,int c,int d) {(void)op;(void)a;(void)b;(void)c;(void)d;}
void send_pet(const char *json) {pet_requests++;snprintf(pet_request,sizeof pet_request,"%s",json);}
static char trade_request[512];
static int trade_requests;
void send_trade(const char *json) {trade_requests++;snprintf(trade_request,sizeof trade_request,"%s",json);}
static u32 used_item,drink_item;
static int used_op,drink_id,gestured;
static int walked_x=-1,walked_y=-1;
void send_walk(int x,int y){walked_x=x;walked_y=y;}
void send_use_op(u32 item,u8 op){used_item=item;used_op=op;}
void send_use(u32 item){used_item=item;}
static int casino_sends;
void send_casino(int op,int a,int b,int c){(void)op;(void)a;(void)b;(void)c;casino_sends++;}
void send_drink(u32 item,int drink){drink_item=item;drink_id=drink;}
void send_gesture(int kind){gestured=kind;}
void show_toast(const char *text,u64 now) {(void)text;(void)now;}
void gfx_furni_preload(u16 def) {(void)def;}
void gfx_furni(u16 def,const FurniPart *p,float x,float y,float k,u32 color) {(void)def;(void)p;(void)x;(void)y;(void)k;(void)color;}
void gfx_pet_preload(int palette) {assert(palette>=0 && palette<pet_palette_count);}
void gfx_pet(int palette,const PetPart *p,float x,float y,float k,bool flip,u32 color,bool solid) {(void)p;(void)x;(void)y;(void)k;(void)flip;(void)color;(void)solid;assert(palette>=0 && palette<pet_palette_count);}
static NetState state=NET_ONLINE;
static char server[64]="ws://127.0.0.1:18997", sent[256], destination[48];
static int looks, chats, names, request_id, result_id;
static u64 saved_look;
static const char *response;
static int response_status=200;
static int moderated;
static int lock_requests;
static char lock_password[129], lock_room[32];
void send_room_mod(int op,u32 target,int length) { assert(target==2);assert(length==(op==MOD_UNBAN||op==MOD_REMOVE_RIGHTS_OF?0:1));moderated=op; }
const char *net_token(void) { return "test-only-token"; }
Ent *world_ent(u32 id) { if(id==W.me)return &W.ents[0];return id==2&&W.ents[1].used?&W.ents[1]:NULL; }
NetState net_state(void) { return state; }
bool net_online(void) { return state==NET_ONLINE; }
const char *net_status(void) { return "Disconnected after 30 minutes without activity. Choose Play here to return."; }
const char *net_server(void) { return server; }
const char *show_platform(void) { return "Nintendo Switch"; }
void show_device_badge(int device,float x,float y,float k) {(void)x;(void)y;(void)k;assert(device>=1 && device<=3);}
void dbg_log(const char *fmt, ...) {(void)fmt;}
int http_download(const char *url,HttpSink sink,void *ctx,long *total,char *err,int cap){(void)url;(void)sink;(void)ctx;(void)total;if(err&&cap)err[0]=0;return 0;}
int http_download_from(const char *url,long from,HttpSink sink,void *ctx,long *total,char *err,int cap){(void)url;(void)from;(void)sink;(void)ctx;(void)total;if(err&&cap)err[0]=0;return 0;}
int net_http(const char *m,const char *p,const char *j,bool auth) {
  if(!strncmp(p,"/api/badges?guest=",18)||!strcmp(p,"/api/identity")||!strcmp(p,"/api/owner-profile")||!strncmp(p,"/static/trax/3ds/",17))return ++request_id;
  if(!auth && !strncmp(p,"/static/consoles/",17))return ++request_id;
  if(!auth && !strncmp(p,"/api/pay-link?token=",20))return ++request_id;
  assert(auth);
  if(!strcmp(m,"POST") && !strcmp(p,"/api/rooms/lock")) {
    json_t *v=json_loads(j,0,NULL);assert(v);
    copy(lock_password,sizeof lock_password,json_string_value(json_object_get(v,"password")));
    copy(lock_room,sizeof lock_room,json_string_value(json_object_get(v,"slug")));
    json_decref(v);lock_requests++;
  }
  else if(!strcmp(m,"POST") && !strcmp(p,"/api/rooms/rename")) {
    json_t *v=json_loads(j,0,NULL);assert(v && json_is_string(json_object_get(v,"title")) && !strcmp(json_string_value(json_object_get(v,"slug")),W.slug));json_decref(v);
  }
  else if(!strcmp(m,"POST")) { assert(!strcmp(p,"/api/rooms") && j);json_t *v=json_loads(j,0,NULL);assert(v && !strcmp(json_string_value(json_object_get(v,"token")),net_token()));json_decref(v); }
  else assert(!strcmp(m,"GET") && !strncmp(p,"/api/rooms?scope=",17) && !j);
  return ++request_id;
}
bool net_http_result(int id,HttpResult *out) {
  if(id!=result_id)return false;
  *out=(HttpResult){.status=response_status,.body=response?strdup(response):NULL};result_id=0;return true;
}
void net_goto(const char *slug,const char *arrival) { assert(!*arrival);copy(destination,sizeof destination,slug); }
void net_unlock(const char *password) { copy(sent,sizeof sent,password); }
void net_resume(void) {}
void show_action(int action) { assert(action==SHOW_OFFLINE); }
void send_chat(const char *s) { chats++;copy(sent,sizeof sent,s); }
void send_name(const char *s) { names++;copy(sent,sizeof sent,s); }
void send_look(u64 look) { looks++;saved_look=look; }
void fill_rect(float x,float y,float w,float h,u32 c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
float text_width(const char *s,float size) { (void)size;return strlen(s)*6; }
void text_fit(char *s,float size,float max) { (void)size; if(strlen(s)*6>max)s[(int)max/6]=0; }
float text_draw(const char *s,float x,float y,float size,u32 c,int align) { (void)s;(void)x;(void)y;(void)size;(void)c;(void)align;return 0; }
void gfx_sprite(int p,int sx,int sy,int sw,int sh,float x,float y,float k,bool flip,u32 c) {
  (void)p;(void)sx;(void)sy;(void)sw;(void)sh;(void)x;(void)y;(void)k;(void)flip;(void)c;
}
void gfx_sprite_solid(int p,int sx,int sy,int sw,int sh,float x,float y,float kx,float ky,bool flip,u32 c) {
  (void)p;(void)sx;(void)sy;(void)sw;(void)sh;(void)x;(void)y;(void)kx;(void)ky;(void)flip;(void)c;
}
void gfx_sprite_xy(int p,int sx,int sy,int sw,int sh,float x,float y,float kx,float ky,bool flip,u32 c) {
  (void)p;(void)sx;(void)sy;(void)sw;(void)sh;(void)x;(void)y;(void)kx;(void)ky;(void)flip;(void)c;
}
void fill_round(float x,float y,float w,float h,float r,u32 c) { (void)x;(void)y;(void)w;(void)h;(void)r;(void)c; }
void fill_circle(float cx,float cy,float r,u32 c) { (void)cx;(void)cy;(void)r;(void)c; }
const SceneArt *gfx_art(const char *name) { (void)name; return NULL; }
void gfx_art_draw(const SceneArt *a,float x,float y,float kx,float ky,u32 color) { (void)a;(void)x;(void)y;(void)kx;(void)ky;(void)color; }
void draw_line(float x0,float y0,float x1,float y1,u32 c) { (void)x0;(void)y0;(void)x1;(void)y1;(void)c; }
void fill_triangle(float x0,float y0,float x1,float y1,float x2,float y2,u32 c) { (void)x0;(void)y0;(void)x1;(void)y1;(void)x2;(void)y2;(void)c; }
static void click(int cmd,int arg) {
  client_ui_draw();
  for(int i=0;i<U.count;i++)if(U.buttons[i].cmd==cmd && U.buttons[i].arg==arg) {
    Button b=U.buttons[i];assert(client_ui_tap(b.x+b.w/2,b.y+b.h/2));return;
  }
  assert(!"Missing control");
}
static void check_bounds(void) {
  client_ui_draw();
  for(int i=0;i<U.count;i++) {
    Button b=U.buttons[i];assert(b.x>=0 && b.y>=0 && b.w>0 && b.h>0);
    assert(b.x+b.w<=SCREEN_W && b.y+b.h<=SCREEN_H);
  }
}
static void check_pad_reachability(void) {
  client_ui_draw();
  bool seen[100]={0};int queue[100],head=0,tail=0;
  int saved=U.focus;seen[saved]=true;queue[tail++]=saved;
  const int dirs[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
  while(head<tail) {
    int from=queue[head++];
    for(int d=0;d<4;d++) {
      U.focus=from;assert(client_ui_move(dirs[d][0],dirs[d][1]));
      if(!seen[U.focus]){seen[U.focus]=true;queue[tail++]=U.focus;}
    }
  }
  if(tail!=U.count){
    fprintf(stderr,"Unreachable controls: device=%d panel=%d focus=%d reached=%d total=%d\n",TWEEBBO_DEVICE,U.panel,saved,tail,U.count);
    for(int i=0;i<U.count;i++)if(!seen[i])fprintf(stderr,"control %d cmd=%d arg=%d rect=%.0f,%.0f,%.0f,%.0f\n",i,U.buttons[i].cmd,U.buttons[i].arg,U.buttons[i].x,U.buttons[i].y,U.buttons[i].w,U.buttons[i].h);
  }
  assert(tail==U.count);
  U.focus=saved;
}
static void trade_snapshot(const char *stage,int revision,bool final,int count) {
  static char payload[2048];
  json_t *j=json_pack("{s:s,s:i,s:i,s:s,s:b,s:b,s:b,s:[],s:[]}","stage",stage,"revision",revision,"peer",2,"peerName","Visitor","okMine",0,"okPeer",0,"confirming",final,"mine","theirs");
  for(int i=0;i<count;i++)for(int side=0;side<2;side++)json_array_append_new(json_object_get(j,side?"theirs":"mine"),json_pack("{s:i,s:i}","id",100+i+side*100,"def",1));
  char *text=json_dumps(j,JSON_COMPACT);copy(payload,sizeof payload,text);free(text);json_decref(j);
  W.trade_json=payload;W.trade_rev++;client_ui_update();
}
int main(void) {
  client_ui_reset();
  assert(!client_ui_animation_active());
  open_panel(CHAT);
  assert(client_ui_animation_active());
  for(int i=0;i<30;i++)client_ui_chat(0,"History fixture");
  client_ui_action(SHOW_L);
  assert(U.chat_page==1);
  int history_end=U.chat_n-U.chat_page*5-U.chat_pending;
  client_ui_chat(0,"Incoming while reading");
  assert(U.chat_n-U.chat_page*5-U.chat_pending==history_end);
  client_ui_action(SHOW_R);
  assert(!U.chat_page && !U.chat_pending);
  client_ui_reset();
  W.me=1;W.in_room=true;W.ents[0].used=true;copy(W.name,sizeof W.name,"Tester");copy(W.ents[0].name,sizeof W.ents[0].name,"Tester");
  client_ui_init();
  client_ui_alert("{\"id\":12345,\"title\":\"Are you still here?\",\"message\":\"You will be disconnected in 2 minutes without activity.\"}");
  check_bounds();check_pad_reachability();assert(U.count==1 && U.buttons[0].cmd==ALERT_CLOSE);
  client_ui_action(SHOW_A);assert(!U.alert);
  state=NET_REPLACED;check_bounds();assert(U.count==1 && U.buttons[0].cmd==RESUME);
  state=NET_ONLINE;
  for(int tv=0;tv<2;tv++) {
    client_ui_set_tv(tv);
    for(int panel=ME;panel<=HELP;panel++) {open_panel(panel);check_bounds();check_pad_reachability();}
    open_panel(CHAT);edit_text(EDIT_CHAT);check_bounds();check_pad_reachability();
    assert(client_ui_room_left()>360 && client_ui_room_left()<SCREEN_W);
    client_ui_draw();
    assert(U.buttons[U.focus].cmd==KEY && U.buttons[U.focus].arg==10);
    client_ui_action(SHOW_A);assert(!strcmp(U.input,"q"));
    client_ui_action(SHOW_X);assert(!*U.input);
    client_ui_action(SHOW_L);client_ui_action(SHOW_A);assert(!strcmp(U.input,"Q"));
    client_ui_action(SHOW_R);assert(!strcmp(U.input,"Q "));
    int focus=U.focus;
    client_ui_set_tv(!tv);client_ui_draw();assert(U.focus==focus && !strcmp(U.input,"Q "));
    client_ui_action(SHOW_Y);assert(!strcmp(sent,"Q") && U.panel==CHAT);
    edit_text(EDIT_CHAT);click(SYMBOLS,0);check_bounds();check_pad_reachability();click(KEY,10);assert(!strcmp(U.input,"@"));click(BACKSPACE,0);click(CLOSE,0);
    chats=0;U.draft[0]=0;
  }
  client_ui_set_tv(false);open_panel(CLOSED);
  assert(client_ui_room_left()==0);
  assert(client_ui_action(SHOW_ZL) && U.panel==MORE);
  open_panel(MORE);check_bounds();check_pad_reachability();click(OPEN,SUPPORT);assert(U.panel==SUPPORT);open_panel(CLOSED);
  puts("OK: every panel/key reachable by pad, keyboard shortcuts and dock changes preserve text/focus.");
  W.ents[1]=(Ent){.used=true,.id=2,.guest=2};strcpy(W.ents[1].name,"Visitor");W.room_owner=true;W.room_rights=true;W.owner=1;
  open_panel(PEOPLE);check_bounds();check_pad_reachability();click(SELECT_PERSON,2);
  check_bounds();check_pad_reachability();click(MODERATE,MOD_KICK);check_bounds();check_pad_reachability();
  click(CLOSE,0);assert(!moderated);click(MODERATE,MOD_KICK);W.room_rights=false;click(CONFIRM_MOD,0);assert(!moderated);
  W.room_rights=true;click(CONFIRM_MOD,0);assert(moderated==MOD_KICK);
  W.rights_json="{\"holders\":[{\"guest\":2,\"name\":\"Visitor\"}],\"bans\":[{\"guest\":2,\"name\":\"Visitor\"}]}";
  open_panel(RIGHTS);check_bounds();check_pad_reachability();click(UNBAN,2);assert(moderated==MOD_UNBAN);click(REVOKE,2);assert(moderated==MOD_REMOVE_RIGHTS_OF);
  W.room_owner=false;activate(UNBAN,2);assert(moderated==MOD_REMOVE_RIGHTS_OF);W.room_owner=true;
  puts("OK: moderation confirmation, permission loss, rights removal and lifting bans.");
  strcpy(W.slug,"fixture");client_ui_room();
  open_panel(MORE);click(OPEN,SETTINGS);assert(U.job);
  response="{\"rooms\":[{\"slug\":\"fixture\",\"locked\":false}]}";result_id=U.job;client_ui_update();
  assert(U.lock_known && !U.locked);check_bounds();check_pad_reachability();
  click(WRITE,EDIT_ROOM_NAME);strcpy(U.input,"Renamed room");click(SUBMIT,0);assert(U.job && U.job_kind==4);
  int rename_job=U.job;submit();assert(U.job==rename_job);
  response="{\"title\":\"Renamed room\"}";result_id=U.job;client_ui_update();assert(!strcmp(W.title,"Renamed room"));
  click(WRITE,EDIT_ROOM_NAME);strcpy(U.input,"x");click(SUBMIT,0);
  response="{\"error\":\"Invalid room name\"}";response_status=400;result_id=U.job;client_ui_update();assert(strstr(U.status,"Invalid") && !strcmp(W.title,"Renamed room"));response_status=200;
  click(WRITE,EDIT_ROOM_NAME);W.room_owner=false;client_ui_update();assert(U.panel==CLOSED);W.room_owner=true;
  open_panel(SETTINGS);response="{\"rooms\":[{\"slug\":\"fixture\",\"locked\":false}]}";result_id=U.job;client_ui_update();
  click(WRITE,EDIT_ROOM_PASSWORD);strcpy(U.input,"abc");click(SUBMIT,0);assert(U.panel==KEYBOARD && !lock_requests);
  strcpy(U.input,"test-password");click(SUBMIT,0);assert(U.panel==LOCK_CONFIRM && !*U.input);
  check_bounds();check_pad_reachability();click(CLOSE,0);assert(!*U.room_password && !lock_requests);
  result_id=U.job;client_ui_update();
  click(WRITE,EDIT_ROOM_PASSWORD);strcpy(U.input,"test-password");click(SUBMIT,0);click(SAVE_LOCK,0);
  assert(lock_requests==1 && !strcmp(lock_password,"test-password") && !strcmp(lock_room,"fixture") && !*U.room_password);
  activate(SAVE_LOCK,0);assert(lock_requests==1);
  response="{\"locked\":true}";result_id=U.job;client_ui_update();assert(U.lock_known && U.locked);
  click(REMOVE_LOCK,0);click(SAVE_LOCK,0);assert(lock_requests==2 && !*lock_password);
  response="{\"locked\":false}";result_id=U.job;client_ui_update();assert(!U.locked);
  click(WRITE,EDIT_ROOM_PASSWORD);strcpy(U.input,"secret");W.room_owner=false;client_ui_update();
  assert(U.panel==CLOSED && !*U.input && !*U.room_password);W.room_owner=true;
  open_panel(SETTINGS);result_id=U.job;response="{\"rooms\":[]}";client_ui_update();
  click(WRITE,EDIT_ROOM_PASSWORD);strcpy(U.input,"secret");strcpy(W.slug,"different");client_ui_room();
  assert(U.panel==CLOSED && !*U.input);strcpy(W.slug,"fixture");client_ui_room();
  open_panel(SETTINGS);result_id=U.job;client_ui_update();click(WRITE,EDIT_ROOM_PASSWORD);strcpy(U.input,"secret");click(SUBMIT,0);click(SAVE_LOCK,0);
  response="{\"error\":\"Forbidden\"}";response_status=403;result_id=U.job;client_ui_update();
  assert(!U.lock_known && strstr(U.status,"Could not confirm"));response_status=200;
  click(WRITE,EDIT_ROOM_PASSWORD);strcpy(U.input,"secret");click(SUBMIT,0);click(SAVE_LOCK,0);
  strcpy(W.slug,"different");client_ui_room();open_panel(SETTINGS);
  response="{\"locked\":true}";result_id=U.job;client_ui_update();
  assert(!U.lock_known);strcpy(W.slug,"fixture");client_ui_room();
  puts("OK: room passwords, confirmation/cancel, duplicate guard, removal, permission/room changes, errors and pad access.");
  open_panel(CLOSED);
  click(OPEN,ME);click(OPEN,DRESS);check_bounds();
  u64 original=look_encode(&U.look);
  click(CATEGORY,1);click(STYLE,1);click(PICK_COLOR,1);click(CLOSE,0);
  assert(!looks && U.panel==ME);
  click(OPEN,DRESS);click(CATEGORY,1);click(STYLE,1);click(SAVE_LOOK,0);
  assert(looks==1 && saved_look!=original && U.panel==ME);
  click(RENAME,0);check_bounds();copy(U.input,sizeof U.input,"New Name");click(SUBMIT,0);
  assert(names==1 && !strcmp(sent,"New Name"));
  client_ui_action(SHOW_Y);click(KEY,40);assert(!strcmp(U.input,"ñ"));
  click(BACKSPACE,0);assert(!*U.input);click(SHIFT,0);click(KEY,40);assert(!strcmp(U.input,"Ñ"));
  click(SPACE,0);click(KEY,10);click(CLOSE,0);assert(!chats && !strcmp(U.draft,"Ñ Q"));
  click(WRITE,EDIT_CHAT);click(SUBMIT,0);assert(chats==1 && !strcmp(sent,"Ñ Q") && !U.draft[0]);
  click(WRITE,EDIT_CHAT);copy(U.input,sizeof U.input,"  ");click(SUBMIT,0);assert(chats==1 && U.panel==KEYBOARD);
  state=NET_RETRYING;copy(U.input,sizeof U.input,"Keep this draft");click(SUBMIT,0);assert(chats==1 && U.panel==KEYBOARD);
  state=NET_ONLINE;click(CLOSE,0);
  for(int i=0;i<80;i++)client_ui_chat(1,"A long message that must wrap without dropping its ending, including accented words: canción, pingüino.");
  assert(U.chat_n==COUNT(U.chat));click(PAGE,1);check_bounds();
  client_ui_room();assert(U.chat_n==COUNT(U.chat));
  strcpy(W.slug,"another-room");client_ui_room();assert(!U.chat_n);
  open_panel(ROOMS);check_bounds();assert(U.job);
  response="{\"rooms\":[{\"slug\":\"park\",\"title\":\"Classic Park\",\"online\":4},{\"title\":\"Invalid\"}],\"models\":[{\"key\":\"cuarto\",\"name\":\"Small room\"}]}";result_id=U.job;
  client_ui_update();assert(U.room_n==1 && !U.job);check_pad_reachability();
  click(SEARCH,0);strcpy(U.input,"PARK");click(SUBMIT,0);assert(U.visible_n==1);
  click(SEARCH,0);strcpy(U.input,"missing");click(SUBMIT,0);assert(U.visible_n==0);
  click(CLEAR_SEARCH,0);assert(U.visible_n==1);
  click(OPEN,NEW_ROOM);check_bounds();check_pad_reachability();click(CREATE_ROOM,0);assert(!U.job);
  click(WRITE,EDIT_TITLE);strcpy(U.input,"Test room");click(SUBMIT,0);click(CREATE_ROOM,0);assert(U.job);
  int pending=U.job;activate(CREATE_ROOM,0);assert(U.job==pending);
  response="{\"error\":\"Invalid room name or layout.\"}";response_status=400;result_id=U.job;client_ui_update();assert(!U.job && strstr(U.status,"Invalid"));
  click(CREATE_ROOM,0);response="{\"slug\":\"new-room\"}";response_status=200;result_id=U.job;client_ui_update();assert(!strcmp(destination,"new-room") && U.panel==CLOSED);
  U.panel=ROOMS;click(ENTER_ROOM,0);assert(!strcmp(destination,"park"));
  open_panel(ROOMS);response="invalid JSON";result_id=U.job;client_ui_update();assert(U.error[0] && !U.room_n);
  click(REFRESH,0);response="{\"rooms\":[]}";result_id=U.job;client_ui_update();assert(!U.error[0] && !U.room_n);
  click(REFRESH,0);strcpy(server,"offline");state=NET_OFFLINE;client_ui_update();assert(!U.job && U.panel==CLOSED && !U.chat_n);
  open_panel(ROOMS);assert(!U.error[0] && !U.job && U.visible_n==13);check_bounds();
  int featured=0,game_rooms=0;
  for(int i=0;i<U.room_n;i++){
    featured+=U.rooms[i].featured;
    if(!strcmp(U.rooms[i].slug,"battleball")||!strcmp(U.rooms[i].slug,"snowstorm")){
      assert(U.rooms[i].official&&!U.rooms[i].featured);game_rooms++;
    }
  }
  assert(featured==3 && game_rooms==2);
  click(ENTER_ROOM,0);assert(!strcmp(destination,"lobby"));
  open_panel(HELP);check_bounds();client_ui_draw();assert(client_ui_move(0,1));
  state=NET_LOCKED;W.in_room=false;open_panel(CLOSED);click(WRITE,EDIT_PASSWORD);
  copy(U.input,sizeof U.input,"private-password");click(CLOSE,0);assert(!*U.input);
  click(WRITE,EDIT_PASSWORD);copy(U.input,sizeof U.input,"private-password");click(SUBMIT,0);
  assert(!strcmp(sent,"private-password") && !*U.input);
  puts("OK: wardrobe save/cancel, chat drafts/UTF-8/history, reconnect guards, room API/error/reset, password clearing and control bounds.");
  state=NET_ONLINE;W.in_room=true;W.w=10;W.h=10;W.room_owner=true;W.guest_id=1;W.credits=100000;
  open_panel(MORE);check_bounds();check_pad_reachability();click(OPEN_SHOP,SHOP_CATALOGUE);
  check_bounds();check_pad_reachability();
  int category=0;while(category<cat_page_count && strcmp(cat_pages[category].id,"6"))category++;
  assert(category<cat_page_count);click(SELECT_CATEGORY,category);check_bounds();check_pad_reachability();
  int offers[4096],offer_n=page_offers(offers,COUNT(offers)),selected=-1;
  for(int i=0;i<offer_n;i++)if(cat_offers[offers[i]].available && cat_offers[offers[i]].amount){selected=offers[i];break;}
  assert(selected>=0);client_shop_action(SELECT_OFFER,selected);check_bounds();check_pad_reachability();
  click(BUY,0);assert(purchases==1 && S.pending==BUY);client_shop_action(BUY,0);assert(purchases==1);
  client_shop_notice("Welcome to the hotel.");assert(S.pending==BUY);
  clock_ms=11000;client_shop_update();assert(S.timed_out && S.pending==BUY);client_shop_action(BUY,0);assert(purchases==1);
  W.inv[0]=(InvItem){500,cat_offers[selected].def};W.inv_n=1;client_shop_update();assert(!S.pending);
  client_shop_open(SHOP_INVENTORY);check_bounds();check_pad_reachability();click(PICK_OWNED,500);check_bounds();check_pad_reachability();
  W.inv[0].def=1;click(PLACE,0);assert(S.ghost.on);check_bounds();
  client_shop_tile(4,4);assert(S.ghost.ok);client_ui_action(SHOW_A);assert(placements==1 && S.pending);
  client_ui_action(SHOW_A);client_ui_action(SHOW_B);assert(placements==1 && S.ghost.on);
  W.items[0]=(Item){.used=true,.id=500,.def=1,.owner=1,.x=4,.y=4,.rot=S.ghost.rot,.z=S.ghost.z};
  client_shop_item(501);assert(S.pending);client_shop_item(500);assert(!S.pending && !S.ghost.on);
  check_bounds();check_pad_reachability();click(PICKUP_ITEM,0);assert(!pickups);check_bounds();check_pad_reachability();click(CONFIRM_ITEM_ACTION,0);assert(pickups==1);
  W.items[0].used=false;client_shop_update();assert(!S.pending && S.panel==ROOM_ITEMS);
  client_shop_open(SHOP_INVENTORY);client_shop_action(SELECT_OWNED,500);W.room_owner=false;W.room_rights=false;
  client_shop_action(PLACE,0);assert(!S.ghost.on);
  W.room_owner=true;client_shop_action(PLACE,0);assert(S.ghost.on);client_shop_tile(0,0);assert(!S.ghost.ok);
  client_shop_tile(3,3);W.ents[0].used=true;W.ents[0].x=3;W.ents[0].y=3;client_shop_update();assert(!S.ghost.ok);
  client_ui_room();assert(!S.ghost.on && !S.pending);
  puts("OK: furniture controls, purchase acknowledgement/duplicate guard/timeout, place acknowledgement, pickup confirmation and permission checks.");
  open_panel(SHOP);S.panel=SPACES;S.finish=0;
  check_bounds();check_pad_reachability();click(FINISH_PREVIEW,0);
  assert(!strcmp(client_shop_wall(),finish_choices[0].id) && !*W.wall);
  open_panel(CHAT);assert(!*client_shop_wall());open_panel(SHOP);
  click(FINISH_STEP,1);assert(S.finish!=0);click(FINISH_STEP,-1);assert(S.finish==0);
  click(FINISH_BUY,0);check_bounds();check_pad_reachability();click(FINISH_CONFIRM,0);
  assert(finish_purchases==1 && S.pending==FINISH_CONFIRM);
  client_shop_action(FINISH_CONFIRM,0);assert(finish_purchases==1);
  client_shop_notice("Welcome to the hotel.");assert(S.pending);
  W.finish_rev++;strcpy(W.wall,finish_choices[0].id);client_shop_update();assert(!S.pending);
  client_shop_action(FINISH_BUY,0);assert(S.panel==SPACES);
  click(FINISH_KIND,2);check_bounds();check_pad_reachability();assert(finish()->kind==2);
  W.room_owner=false;W.room_rights=true;client_shop_action(FINISH_BUY,0);assert(S.panel==SPACES);
  assert(!strcmp(client_shop_floor(),W.floor));W.room_owner=true;
  click(FINISH_BUY,0);click(FINISH_CONFIRM,0);client_shop_notice("You do not have enough credits.");assert(!S.pending);
  client_ui_room();assert(!S.preview && !S.pending);
  puts("OK: Spaces preview isolation, controller access, owner-only purchase, duplicate guard and server acknowledgement.");
  open_panel(PETS);check_bounds();check_pad_reachability();click(4003,0);client_ui_update();check_bounds();check_pad_reachability();
  click(4007,0);strcpy(U.input,"Bobby");click(SUBMIT,0);assert(U.panel==PETS);
  click(4008,0);check_bounds();check_pad_reachability();click(4009,0);
  json_t *adoption=json_loads(pet_request,0,NULL);assert(adoption && !strcmp(json_string_value(json_object_get(adoption,"name")),"Bobby"));json_decref(adoption);
  int pet_before=pet_requests;client_pets_action(4009,0);assert(pet_before==pet_requests);
  W.pet_inv_json="[{\"id\":5001,\"owner\":1,\"name\":\"Bobby\",\"kind\":0,\"room\":null}]";W.pet_inv_rev++;
  client_ui_update();click(4000,5001);check_bounds();check_pad_reachability();click(4011,0);assert(strstr(pet_request,"place"));
  W.pet_inv_json="[{\"id\":5001,\"owner\":1,\"name\":\"Bobby\",\"kind\":0,\"room\":\"another-room\"}]";W.pet_inv_rev++;
  client_ui_update();check_bounds();check_pad_reachability();click(4010,5);assert(strstr(pet_request,"feed"));
  client_pets_notice("Unrelated hotel message");pet_before=pet_requests;client_pets_action(4010,6);assert(pet_before==pet_requests);
  W.pet_inv_rev++;client_ui_update();click(4010,6);assert(strstr(pet_request,"drink"));
  client_pets_reset();
  puts("OK: pet adoption confirmation/duplicate guard, appearance preview, naming, placement, care and control bounds.");
  W.duel=(Duel){.id=42,.kind=3,.players={1,2},.turn=0,.move_count=8};
  for(int i=0;i<8;i++){W.duel.moves[i][0]=8+i;W.duel.moves[i][1]=16+i;W.duel.board[8+i]=1;}
  W.game_rev++;client_ui_update();open_panel(GAMES);check_bounds();check_pad_reachability();click(3005,8);check_bounds();check_pad_reachability();
  W.invites[0]=(GameInvite){2,3};check_bounds();check_pad_reachability();
  puts("OK: chess source/destination selection and game invitations remain reachable by pad.");
  client_trade_reset();open_panel(PEOPLE);click(OPEN,TRADE);check_bounds();check_pad_reachability();
  click(5011,2);assert(strstr(trade_request,"invite"));trade_snapshot("invite",1,false,0);
  check_bounds();check_pad_reachability();trade_snapshot("open",1,false,0);
  check_bounds();check_pad_reachability();click(5003,0);
  W.inv[0]=(InvItem){100,1};W.inv_n=1;check_bounds();check_pad_reachability();click(5008,100);
  check_bounds();check_pad_reachability();click(5009,100);assert(strstr(trade_request,"add"));
  trade_snapshot("open",2,false,12);check_bounds();check_pad_reachability();
  click(5005,1);check_bounds();check_pad_reachability();click(5006,103);check_bounds();check_pad_reachability();click(5004,0);
  click(5007,200);check_bounds();check_pad_reachability();click(5004,0);
  click(5002,0);assert(strstr(trade_request,"ready"));trade_snapshot("open",3,true,12);
  check_bounds();check_pad_reachability();click(5002,0);assert(strstr(trade_request,"confirm"));
  int trade_before=trade_requests;activate(5002,0);assert(trade_requests==trade_before);
  trade_snapshot("done",3,true,12);check_bounds();check_pad_reachability();click(5012,0);
  trade_snapshot("invite",1,false,0);check_bounds();check_pad_reachability();click(5000,0);
  trade_snapshot("open",1,false,0);click(CLOSE,0);assert(strstr(trade_request,"cancel"));
  open_panel(CHAT);edit_text(EDIT_CHAT);client_ui_draw();U.focus=12;
  strcpy(U.input,"Keep typing");trade_snapshot("cancelled",1,false,0);
  assert(U.panel==KEYBOARD && U.focus==12 && !strcmp(U.input,"Keep typing"));
  click(CLOSE,0);
  puts("OK: trading invitation, 12-item paging, both offer details, inventory, two confirmations and cancellation are reachable by pad on both canvases.");
  client_shop_reset();client_shop_open(SHOP_CATALOGUE);open_panel(SHOP);click(SEARCH_OFFERS,0);
  strcpy(U.input,"CHAIR");click(SUBMIT,0);assert(S.category==-1 && U.panel==SHOP);
  int results[4096],found=page_offers(results,COUNT(results));assert(found>0);
  for(int i=0;i<found;i++)assert(matches(cat_offers[results[i]].name,"chair") || matches(cat_offers[results[i]].desc,"chair"));
  check_bounds();check_pad_reachability();click(CLEAR_QUERY,0);click(SEARCH_OFFERS,0);
  strcpy(U.input,"no-match-furniture-abcdef");click(SUBMIT,0);assert(!page_offers(results,COUNT(results)));check_bounds();check_pad_reachability();
  click(CLEAR_QUERY,0);click(SEARCH_OFFERS,0);strcpy(U.input,"cancelled");click(CLOSE,0);assert(strcmp(S.query,"cancelled"));
  const FurniDef *dispenser=NULL,*dice=NULL,*seat=NULL,*toggle=NULL;
  for(int i=0;i<furni_def_count;i++) {
    int ids[32];const FurniDef *d=&furni_defs[i];
    if(drinks(d,ids)>1)dispenser=d;
    if(d->kind==KIND_DICE)dice=d;
    if(d->kind==KIND_SIT && !(d->flags&(DEF_LAY|DEF_TELEPORTER)))seat=d;
    if(d->kind==KIND_TOGGLE && !d->casino)toggle=d;
  }
  assert(dispenser && dice && seat && toggle);
  W.items[0]=(Item){.used=true,.id=999,.def=dispenser->id};client_ui_inspect(999);
  check_bounds();check_pad_reachability();int ids[32],n=drinks(dispenser,ids);click(DRINK_STEP,1);assert(S.drink==1);
  click(GET_DRINK,0);assert(drink_item==999 && drink_id==ids[1] && U.panel==CLOSED);
  client_ui_inspect(999);client_shop_action(DRINK_STEP,-1);assert(S.drink==n-1);W.items[0].used=false;
  drink_id=0;activate(GET_DRINK,0);assert(!drink_id);W.items[0].used=true;
  W.items[0].def=dice->id;W.items[0].state=7;client_ui_inspect(999);used_item=0;activate(USE_ITEM,0);assert(!used_item);
  W.items[0].state=3;check_bounds();check_pad_reachability();click(CLOSE_DICE,0);assert(used_item==999 && used_op==1 && U.panel==CLOSED);
  W.items[0].def=seat->id;W.items[0].x=4;W.items[0].y=5;walked_x=walked_y=-1;
  client_ui_inspect(999);click(USE_ITEM,0);assert(walked_x==4 && walked_y==5 && U.panel==CLOSED);
  W.items[0].def=1086;W.items[0].state=0;used_item=0;client_ui_inspect(999);
  check_bounds();check_pad_reachability();click(CHANGE_STATE,0);assert(used_item==999 && used_op==0);
  used_item=0;walked_x=walked_y=-1;client_ui_inspect(999);click(USE_ITEM,0);
  assert(!used_item && walked_x==4 && walked_y==5);
  W.items[0].def=1047;W.items[0].state=8;used_item=0;client_ui_inspect(999);activate(USE_ITEM,0);assert(!used_item);
  W.items[0].state=7;client_ui_inspect(999);check_bounds();check_pad_reachability();click(USE_ITEM,0);
  assert(used_item==999 && used_op==0);
  W.items[0].def=toggle->id;client_ui_inspect(999);state=NET_RETRYING;used_item=0;activate(USE_ITEM,0);assert(!used_item);state=NET_ONLINE;
  open_panel(ME);click(OPEN,ACTIONS);check_bounds();check_pad_reachability();click(GESTURE,GESTURE_DANCE);assert(gestured==GESTURE_DANCE);
  W.ents[0].drink=ids[0];open_panel(ACTIONS);check_bounds();check_pad_reachability();click(PUT_DRINK_AWAY,0);assert(!drink_id && !drink_item);
  W.ents[0].flags=FLAG_LAY;open_panel(ACTIONS);check_bounds();check_pad_reachability();
  gestured=0;activate(GESTURE,GESTURE_DANCE);assert(gestured==GESTURE_DANCE);
  activate(GESTURE,GESTURE_SIT);assert(gestured==GESTURE_SIT);
  activate(GESTURE,GESTURE_WAVE);assert(gestured==GESTURE_WAVE);
  state=NET_RETRYING;gestured=0;activate(GESTURE,GESTURE_WAVE);assert(!gestured);state=NET_ONLINE;
  state=NET_OFFLINE;W.items[0].def=seat->id;W.items[0].x=4;W.items[0].y=5;
  client_ui_inspect(999);click(USE_ITEM,0);assert(walked_x==4 && walked_y==5);
  W.items[0].def=dispenser->id;client_ui_inspect(999);click(GET_DRINK,0);assert(drink_item==999);
  W.items[0].def=dice->id;used_item=0;client_ui_inspect(999);activate(USE_ITEM,0);assert(!used_item);
  state=NET_ONLINE;
  const FurniDef *cabinet=NULL;for(int i=0;i<furni_def_count;i++)if(furni_defs[i].casino){cabinet=&furni_defs[i];break;}
  assert(cabinet);W.items[0].def=cabinet->id;strcpy(W.slug,"casino");
  const char *casino_keys[]={"slot","roulette","blackjack","crash","poker","videopoker","dash"};
  for(int game=0;game<7;game++) {
    client_casino_reset();client_ui_inspect(999);click(OPEN_CASINO,0);assert(used_item==999 && U.panel==CASINO);
    char payload[800];snprintf(payload,sizeof payload,"{\"t\":\"open\",\"mode\":\"free\",\"item\":999,\"game\":\"%s\",\"bets\":[1,2,5,10,25,50],\"hand\":{\"player\":[0,4],\"dealer\":[2,-1],\"bet\":5},\"poker\":{\"player\":[0,4],\"board\":[2,3,4],\"ante\":5},\"video\":{\"cards\":[0,1,2,3,4]},\"dash\":{\"step\":1,\"steps\":8},\"round\":{\"phase\":\"idle\"}}",casino_keys[game]);
    client_casino_receive(payload);check_bounds();check_pad_reachability();
    if(game==5) {
      int holds=0,draws=0;
      for(int i=0;i<U.count;i++){holds+=U.buttons[i].cmd==6004;draws+=U.buttons[i].cmd==6005;}
      assert(holds==5 && draws==1);
    }
    click(6000,1);check_bounds();check_pad_reachability();
    click(6000,2);check_bounds();check_pad_reachability();
    click(6000,0);click(6009,0);check_bounds();check_pad_reachability();click(6009,0);
  }
  client_casino_reset();
  open_panel(MORE);check_bounds();check_pad_reachability();click(OPEN,SNOW);
  check_bounds();assert(U.count==1 && U.buttons[0].cmd==CLOSE);
  client_ui_tap(315,SCREEN_H-16);assert(U.panel==SNOW);
  client_ui_action(SHOW_Y);assert(U.panel==SNOW);
  client_ui_action(SHOW_B);assert(U.panel==CLOSED);
  strcpy(W.model,"pool_a");clock_ms=10000;
  pool_receive("{\"now\":10000,\"state\":{\"swimmers\":{\"1\":{\"color\":2,\"booth\":[17,11]}}}}",clock_ms);
  assert(pool_mode()==1 && pool_swimsuit(1)==2);open_panel(POOL);check_bounds();check_pad_reachability();
  click(8006,0);click(8008,0);assert(strstr(activity_request,"outfit")&&strstr(activity_request,"3"));
  pool_receive("{\"now\":10000,\"state\":{\"swimmers\":{\"1\":{\"color\":-9}}}}",clock_ms);assert(pool_swimsuit(1)==-1);
  strcpy(W.model,"pool_b");pool_receive("{\"now\":10000,\"state\":{\"dive\":{\"eid\":1,\"phase\":1,\"motion\":{\"airborne\":false}}}}",clock_ms);
  open_panel(POOL);check_bounds();client_ui_action(SHOW_A);assert(strstr(activity_request,"dive_move")&&strstr(activity_request,"8"));
  client_ui_move(-1,0);assert(strstr(activity_request,"1"));
  pool_receive("{\"now\":10000,\"state\":{\"dive\":{\"eid\":1,\"phase\":1,\"motion\":{\"airborne\":true,\"lower\":true}}}}",clock_ms);
  assert(pool_mode()==2);client_ui_action(SHOW_B);assert(U.panel==POOL&&strstr(activity_request,"7"));
  pool_receive("{\"now\":10000,\"state\":{\"dive\":{\"eid\":2,\"phase\":2,\"votes\":{}}}}",clock_ms);
  open_panel(POOL);check_bounds();check_pad_reachability();click(8004,10);assert(strstr(activity_request,"vote")&&strstr(activity_request,"10"));
  pool_room_changed();strcpy(W.slug,"afterhours");client_ui_bar();
  client_bar_receive("{\"bar\":\"menu\",\"items\":[{\"id\":28,\"price\":5,\"name\":\"Fixture drink\"}]}",clock_ms);
  check_bounds();check_pad_reachability();click(9000,0);check_bounds();click(9009,0);assert(U.panel==BAR);
  click(9000,0);W.credits=200;click(9001,0);assert(U.panel==CLOSED&&strstr(activity_request,"bar_order"));
  W.ents[0].id=1;W.ents[0].drink=28;W.ents[0].drink_until=clock_ms+10000;client_ui_bar();check_bounds();check_pad_reachability();click(9004,0);assert(strstr(activity_request,"bar_use"));
  W.ents[0].drink=19;assert(client_bar_sip(&W.ents[0],clock_ms,false)>=0);click(9004,0);assert(strstr(activity_request,"bar_use"));W.ents[0].drink=28;
  client_bar_receive("{\"bar\":\"act\",\"entity\":1,\"drink\":28}",clock_ms);assert(client_bar_sip(&W.ents[0],clock_ms,false)==1);
  assert(client_bar_sip(&W.ents[0],clock_ms+1200,false)==0);click(9006,0);assert(strstr(activity_request,"bar_drop"));
  client_identity_reset();open_panel(IDENTITY);response="{\"badges\":[\"VIP\",\"CLUB1\"]}";result_id=request_id;client_identity_update();check_bounds();check_pad_reachability();
  click(10002,0);assert(U.panel==KEYBOARD&&U.secret);strcpy(U.input,"TWEEBBO-00000000000000000000000000000000");click(SUBMIT,0);
  assert(U.panel==IDENTITY&&!*U.input);result_id=request_id;response="{\"name\":\"Fixture character\"}";client_identity_update();check_bounds();check_pad_reachability();click(10004,0);assert(!restored);
  click(10002,0);strcpy(U.input,"TWEEBBO-00000000000000000000000000000000");click(SUBMIT,0);
  result_id=request_id;client_identity_update();click(10003,0);assert(restored==1&&U.panel==CLOSED&&!*U.input);
  client_identity_close();open_panel(MUSIC);check_bounds();check_pad_reachability();click(11001,0);assert(audio_muted());click(11001,0);assert(!audio_muted());
  client_music_receive("{\"state\":{\"active\":999,\"queues\":{\"999\":[{\"provider\":\"trax\",\"title\":\"Fixture\"}]}}}",clock_ms);
  W.room_owner=W.room_rights=false;W.items[0].owner=W.guest_id+1;music_request[0]=0;
  client_music_action(11003,0);assert(!*music_request);
  W.room_rights=true;client_music_action(11003,0);assert(strstr(music_request,"play"));
  client_music_receive("{\"state\":{\"queues\":{}}}",clock_ms);music_request[0]=0;
  client_music_action(11003,0);assert(!*music_request);
  puts("OK: Lido outfit/diving/voting, paid bar confirmation and gestures, masked identity lookup/cancel and audio controls on both canvases.");
  puts("OK: Snow Storm menu hides hotel tabs and blocks global chat until the match panel closes.");
  puts("OK: seven casino control panels, cards/holds, rules and leave confirmation fit both canvases and are reachable by pad.");
  puts("OK: catalogue search/cancel/empty results, dispenser choices, dice close/rolling guard, furniture use, connection guards and character actions.");
}

/* Snow's real engine is exercised separately; these tests isolate panel routing. */
void client_snow_open(void){}
void client_snow_reset(void){}
void client_snow_update(void){}
bool client_snow_playing(void){return false;}
bool client_snow_key(int action){(void)action;return false;}
bool client_snow_move(int dx,int dy){(void)dx;(void)dy;return false;}
void client_snow_close(void){client_ui_close();}
void client_snow_draw(float x,float y,float w){(void)x;(void)y;(void)w;}
void client_snow_hud(void){}
void client_snow_action(int command,int arg){(void)command;(void)arg;}
bool client_snow_tap(float x,float y){(void)x;(void)y;return false;}

void send_activity(const char *json){snprintf(activity_request,sizeof activity_request,"%s",json);}
void send_music(const char *json){snprintf(music_request,sizeof music_request,"%s",json);}
bool audio_init(void){return true;}
void audio_exit(void){}
bool store_load(const char *key,char *out,int cap){(void)key;(void)out;(void)cap;return false;}
void store_save(const char *key,const char *value){(void)key;(void)value;}
void net_use_token(const char *token){assert(token && *token);restored++;}
