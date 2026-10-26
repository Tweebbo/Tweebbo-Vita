#include "language.h"
#include "client_fastfood.h"
#include "client_ui.h"
#include "fastfood.h"
#include "net.h"
#include "show.h"
#include "gfx.h"
#include "game_audio.h"
#include <stdio.h>
#include <string.h>
enum { PRACTICE=15000,ONLINE,DROP,BIG,MISSILE,SHIELD,MENU,CREATE,JOIN,START,LEAVE,REFRESH,PAGE,SOUND };
static int page;
static const char *str(json_t *j,const char *key){const char *s=json_string_value(json_object_get(j,key));return s?s:"";}
static int num(json_t *j,const char *key){return json_integer_value(json_object_get(j,key));}
static json_t *state(void){return json_object_get(fastfood_snapshot(),"state");}
static json_t *player(void){return json_array_get(json_object_get(state(),"players"),num(fastfood_snapshot(),"slot"));}
bool client_fastfood_playing(void){return fastfood_active()&&json_is_object(state())&&!fastfood_error()[0];}
void client_fastfood_action(int command,int argument){
  char message[80];
  if(command==PRACTICE)fastfood_practice(argument);
  else if(command==ONLINE)fastfood_online();
  else if(command==DROP)fastfood_action(json_is_object(json_object_get(player(),"plate"))?"parachute":"drop");
  else if(command==BIG)fastfood_action("big");
  else if(command==MISSILE)fastfood_action("missile");
  else if(command==SHIELD)fastfood_action("shield");
  else if(command==MENU)fastfood_menu();
  else if(command==CREATE){snprintf(message,sizeof message,"{\"op\":\"create\",\"goal\":%d}",argument);fastfood_command(message);}
  else if(command==JOIN){snprintf(message,sizeof message,"{\"op\":\"join\",\"game\":%d}",argument);fastfood_command(message);}
  else if(command==START)fastfood_command("{\"op\":\"start\"}");
  else if(command==LEAVE)fastfood_command("{\"op\":\"leave\"}");
  else if(command==REFRESH)fastfood_command("{\"op\":\"list\"}");
  else if(command==PAGE)page=argument;
  else if(command==SOUND)game_audio_toggle();
  client_ui_changed();
}
bool client_fastfood_key(int key){
  if(!client_fastfood_playing())return false;
  int cmd=key==SHOW_A?DROP:key==SHOW_X?BIG:key==SHOW_Y?MISSILE:key==SHOW_R?SHIELD:key==SHOW_B?MENU:0;
  if(cmd){client_fastfood_action(cmd,0);return true;}return false;
}
void client_fastfood_hud(void){
  float width=(SCREEN_W-28)/6.f,y=SCREEN_H-30;
  const char *labels[]={json_is_object(json_object_get(player(),"plate"))?"Chute":"Drop","Big chute","Missile","Shield",game_audio_muted()?"Unmute":"Mute","Back"};
#if TWEEBBO_DEVICE==3
  const char *keys[]={"Cross","Square","Triangle","R",NULL,"Circle"};
#else
  const char *keys[]={"A","X","Y","R",NULL,"B"};
#endif
  const int commands[]={DROP,BIG,MISSILE,SHIELD,SOUND,MENU};bool playing=!strcmp(str(state(),"phase"),"playing");
  for(int i=0;i<6;i++){client_ui_button(4+i*(width+4),y,width,26,TR(labels[i]),playing||i>=4?commands[i]:0,0);if(keys[i])client_ui_key_hint(keys[i],"",8+i*(width+4),y-18,0xffffff,0xffffff);}
}
void client_fastfood_draw(float x,float y,float width){
  if(fastfood_error()[0]){client_ui_paragraph(fastfood_error(),x+10,y+34,width-20,5,0x18272a);return;}
  json_t *root=fastfood_snapshot(),*lobby=json_object_get(root,"lobby");
  if(json_is_object(lobby)){
    json_t *games=json_object_get(lobby,"games");int current=num(lobby,"current");
    if(current){client_ui_label(TR("Waiting for players"),x+10,y+34,width-20,0x18272a);client_ui_button(x+8,y+70,width/2-12,26,TR("Start"),START,0);client_ui_button(x+width/2+4,y+70,width/2-12,26,TR("Leave game"),LEAVE,0);}
    else{
      int pages=((int)json_array_size(games)+1)/2;if(page>=pages||page<0)page=0;
      for(int i=0;i<2;i++){json_t *g=json_array_get(games,page*2+i);if(!g)break;char label[90];snprintf(label,sizeof label,TR("Game %d | %u players"),num(g,"id"),(unsigned)json_array_size(json_object_get(g,"players")));client_ui_button(x+8,y+32+i*29,width-16,26,label,json_is_true(json_object_get(g,"running"))?0:JOIN,num(g,"id"));}
      if(pages>1){client_ui_button(x+8,y+92,width/2-12,26,TR("Previous"),page>0?PAGE:0,page-1);client_ui_button(x+width/2+4,y+92,width/2-12,26,TR("Next"),page+1<pages?PAGE:0,page+1);}
      if(!json_array_size(games))client_ui_label(TR("No games yet. Create one below."),x+10,y+40,width-20,0x18272a);
      for(int i=0;i<3;i++){char label[24];snprintf(label,sizeof label,TR("Create %d"),6+i*6);client_ui_button(x+8+i*(width-16)/3,y+130,(width-22)/3,26,label,CREATE,6+i*6);}
    }
    client_ui_label(str(root,"error"),x+10,y+164,width-20,0x18272a);
    client_ui_button(x+8,y+188,width/2-12,26,TR("Refresh"),REFRESH,0);client_ui_button(x+width/2+4,y+188,width/2-12,26,TR("Practice"),PRACTICE,12);return;
  }
  if(json_is_true(json_object_get(root,"online"))){
    client_ui_paragraph(str(root,"error")[0]?str(root,"error"):TR("Connecting to Fast Food..."),x+10,y+34,width-20,5,0x18272a);
    client_ui_button(x+8,y+130,width-16,26,TR("Retry"),net_online()?ONLINE:0,0);
    client_ui_button(x+8,y+169,width-16,28,TR("Practice"),PRACTICE,12);return;
  }
  client_ui_paragraph(TR("Land your dishes gently. Choose a menu to practise against computer players."),x+10,y+34,width-20,4,0x18272a);
  for(int i=0;i<3;i++){char label[24];snprintf(label,sizeof label,TR("%d dishes"),6+i*6);client_ui_button(x+8+i*(width-16)/3,y+125,(width-22)/3,26,label,PRACTICE,6+i*6);}
  client_ui_button(x+8,y+169,width-16,28,TR("Play online"),net_online()?ONLINE:0,0);
}
