#include "fastfood.h"
#include "battle_panel.h"
#include "client_fastfood.h"
#include "client_battle.h"
#include "client_snow.h"
#include "language.h"
#include <malloc.h>
#include <psp2/kernel/sysmem.h>

static void games_memory(const char *where) {
  struct mallinfo m=mallinfo();
  dbg_log("games memory %s: heap_used=%u heap_free=%u",where,(unsigned)m.uordblks,(unsigned)m.fordblks);
  SceKernelFreeMemorySizeInfo free_memory={.size=sizeof free_memory};
  if(sceKernelGetFreeMemorySize(&free_memory)==0)
    dbg_log("games memory %s: user_free=%u cdram_free=%u",where,(unsigned)free_memory.size_user,(unsigned)free_memory.size_cdram);
}

static void games_exercise(u64 now) {
  static int step,cycle,arena=1;
  static u64 at,start;
  if(step==99||!W.in_room)return;
  if(!start){start=at=now;games_memory("hotel");}
  if(now-start>180000){dbg_log("FAIL: games exercise timed out at step %d",step);step=99;return;}
  if(fastfood_error()[0]||battle_error()[0]){
    dbg_log("FAIL: games exercise: %s %s",fastfood_error(),battle_error());step=99;return;
  }
  json_t *state=json_object_get(fastfood_snapshot(),"state");
  const char *phase=json_string_value(json_object_get(state,"phase"));
  switch(step){
  case 0:if(now-at<3000)return;client_ui_fastfood();step=1;at=now;break;
  case 1:if(now-at<2000)return;client_fastfood_action(15000,6+cycle*6);step=2;at=now;break;
  case 2:
    if(now-at<4500||!phase||strcmp(phase,"playing"))return;
    client_ui_action(SHOW_A);step=3;at=now;break;
  case 3:
    if(now-at<300)return;
    client_ui_action(SHOW_X);client_ui_action(SHOW_R);client_ui_action(SHOW_Y);
    games_memory("fastfood");dbg_log("SHOT: food-%d",cycle);step=4;at=now;break;
  case 4:
    if(now-at<4000)return;client_ui_close();step=5;at=now;break;
  case 5:
    if(now-at<2000)return;games_memory("after-food");
    if(++cycle<3){step=0;at=now;}else{client_ui_battle();step=6;at=now;}break;
  case 6:
    if(now-at<2000)return;
    for(int i=0;i<arena%5;i++)client_battle_action(BATTLE_ARENA,0);
    client_battle_action(BATTLE_PRACTICE,0);step=7;at=now;break;
  case 7:
    if(now-at<4500||!battle_playing())return;
    battle_move(1,0);client_ui_action(SHOW_A);games_memory("battle");
    dbg_log("SHOT: battle-%d",arena);step=8;at=now;break;
  case 8:
    if(now-at<4000)return;client_ui_close();step=9;at=now;break;
  case 9:
    if(now-at<2000)return;games_memory("after-battle");
    if(++arena<=5){client_ui_battle();step=6;at=now;}else{client_ui_snow();step=10;at=now;}break;
  case 10:if(now-at<3000)return;client_snow_action(7003,0);step=11;at=now;break;
  case 11:
    if(now-at<6000||!client_snow_playing())return;
    client_snow_move(1,0);client_ui_action(SHOW_X);client_ui_action(SHOW_A);
    games_memory("snow");dbg_log("SHOT: snow");step=12;at=now;break;
  case 12:if(now-at<5000)return;client_snow_action(7009,0);step=13;at=now;break;
  case 13:
    if(now-at<3000)return;games_memory("after-snow");
    tw_language_request(TW_ZH);step=14;at=now;break;
  case 14:
    if(now-at<4000)return;
    if(tw_language()!=TW_ZH||tw_language_error()[0]){dbg_log("FAIL: Vita Chinese font replacement");step=99;break;}
    dbg_log("FONT: zh");tw_language_request(TW_EN);step=15;at=now;break;
  case 15:
    if(now-at<4000)return;
    if(tw_language()!=TW_EN||tw_language_error()[0]){dbg_log("FAIL: Vita Latin font replacement");step=99;break;}
    dbg_log("FONT: en");tw_language_request(TW_JA);step=16;at=now;break;
  case 16:
    if(now-at<4000)return;
    if(tw_language()!=TW_JA||tw_language_error()[0]){dbg_log("FAIL: Vita Japanese font replacement");step=99;break;}
    games_memory("after-font-cycle");
    dbg_log("PASS: Vita font replacement zh/en/ja between frames");
    dbg_log("PASS: Vita backend exercised Fast Food 6/12/18, five Battle Ball arenas, Snow Storm and returns");
    step=99;break;
  }
}
