#include "../source/client_casino.c"
#include <assert.h>
World W;
static bool online=true;
static u64 clock_ms=1000;
static int sends,op,a,b,c,opened;
static FurniDef cabinet={.id=60000,.casino=1};
u64 now_ms(void){return clock_ms;}
bool net_online(void){return online;}
Item *world_item(u32 id){return id==W.items[0].id && W.items[0].used?&W.items[0]:NULL;}
const FurniDef *def_get(u16 id){return id==60000?&cabinet:NULL;}
void send_use(u32 item){assert(item==W.items[0].id);opened++;}
void send_casino(int oo,int aa,int bb,int cc){sends++;op=oo;a=aa;b=bb;c=cc;}
void client_ui_casino(void){}
void client_ui_close(void){}
void client_ui_changed(void){}
void gfx_furni_preload(u16 def){assert(def==60007 || def==1114 || def==1098 || def==1107);}
void show_toast(const char *text,u64 now){(void)text;(void)now;}
static void open_game(int kind,const char *extra) {
  client_casino_reset();client_casino_open(1);assert(C.pending==1);
  char json[2048];snprintf(json,sizeof json,"{\"t\":\"open\",\"mode\":\"free\",\"item\":1,\"game\":\"%s\",\"bets\":[1,2,5,10,25,50]%s}",keys[kind],extra?extra:"");
  client_casino_receive(json);assert(C.kind==kind && !C.pending && !C.uncertain);
}
int main(void) {
  W.in_room=true;W.me=10;W.credits=5000;strcpy(W.slug,"casino");W.items[0]=(Item){.used=true,.id=1,.def=60000};
  for(int k=0;k<NO_GAME;k++) {
    open_game(k,k==CRASH?",\"round\":{\"phase\":\"idle\"}":NULL);
    client_casino_action(BET,2);assert(C.bet==5);
    int before=sends;client_casino_action(PLAY,0);assert(sends==before+1);
    client_casino_action(PLAY,0);assert(sends==before+1);
    static const int operations[]={2,3,4,8,11,14,16};assert(op==operations[k]);
    client_casino_receive("{\"t\":\"unrelated\"}");assert(C.pending);
    clock_ms+=13000;client_casino_update();assert(C.uncertain && !C.pending);
    client_casino_action(PLAY,0);assert(sends==before+1);
    client_casino_action(RELOAD,0);assert(C.pending==1);
  }
  open_game(SLOT,",\"freeLeft\":3,\"freeGame\":4");
  W.credits=0;client_casino_action(BET,4);client_casino_action(CHOICE,1);assert(C.bet==1 && C.slot==4);
  client_casino_action(PLAY,0);assert(op==2 && b==4);
  client_casino_receive("{\"t\":\"slot\",\"game\":4,\"freeLeft\":2,\"win\":5,\"grid\":[[1,2,3]]}");assert(!C.pending && C.free_left==2);
  W.credits=5000;
  open_game(BLACKJACK,",\"hand\":{\"bet\":10,\"total\":12,\"player\":[4,40],\"dealer\":[5,-1]}");
  client_casino_action(DOUBLE,0);assert(op==7);client_casino_receive("{\"t\":\"bj\",\"hand\":{\"result\":\"win\",\"win\":40}}");assert(!playing());
  open_game(BLACKJACK,",\"hand\":{\"bet\":10,\"total\":16,\"player\":[28,29],\"dealer\":[5,-1],\"canSplit\":true}");
  client_casino_action(SPLIT,0);assert(op==20 && C.pending==20);
  client_casino_receive("{\"t\":\"bj\",\"hand\":{\"bet\":10,\"total\":18,\"player\":[28,36],\"dealer\":[5,-1],\"canSplit\":false,\"activeHand\":0,\"hands\":[{\"player\":[28,36],\"total\":18,\"bet\":10},{\"player\":[29,40],\"total\":18,\"bet\":10}]}}");
  assert(!C.pending && playing() && json_array_size(json_object_get(C.state,"hands"))==2);
  int split_sends=sends;client_casino_action(SPLIT,0);assert(sends==split_sends);
  open_game(POKER,",\"poker\":{\"ante\":10,\"player\":[0,1]}");
  W.credits=19;int before=sends;client_casino_action(PLAY,0);assert(sends==before);
  client_casino_action(SECOND,0);assert(op==13);W.credits=5000;
  open_game(VIDEO,",\"video\":{\"cards\":[0,1,2,3,4]}");
  client_casino_action(HOLD,0);client_casino_action(HOLD,4);client_casino_action(HOLD,7);client_casino_action(PLAY,0);assert(op==15 && a==17);
  client_casino_receive("{\"t\":\"vp\",\"cards\":[0,1,2,3,4],\"done\":true,\"win\":25}");assert(!playing());
  open_game(CRASH,",\"round\":{\"phase\":\"betting\",\"startsIn\":3000,\"bets\":[]}");
  client_casino_action(PLAY,0);assert(op==8);
  client_casino_receive("{\"t\":\"crash\",\"item\":1,\"round\":{\"phase\":\"betting\",\"startsIn\":3000,\"bets\":[]}}");assert(C.pending==8);
  client_casino_receive("{\"t\":\"crash\",\"item\":1,\"round\":{\"phase\":\"running\",\"bets\":[{\"eid\":10,\"bet\":1}]}}");assert(!C.pending);
  client_casino_action(SECOND,0);assert(op==9 && C.pending==9);
  client_casino_receive("{\"t\":\"crash\",\"item\":1,\"round\":{\"phase\":\"running\",\"bets\":[{\"eid\":10,\"bet\":1}]}}");assert(C.pending==9);
  client_casino_receive("{\"t\":\"crash\",\"item\":1,\"round\":{\"phase\":\"running\",\"bets\":[{\"eid\":10,\"out\":1.1}]}}");assert(!C.pending);
  client_casino_reset();client_casino_open(1);
  client_casino_receive("{\"t\":\"open\",\"mode\":\"free\",\"item\":1,\"game\":\"dash\",\"bets\":[50,100,200,500,1000,2500]}");
  client_casino_action(BET,4);client_casino_action(PLAY,0);assert(op==16 && a==0 && b==4);
  client_casino_receive("{\"t\":\"dash\",\"step\":1,\"steps\":8,\"cashout\":1190}");client_casino_action(SECOND,0);assert(op==18);
  open_game(ROULETTE,NULL);for(int i=0;i<7;i++)client_casino_action(CHOICE,1);
  client_casino_action(VALUE,-1);client_casino_action(PLAY,0);assert(op==3 && a==7 && b==2 && c==1);
  client_casino_receive("{\"t\":\"refused\",\"message\":\"Please wait\"}");assert(C.uncertain && !C.pending);
  open_game(SLOT,NULL);online=false;client_casino_update();online=true;before=sends;client_casino_action(PLAY,0);assert(sends==before && C.uncertain);
  client_casino_action(RELOAD,0);client_casino_receive("{");assert(C.uncertain);
  client_casino_reset();client_casino_open(1);client_casino_receive("{\"t\":\"open\",\"mode\":\"paid\",\"item\":1,\"game\":\"slot\",\"bets\":[1]}");assert(C.uncertain);
  client_casino_action(CONFIRM_LEAVE,0);assert(C.item);client_casino_action(LEAVE,0);client_casino_action(CONFIRM_LEAVE,0);assert(!C.item && op==10);
  client_casino_receive("{\"t\":\"open\"}");assert(!C.item);
  W.items[0].id=4000000001u;client_casino_open(W.items[0].id);
  client_casino_receive("{\"t\":\"open\",\"mode\":\"free\",\"item\":4000000001,\"game\":\"slot\",\"bets\":[1]}");assert(C.kind==SLOT && !C.pending);
  client_casino_reset();
  puts("OK: all seven casino games, duplicate guards, uncertain replies/reload, free spins, poker funds, held cards, Crash acknowledgements, immediate cash-out, Dash stake indices, roulette and leave confirmation.");
}
