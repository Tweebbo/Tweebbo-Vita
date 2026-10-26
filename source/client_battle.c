#include "client_battle.h"
#include "language.h"
#include "client_ui.h"
#include "show.h"
#include "gfx.h"
#include "game_audio.h"
void client_battle_action(int command,int argument){battle_panel_action(command,argument);if(!battle_active())client_ui_close();client_ui_changed();}
bool client_battle_key(int key){
  if(!battle_playing())return false;
  if(key==SHOW_A)client_battle_action(BATTLE_POWER,0);
  else if(key==SHOW_L||key==SHOW_R)battle_zoom(key==SHOW_R?1:-1);
  else if(key==SHOW_X)battle_centre();
  else if(key==SHOW_B)client_battle_action(BATTLE_LEAVE,0);
  return true;
}
bool client_battle_move(int dx,int dy){if(!battle_playing())return false;battle_move(dx+dy,dy-dx);return true;}
bool client_battle_tap(float x,float y){if(!battle_playing())return false;battle_tap(x,y);return true;}
static void label(const char *text,float x,float y,float width){client_ui_label(text,x,y,width,0x18272a);}
void client_battle_draw(float x,float y,float width){battle_panel(x+10,y+34,width-20,client_ui_button,label);}
void client_battle_hud(void){
  char score[128],clock[32];battle_score(score,sizeof score);battle_status(clock,sizeof clock);fill_rect(0,0,SCREEN_W,21,0xffeeeeee);label(score,5,3,SCREEN_W-90);label(clock,SCREEN_W-80,3,75);
  float width=(SCREEN_W-28)/6.f,y=SCREEN_H-30;
  const char *labels[]={battle_power_name(),"Zoom +","Zoom -","Centre",game_audio_muted()?"Unmute":"Mute","Leave"};
  const int cmds[]={BATTLE_POWER,BATTLE_ZOOM,BATTLE_ZOOM,BATTLE_CENTRE,BATTLE_SOUND,BATTLE_LEAVE},args[]={0,1,-1,0,0,0};
  for(int i=0;i<6;i++)client_ui_button(4+i*(width+4),y,width,26,TR(labels[i]),cmds[i],args[i]);
}
