#include "language.h"
#include "client_casino.h"
#include "client_ui.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "show.h"
#include "world.h"
#include "avatar.h"
#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { SLOT, ROULETTE, BLACKJACK, CRASH, POKER, VIDEO, DASH, NO_GAME };
enum { TAB=6000, BET, CHOICE, VALUE, HOLD, PLAY, SECOND, DOUBLE, RELOAD, LEAVE, CONFIRM_LEAVE, RULE_PAGE, NUMBER, SPLIT };
static const char *keys[]={"slot","roulette","blackjack","crash","poker","videopoker","dash"};
static const char *names[]={"Slots","Roulette","Blackjack","Crash","Casino Hold'em","Video Poker","VIP Dash","Casino"};
static const char *slots[]={"Star Treasure","Lucky Sevens","Gem Ways","Candy Tumble","Coin Rush"};
static const char *paths[]={"video","slots-classic","slots-ways","slots-tumble","slots-coins"};
static const char *risks[]={"Bellboy","VIP","High Roller","Bobba"};
static const char *choices[]={"Number","Red","Black","Odd","Even","1-18","19-36","Dozen"};
static const char *rules[]={
  "Play with hotel credits. Choose a slot and a bet, then spin. Free spins keep the game and bet that awarded them. Table shows the server's result. Star Treasure has ten lines; three scatters award eight free spins. Other slots use their own lines, ways, tumbles or coin respins. The hotel calculates every payout.",
  "European roulette has one zero. A number pays 36 times the bet. Red, black, odd, even and either half pay twice the bet. A dozen pays three times. Zero loses outside bets. Payouts include the original stake.",
  "Get closer to 21 than the dealer without going over. Aces count as 1 or 11. Blackjack pays 3:2. The dealer stands on 17. Double charges another bet and draws one card; it is available with your first two cards. A push returns the stake. Table shows both hands.",
  "This cabinet shares one round. Bet before it starts and cash out before it crashes. A successful cash-out pays the bet times the multiplier. An uncashed bet is lost on a crash. The hotel decides whether your request arrived in time. The displayed multiplier is an estimate between updates.",
  "Bet the ante, then call or fold. Call costs twice the ante; folding loses it. Combine your two cards with five community cards. The dealer qualifies with a pair of fours or better. Ante wins pay royal flush 100:1, straight flush 20:1, four of a kind 10:1, full house 3:1, flush 2:1, other wins 1:1. Table shows the cards.",
  "Jacks or Better: deal five cards, hold any of them, then draw once. Payout per credit: royal flush 250, straight flush 50, four of a kind 25, full house 9, flush 6, straight 4, three of a kind 3, two pair 2, jacks or better 1. Payouts include the bet.",
  "Choose a ladder and a credit stake. Each step can lose the stake. After a safe step, cash out the displayed amount or take another step. Reaching the last step pays automatically. Bellboy has the lowest risk; Bobba the highest. Leaving pays the reached value, which is zero before the first safe step."
};
static struct {
  u32 item;
  int kind,tab,bet,bets[6],count,slot,free_left,choice,value,risk,held,page,pending;
  bool uncertain,confirm,connected,dash_loaded;
  u64 sent,ready,received;
  json_t *state,*round;
  char status[180];
} C={.kind=NO_GAME};
static int num(json_t *j,const char *key) {
  json_int_t n=json_integer_value(json_object_get(j,key));return n>=0 && n<=1000000000?(int)n:0;
}
static u32 identifier(json_t *j,const char *key) {
  json_int_t n=json_integer_value(json_object_get(j,key));return n>0 && n<=UINT32_MAX?(u32)n:0;
}
static const char *str(json_t *j,const char *key) {const char *s=json_string_value(json_object_get(j,key));return s?s:"";}
static void replace(json_t **p,json_t *v) {json_incref(v);json_decref(*p);*p=v;}
static void status(const char *s) {snprintf(C.status,sizeof C.status,"%s",s);}
void client_casino_reset(void) {json_decref(C.state);json_decref(C.round);memset(&C,0,sizeof C);C.kind=NO_GAME;}
const char *client_casino_title(void) {return names[C.kind];}
static bool present(void) {Item *it=world_item(C.item);return it && def_get(it->def) && def_get(it->def)->casino && W.in_room && !strcmp(W.slug,"casino");}
static bool available(void) {return net_online() && present() && C.kind<NO_GAME && !C.pending && !C.uncertain;}
static bool ready(void) {return available() && now_ms()>=C.ready;}
static bool playing(void) {
  if(!C.state)return false;
  if(C.kind==BLACKJACK || C.kind==POKER)return !*str(C.state,"result");
  return (C.kind==VIDEO || C.kind==DASH) && !json_is_true(json_object_get(C.state,"done"));
}
static json_t *mine(void) {
  size_t i;json_t *v;json_array_foreach(json_object_get(C.round,"bets"),i,v)if(identifier(v,"eid")==W.me)return v;
  return NULL;
}
static const char *phase(void) {
  const char *p=str(C.round,"phase");
  return !strcmp(p,"betting") && now_ms()-C.received>=(u64)num(C.round,"startsIn")?"running":p;
}
static double multiplier(void) {
  const char *p=phase();
  if(!strcmp(p,"crashed"))return json_number_value(json_object_get(C.round,"point"));
  if(strcmp(p,"running"))return 1;
  double ms=(double)(now_ms()-C.received)+(!strcmp(str(C.round,"phase"),"betting")?-num(C.round,"startsIn"):num(C.round,"elapsed"));
  return exp(fmin(100000,fmax(0,ms))*.00006);
}
static bool can_play(void) {
  if(!ready())return false;
  if(C.kind==CRASH)return (!strcmp(phase(),"betting") || !strcmp(phase(),"idle")) && !mine() && W.credits>=(u32)C.bet;
  if(playing())return C.kind!=POKER || W.credits>=(u32)num(C.state,"ante")*2;
  return C.count && (C.free_left || W.credits>=(u32)C.bet);
}
static bool can_second(void) {
  if(!available() || ((C.kind!=CRASH && C.kind!=DASH) && !ready()))return false;
  if(C.kind==CRASH)return mine() && !json_is_number(json_object_get(mine(),"out")) && !strcmp(phase(),"running");
  return playing() && (C.kind==BLACKJACK || C.kind==POKER || (C.kind==DASH && num(C.state,"step")>0));
}
static void send_action(int op,int a,int b,int c) {
  send_casino(op,a,b,c);C.pending=op;C.sent=now_ms();C.ready=C.sent+650;status("Waiting for the hotel...");
}
void client_casino_open(u32 item) {
  Item *it=world_item(item);const FurniDef *d=it?def_get(it->def):NULL;
  if(!net_online() || !d || !d->casino)return;
  if(strcmp(W.slug,"casino")){show_toast("Play this game in the hotel Casino.",now_ms());return;}
  if(C.item==item && C.pending)return;
  if(C.item && C.item!=item){show_toast("Leave the current casino game before choosing another.",now_ms());client_ui_casino();return;}
  C.item=item;C.pending=1;C.sent=now_ms();C.connected=true;C.confirm=false;
  status("Walking to the cabinet. Waiting for the hotel...");send_use(item);client_ui_casino();
}
void client_casino_update(void) {
  if(!C.item)return;
  if(C.kind==DASH && !C.dash_loaded) {
    static const u16 furniture[]={60007,1114,1098,1107};
    for(int i=0;i<COUNT(furniture);i++)gfx_furni_preload(furniture[i]);
    C.dash_loaded=true;
  }
  if(!net_online() || !present()) {
    C.uncertain=true;C.pending=0;C.connected=false;status("Reconnect, then reload the game before playing.");
  } else if(C.pending && now_ms()-C.sent>(C.pending==1?30000:12000)) {
    C.pending=0;C.uncertain=true;status("No reply. Reload to check the game; the action will not be repeated.");
  }
}
void client_casino_receive(const char *text) {
  if(!C.item || !net_online() || !present() || !text || strlen(text)>65536)return;
  json_t *j=json_loads(text,JSON_REJECT_DUPLICATES,NULL);
  if(!json_is_object(j)){json_decref(j);C.uncertain=true;status("Invalid game state. Reload before playing.");return;}
  const char *type=str(j,"t");bool answered=false;
  if(!strcmp(type,"open") && C.pending==1 && identifier(j,"item")==C.item) {
    int kind=NO_GAME;for(int i=0;i<NO_GAME;i++)if(!strcmp(str(j,"game"),keys[i]))kind=i;
    if(kind==NO_GAME || strcmp(str(j,"mode"),"free")){C.uncertain=true;C.pending=0;status("This game must use hotel credits.");json_decref(j);return;}
    C.kind=kind;C.count=0;size_t i;json_t *v;
    json_array_foreach(json_object_get(j,"bets"),i,v) {
      json_int_t n=json_integer_value(v);if(C.count<6 && n>0 && n<=(kind==DASH?100000:255))C.bets[C.count++]=(int)n;
    }
    if(!C.count){C.uncertain=true;C.pending=0;status("No valid stakes received. Reload the game.");json_decref(j);return;}
    bool found=false;for(int i=0;i<C.count;i++)found|=C.bet==C.bets[i];if(!found)C.bet=C.bets[0];
    C.free_left=num(j,"freeLeft");if(C.free_left)C.slot=num(j,"freeGame")%5;
    replace(&C.state,json_object_get(j,kind==POKER?"poker":kind==VIDEO?"video":kind==DASH?"dash":"hand"));
    replace(&C.round,json_object_get(j,"round"));C.held=0;C.uncertain=false;C.connected=true;C.received=now_ms();answered=true;
  } else if(C.kind<NO_GAME && C.connected) {
    if(!strcmp(type,"refused") || !strcmp(type,"note")) {
      C.pending=0;C.uncertain=true;status(str(j,!strcmp(type,"refused")?"message":"text"));json_decref(j);return;
    }
    if(!strcmp(type,"crash") && C.kind==CRASH && identifier(j,"item")==C.item) {
      replace(&C.round,json_object_get(j,"round"));C.received=now_ms();
      answered=(C.pending==8 && mine()) || (C.pending==9 && (json_is_number(json_object_get(mine(),"out")) || !strcmp(phase(),"crashed") || !strcmp(phase(),"idle")));
    } else if((!strcmp(type,"bj") && C.kind==BLACKJACK && ((C.pending>=4 && C.pending<=7)||C.pending==20)) ||
              (!strcmp(type,"poker") && C.kind==POKER && C.pending>=11 && C.pending<=13)) {
      json_t *hand=json_object_get(j,"hand");if(json_is_object(hand)){replace(&C.state,hand);answered=true;}
    } else if((!strcmp(type,"slot") && C.kind==SLOT && C.pending==2) ||
              (!strcmp(type,"roulette") && C.kind==ROULETTE && C.pending==3) ||
              (!strcmp(type,"vp") && C.kind==VIDEO && C.pending>=14 && C.pending<=15) ||
              (!strcmp(type,"dash") && C.kind==DASH && C.pending>=16 && C.pending<=18)) {
      replace(&C.state,j);answered=true;
      if(C.kind==SLOT){C.slot=num(j,"game")%5;C.free_left=num(j,"freeLeft");}
      if(C.kind==VIDEO)C.held=num(j,"held")&31;
    }
  }
  if(answered){C.pending=0;status("");}
  json_decref(j);
}
void client_casino_action(int cmd,int arg) {
  if(cmd==TAB && arg>=0 && arg<=2){C.tab=arg;C.page=0;client_ui_changed();return;}
  if(cmd==RULE_PAGE){if(arg==1 && C.page<8)C.page++;else if(arg==-1 && C.page>0)C.page--;return;}
  if(cmd==LEAVE){C.confirm=!C.confirm;client_ui_changed();return;}
  if(cmd==CONFIRM_LEAVE && C.confirm){if(net_online())send_casino(10,0,0,0);client_casino_reset();client_ui_close();return;}
  if(cmd==RELOAD && net_online() && !C.pending){client_casino_open(C.item);return;}
  if(cmd==SECOND && can_second()){send_action(C.kind==BLACKJACK?6:C.kind==POKER?13:C.kind==CRASH?9:18,0,0,0);return;}
  if(!ready())return;
  if(cmd==NUMBER && C.kind==ROULETTE && arg>=0 && arg<=36){C.choice=0;C.value=arg;return;}
  if(cmd==BET && !playing() && !C.free_left && arg>=0 && arg<C.count)C.bet=C.bets[arg];
  if(cmd==CHOICE && !playing() && !C.free_left && (arg==1 || arg==-1)) {
    if(C.kind==SLOT){C.slot=(C.slot+arg+5)%5;replace(&C.state,NULL);}
    if(C.kind==DASH)C.risk=(C.risk+arg+4)%4;
    if(C.kind==ROULETTE){C.choice=(C.choice+arg+8)%8;C.value=0;}
  }
  if(cmd==VALUE && C.kind==ROULETTE && (arg==1 || arg==-1)){int n=C.choice==7?3:37;C.value=(C.value+arg+n)%n;}
  if(cmd==HOLD && C.kind==VIDEO && playing() && arg>=0 && arg<5)C.held^=1<<arg;
  if(cmd==PLAY && can_play()) {
    if(C.kind==SLOT)send_action(2,C.bet,C.slot,0);
    if(C.kind==ROULETTE)send_action(3,C.choice,C.value,C.bet);
    if(C.kind==BLACKJACK)send_action(playing()?5:4,playing()?0:C.bet,0,0);
    if(C.kind==POKER)send_action(playing()?12:11,playing()?0:C.bet,0,0);
    if(C.kind==VIDEO)send_action(playing()?15:14,playing()?C.held:C.bet,0,0);
    if(C.kind==CRASH)send_action(8,C.bet,0,0);
    if(C.kind==DASH){int b=0;while(b<C.count-1 && C.bets[b]!=C.bet)b++;send_action(playing()?17:16,playing()?0:C.risk,playing()?0:b,0);}
  } else if(cmd==DOUBLE && C.kind==BLACKJACK && playing() && json_array_size(json_object_get(C.state,"player"))==2 && W.credits>=(u32)num(C.state,"bet"))send_action(7,0,0,0);
  else if(cmd==SPLIT && C.kind==BLACKJACK && playing() && json_is_true(json_object_get(C.state,"canSplit")) && W.credits>=(u32)num(C.state,"bet"))send_action(20,0,0,0);
}
static bool sprite(const char *key,float x,float y,float maxw,float maxh) {
  for(int i=0;i<ui_sprite_count;i++)if(!strcmp(ui_sprites[i].name,key)) {
    const UiSprite *s=&ui_sprites[i];float k=fminf(1,fminf(maxw/s->w,maxh/s->h));
    gfx_sprite(s->page,s->x,s->y,s->w,s->h,x+(maxw-s->w*k)/2,y+(maxh-s->h*k)/2,k,false,0xffffffff);return true;
  }return false;
}
static void cards(json_t *a,int minimum,float x,float y,float w) {
  int n=(int)json_array_size(a);if(n<minimum)n=minimum;if(n>12)n=12;
  float pitch=fminf(49,w/(n?n:1));
  for(int i=0;i<n;i++) {
    json_t *v=json_array_get(a,i);int value=json_is_integer(v)?(int)json_integer_value(v):-1;char key[48];
    if(value<0 || value>51)snprintf(key,sizeof key,"casino-cards-back");
    else snprintf(key,sizeof key,"casino-cards-%c%d","hdcs"[value%4],value/4+1);
    sprite(key,x+i*pitch,y,pitch-2,38);
  }
}
static void dash_furni(u16 def,int rot,float cx,float floor,float scale) {
  const FurniVariant *v=variant_get(def,rot,0);
  if(!v)return;
  for(int i=0;i<v->count;i++){
    const FurniPart *p=&v->parts[i];
    gfx_furni(def,p,cx+p->ox*scale,floor+(p->oy-16)*scale,scale,0x00ffffff|((u32)p->alpha<<24));
  }
}
/* Same carpet, pillars, throne and guest figures as the web/3DS Dash hall. */
static void dash_hall(float x,float y,float w) {
  float k=fminf(w/400,132.0f/240),left=x+(w-400*k)/2;
  fill_rect(x,y,w,132,0xff20103d);
  fill_rect(x,y+124*k,w,132-124*k,0xff1a1117);
  fill_rect(x,y+120*k,w,4*k,0xff008ac7);
  for(int n=1;n<=3;n++)dash_furni(60007,0,left+n*110*k,y+124*k,k);
  dash_furni(1114,0,left+18*k,y+128*k,k);
  dash_furni(1098,0,left+18*k,y+96*k,k);
  int steps=num(C.state,"steps"),step=num(C.state,"step");
  if(steps<1 || steps>15)steps=C.risk==3?10:8;
  bool lost=json_is_true(json_object_get(C.state,"lost")),done=json_is_true(json_object_get(C.state,"done"));
  float ax=left+48*k,ay=y+190*k,bx=left+334*k,by=y+202*k;
  fill_triangle(ax-34*k,ay-14*k,bx+30*k,by-14*k,bx+22*k,by+16*k,0xff2ea8e0);
  fill_triangle(ax-34*k,ay-14*k,bx+22*k,by+16*k,ax-42*k,ay+16*k,0xff2ea8e0);
  fill_triangle(ax-32*k,ay-12*k,bx+28*k,by-12*k,bx+20*k,by+14*k,0xff1d127a);
  fill_triangle(ax-32*k,ay-12*k,bx+20*k,by+14*k,ax-40*k,ay+14*k,0xff1d127a);
  dash_furni(1107,2,bx+22*k,by+4*k,k);
  for(int n=1;n<=steps;n++){
    float xx=ax+(bx-ax)*n/steps,yy=ay+(by-ay)*n/steps;
    bool trap=lost && n==step+1,passed=n<=step,next=!done && n==step+1;
    u32 color=trap?0xff4f4fff:passed?0xff69ff8d:next?0xff75e8ff:0xff008ac7;
    fill_triangle(xx-8*k,yy,xx,yy-4*k,xx+8*k,yy,color);
    fill_triangle(xx-8*k,yy,xx,yy+4*k,xx+8*k,yy,color);
    if(n!=step || lost)avatar_draw(n%2?8592415008ull:65516340544ull,4,trap?POSE_LAY:passed?POSE_DANCE:next?POSE_WAVE:POSE_STD,(int)(now_ms()/350)%2,0,4,0,false,xx-ANCHOR_X*k,yy-ANCHOR_Y*k,k,255);
  }
  Ent *me=world_ent(W.me);
  if(me)avatar_draw(me->look,2,POSE_STD,0,0,2,0,false,ax+(bx-ax)*step/steps-ANCHOR_X*k,ay+(by-ay)*step/steps-ANCHOR_Y*k,k,255);
  char line[80];snprintf(line,sizeof line,TR("Step %d/%d%s"),step,steps,lost?" / lost":done?" / paid":"");
  client_ui_label(line,x+6,y+3,w-12,0xffd84a);
}
static void table(float x,float y,float w) {
  char line[128];
  if(C.kind==BLACKJACK || C.kind==POKER) {
    client_ui_label(TR("Dealer"),x+8,y+58,65,0x18272a);cards(json_object_get(C.state,"dealer"),2,x+78,y+54,w-86);
    if(C.kind==POKER){client_ui_label(TR("Table"),x+8,y+102,65,0x18272a);cards(json_object_get(C.state,"board"),5,x+78,y+98,w-86);}
    else {snprintf(line,sizeof line,TR("Dealer %d / You %d"),num(C.state,"dealerTotal"),num(C.state,"total"));client_ui_label(line,x+8,y+105,w-16,0x18272a);}
    json_t *hands=json_object_get(C.state,"hands");
    if(C.kind==BLACKJACK && json_array_size(hands)>1) {
      for(int i=0;i<2;i++) {
        json_t *side=json_array_get(hands,i);
        snprintf(line,sizeof line,TR("Hand %d: %d"),i+1,num(side,"total"));
        client_ui_label(line,x+8+i*w/2,y+127,w/2-16,playing()&&num(C.state,"activeHand")==i?0x825300:0x18272a);
        cards(json_object_get(side,"player"),2,x+8+i*w/2,y+143,w/2-16);
      }
    } else { client_ui_label(TR("You"),x+8,y+146,65,0x18272a);cards(json_object_get(C.state,"player"),2,x+78,y+142,w-86); }
  } else if(C.kind==VIDEO) {
    cards(json_object_get(C.state,"cards"),5,x+12,y+62,w-24);
    float pitch=(w-16)/5;
    for(int i=0;i<5;i++){snprintf(line,sizeof line,C.held&(1<<i)?TR("Held %d"):TR("Hold %d"),i+1);client_ui_button(x+8+i*pitch,y+112,pitch-3,26,line,ready()&&playing()?HOLD:0,i);}
    client_ui_label(str(C.state,"hand"),x+10,y+150,w-20,0x18272a);
  } else if(C.kind==SLOT) {
    int cols=C.slot==1?3:C.slot==3?6:5,rows=C.slot==3?5:3;float cw=(w-20)/cols,ch=124.0f/rows;
    json_t *grid=json_object_get(C.state,"grid");
    json_t *stages=json_object_get(C.state,C.slot==3?"steps":"respins");size_t stages_n=json_array_size(stages);
    if(stages_n){json_t *last=json_object_get(json_array_get(stages,stages_n-1),"grid");if(json_is_array(last))grid=last;}
    for(int c=0;c<cols;c++)for(int r=0;r<rows;r++) {
      json_t *v=json_array_get(json_array_get(grid,c),r);if(!json_is_integer(v))continue;
      int n=(int)json_integer_value(v);if(n==255)continue;
      char key[64];snprintf(key,sizeof key,"casino-%s-%d",paths[C.slot],n>=100?100:n);
      float xx=x+10+c*cw,yy=y+57+r*ch;
      fill_rect(xx,yy,cw-2,ch-2,0xffd2e7ee);
      if(!sprite(key,xx,yy,cw-2,ch-2)){
        static const char *ranks[]={"10","J","Q","K","A"};
        if(C.slot==0 && n>=0 && n<5)snprintf(line,sizeof line,"%s",ranks[n]);else snprintf(line,sizeof line,"%d",n);
        client_ui_label(line,xx+2,yy+2,cw-4,0x18272a);
      }
      if(n>=100){snprintf(line,sizeof line,TR("%dx"),n-100);client_ui_label(line,xx+2,yy+ch-16,cw-4,0x18272a);}
    }
    if(!grid)client_ui_label(TR("Spin to see the result here."),x+10,y+110,w-20,0x18272a);
  } else if(C.kind==CRASH) {
    snprintf(line,sizeof line,TR("%s / %.2fx"),phase(),multiplier());client_ui_label(line,x+10,y+60,w-20,0x18272a);
    float chart_w=w-24,chart_h=58,base=y+143;
    draw_line(x+12,base-chart_h,x+12,base,0xff615b4d);
    draw_line(x+12,base,x+12+chart_w,base,0xff615b4d);
    double end=fmax(1,multiplier()),span=fmax(2,end);
    for(int i=1;i<=40;i++){
      float y0=base-(float)((pow(end,(i-1)/40.0)-1)/(span-1))*chart_h;
      float y1=base-(float)((pow(end,i/40.0)-1)/(span-1))*chart_h;
      draw_line(x+12+chart_w*(i-1)/40,y0,x+12+chart_w*i/40,y1,!strcmp(phase(),"crashed")?0xff2d27c1:0xff226b36);
    }
    client_ui_label(TR("Estimated multiplier; hotel confirms cash-out."),x+10,y+148,w-20,0x18272a);
    snprintf(line,sizeof line,TR("%zu players / %s"),json_array_size(json_object_get(C.round,"bets")),json_is_number(json_object_get(mine(),"out"))?"cashed out":mine()?"bet placed":"no bet");client_ui_label(line,x+10,y+168,w-20,0x18272a);
  } else if(C.kind==ROULETTE) {
    static const int red[]={1,3,5,7,9,12,14,16,18,19,21,23,25,27,30,32,34,36};
    float cw=(w-42)/12;
    for(int n=0;n<=36;n++) {
      bool is_red=false;for(int i=0;i<COUNT(red);i++)is_red|=red[i]==n;
      float xx=n?x+32+(n-1)/3*cw:x+8,yy=n?y+59+(n-1)%3*29:y+59;
      float ww=n?cw-1:22,hh=n?27:85;
      fill_rect(xx,yy,ww,hh,n?(is_red?0xff26299c:0xff222222):0xff366b22);
      bool focus=client_ui_hot(xx,yy,ww,hh,ready()?NUMBER:0,n);
      if(focus || (C.choice==0 && C.value==n)){
        fill_rect(xx,yy,ww,2,0xff4ad8ff);fill_rect(xx,yy+hh-2,ww,2,0xff4ad8ff);
      }
      snprintf(line,sizeof line,"%d",n);text_draw(line,xx+ww/2,yy+(hh-10)/2,10,0xffffffff,1);
    }
    snprintf(line,sizeof line,C.state?TR("Number %d / paid %d credits"):TR("Place a bet to see the result."),num(C.state,"n"),num(C.state,"win"));
    client_ui_label(line,x+10,y+161,w-20,0x18272a);
  } else dash_hall(x+8,y+54,w-16);
}
void client_casino_draw(float x,float y,float w) {
  char line[200];bool ok=ready(),live=playing();
  float frame_x=x,frame_w=w;
  if(C.confirm) {
    client_ui_paragraph(TR("Leave this cabinet? Dash pays the reached value. An uncashed Crash bet can still lose. Other unfinished hands remain at the hotel until you return or disconnect."),x+10,y+38,w-20,7,0x18272a);
    client_ui_button(x+8,y+183,142,27,TR("Keep playing"),LEAVE,0);client_ui_button(x+156,y+183,w-164,27,TR("Leave game"),CONFIRM_LEAVE,0);return;
  }
  if(C.kind==NO_GAME) {
    client_ui_paragraph(C.status,x+10,y+45,w-20,6,0x18272a);
    client_ui_button(x+8,y+183,142,27,TR("Reload"),net_online()&&!C.pending?RELOAD:0,0);
    client_ui_button(x+156,y+183,w-164,27,TR("Leave game"),LEAVE,0);return;
  }
  for(int i=0;i<3;i++)client_ui_button(x+8+i*99,y+28,95,23,i==0?TR("Play"):i==1?TR("Table"):TR("Rules"),TAB,i);
  if(C.tab==2) {
    size_t offset=0;for(int i=0;i<C.page;i++)offset+=client_ui_paragraph(rules[C.kind]+offset,-1000,-1000,w-20,7,0x18272a);
    size_t count=client_ui_paragraph(rules[C.kind]+offset,x+10,y+61,w-20,7,0x18272a);
    client_ui_button(x+8,y+183,142,27,TR("Previous"),C.page?RULE_PAGE:0,-1);
    client_ui_button(x+156,y+183,w-164,27,TR("Next"),rules[C.kind][offset+count]?RULE_PAGE:0,1);return;
  }
  if(C.tab==1)table(x,y,w);
  else {
    float table_w=floorf(w*.52f);
    fill_rect(x+8,y+54,table_w-12,132,0xffd2e7ee);
    table(x,y,table_w);
    x+=table_w;w-=table_w;
    float stake_pitch=(w-16)/6;
    for(int i=0;i<C.count;i++){
      float xx=x+8+i*stake_pitch,bw=stake_pitch-2;
      snprintf(line,sizeof line,"%d",C.bets[i]);
      client_ui_button(xx,y+57,bw,23,"",ok&&!live&&!C.free_left?BET:0,i);
      text_draw(line,xx+bw/2,y+63,10,0xff2a2718,1);
      if(C.bet==C.bets[i])fill_rect(xx+4,y+76,bw-8,2,0xff008ac7);
    }
    if(C.kind==SLOT || C.kind==DASH || C.kind==ROULETTE) {
      client_ui_button(x+8,y+86,40,23,"<",ok&&!live&&!C.free_left?CHOICE:0,-1);
      client_ui_label(C.kind==SLOT?slots[C.slot]:C.kind==DASH?risks[live?num(C.state,"risk")%4:C.risk]:choices[C.choice],x+54,y+91,w-108,0x18272a);
      client_ui_button(x+w-48,y+86,40,23,">",ok&&!live&&!C.free_left?CHOICE:0,1);
    } else {snprintf(line,sizeof line,TR("Balance: %u hotel credits"),W.credits);client_ui_label(line,x+10,y+91,w-20,0x18272a);}
    if(C.kind==ROULETTE && (C.choice==0 || C.choice==7)) {
      client_ui_button(x+8,y+114,40,23,"-",ok?VALUE:0,-1);snprintf(line,sizeof line,C.choice==7?TR("Dozen %d"):TR("Number %d"),C.value+(C.choice==7));client_ui_label(line,x+54,y+119,w-108,0x18272a);client_ui_button(x+w-48,y+114,40,23,"+",ok?VALUE:0,1);
      if(C.status[0])snprintf(line,sizeof line,"%s",C.status);
      else snprintf(line,sizeof line,C.state?TR("Last number %d / paid %d"):TR("Choose a stake in hotel credits."),num(C.state,"n"),num(C.state,"win"));
      client_ui_label(line,x+10,y+141,w-20,0x18272a);
    } else {
      if(C.status[0])snprintf(line,sizeof line,"%s",C.status);
      else if(C.kind==BLACKJACK && live)snprintf(line,sizeof line,TR("Your total: %d."),num(C.state,"total"));
      else if(C.kind==POKER && live)snprintf(line,sizeof line,TR("Call costs %d credits."),num(C.state,"ante")*2);
      else if(C.kind==VIDEO && live)snprintf(line,sizeof line,"%s",TR("Hold cards on the left, then draw."));
      else if(C.kind==DASH && live)snprintf(line,sizeof line,TR("Step %d/%d. Cash out %d. Next %d."),num(C.state,"step"),num(C.state,"steps"),num(C.state,"cashout"),num(C.state,"next"));
      else if(C.kind==CRASH)snprintf(line,sizeof line,TR("%s / %.2fx. %s"),phase(),multiplier(),json_is_number(json_object_get(mine(),"out"))?"Cashed out.":mine()?"Bet placed.":"Bet before it starts.");
      else if(C.state && C.kind==SLOT)snprintf(line,sizeof line,TR("Win: %d credits. Free spins: %d."),num(C.state,"win"),C.free_left);
      else if(C.state && C.kind==DASH && json_is_true(json_object_get(C.state,"lost")))snprintf(line,sizeof line,TR("Lost on step %d. Stake: %d credits."),num(C.state,"step")+1,num(C.state,"stake"));
      else if(C.state && C.kind==DASH)snprintf(line,sizeof line,TR("Cashed out: %d credits."),num(C.state,"win"));
      else if(C.state && C.kind==ROULETTE)snprintf(line,sizeof line,TR("Number %d. Paid: %d credits."),num(C.state,"n"),num(C.state,"win"));
      else if(C.state)snprintf(line,sizeof line,TR("%s. Paid: %d credits."),str(C.state,C.kind==VIDEO?"hand":"result"),num(C.state,"win"));
      else snprintf(line,sizeof line,"%s",TR("Choose a stake in hotel credits."));
      client_ui_paragraph(line,x+10,y+115,w-20,3,0x18272a);
    }
    const char *primary=C.kind==SLOT?(C.free_left?"Free spin":"Spin"):C.kind==ROULETTE || C.kind==CRASH?"Place bet":C.kind==DASH?(live?"Step":"Start"):C.kind==VIDEO?(live?"Draw":"Deal"):live?(C.kind==POKER?"Call":"Hit"):C.kind==BLACKJACK&&C.state?"Deal again":"Deal";
    float action_pitch=(w-16)/(C.kind==BLACKJACK?4:2);
    client_ui_button(x+8,y+161,action_pitch-4,26,primary,can_play()?PLAY:0,0);
    if(C.kind==BLACKJACK || C.kind==POKER || C.kind==CRASH || C.kind==DASH)client_ui_button(x+8+action_pitch,y+161,action_pitch-4,26,C.kind==BLACKJACK?TR("Stand"):C.kind==POKER?TR("Fold"):TR("Cash out"),can_second()?SECOND:0,0);
    if(C.kind==BLACKJACK)client_ui_button(x+8+action_pitch*2,y+161,action_pitch-4,26,TR("Double"),ok&&live&&json_array_size(json_object_get(C.state,"player"))==2&&W.credits>=(u32)num(C.state,"bet")?DOUBLE:0,0);
    if(C.kind==BLACKJACK)client_ui_button(x+8+action_pitch*3,y+161,action_pitch-4,26,TR("Split"),ok&&live&&json_is_true(json_object_get(C.state,"canSplit"))&&W.credits>=(u32)num(C.state,"bet")?SPLIT:0,0);
  }
  x=frame_x;w=frame_w;
  float footer_width=(w-24)/2;
  client_ui_button(x+8,y+193,footer_width,23,TR("Reload state"),net_online()&&!C.pending?RELOAD:0,0);
  client_ui_button(x+16+footer_width,y+193,footer_width,23,TR("Leave game"),LEAVE,0);
}
