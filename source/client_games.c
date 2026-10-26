#include "language.h"
#include "client_games.h"
#include "client_ui.h"
#include "client_social.h"
#include "net.h"
#include "show.h"
#include "world.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

enum { KIND=3000, PAGE, CHALLENGE, ACCEPT, DECLINE, CELL, PROMOTION, RESIGN, CONFIRM, CANCEL, DISMISS, CENTER, FOOD, SNOW, BATTLE_LOBBY, SNOW_LOBBY, BATTLE };
static bool center=true;
static struct {int kind,page,picked,promotion;u32 revision,duel,invitation;bool pending,confirm,dismissed;} D;
static const char *name(int kind) {return kind==1?"Tic-tac-toe":kind==2?"Connect Four":"Chess";}
static const char *player(u32 id) {Ent *e=world_ent(id);return e?e->name:"Player";}
static bool legal(int from,int to) {
  for(int i=0;i<W.duel.move_count;i++)if(W.duel.moves[i][0]==from && W.duel.moves[i][1]==to)return true;
  return false;
}
static bool selectable(int cell) {for(int i=0;i<W.duel.move_count;i++)if(W.duel.moves[i][0]==cell)return true;return false;}
static bool my_turn(void) {return net_online() && W.duel.id && !W.duel.result && W.duel.turn<2 && W.duel.players[W.duel.turn]==W.me && !D.pending;}
void client_games_reset(void) {memset(&D,0,sizeof D);D.kind=1;D.picked=-1;D.promotion=5;}
void client_games_update(bool visible) {
  if(D.revision==W.game_rev)return;
  D.revision=W.game_rev;D.pending=false;D.picked=-1;
  if(D.duel!=W.duel.id) {D.duel=W.duel.id;D.confirm=false;D.dismissed=false;}
  for(int i=0;i<8;i++)if(W.invites[i].from) {
    Ent *e=world_ent(W.invites[i].from);
    if(e && client_social_blocked(e->guest)) {send_game(2,e->id,0,0,0);continue;}
    if(D.invitation!=W.invites[i].from) {D.invitation=W.invites[i].from;show_toast("Game invitation received. Open More > Games.",now_ms());}
    break;
  }
  if(W.duel.id && !visible)show_toast(W.duel.result?"Game ended. Open More > Games.":"Game updated. Open More > Games.",now_ms());
  if(visible)client_ui_changed();
}
void client_games_action(int command,int arg) {
  if(command==CENTER){center=arg==0;client_ui_changed();return;}
  if(command==FOOD){client_ui_fastfood();return;}
  if(command==SNOW){client_ui_snow();return;}
  if(command==BATTLE){client_ui_battle();return;}
  if(command==BATTLE_LOBBY||command==SNOW_LOBBY){net_goto(command==BATTLE_LOBBY?"battleball":"snowstorm","");client_ui_close();return;}

  if(command==KIND){D.kind=arg;client_ui_changed();return;}
  if(command==PAGE){D.page+=arg;client_ui_changed();return;}
  if(command==CANCEL){D.confirm=false;client_ui_changed();return;}
  if(command==DISMISS && W.duel.result){D.dismissed=true;client_ui_changed();return;}
  if(!net_online() || D.pending)return;
  if(command==CHALLENGE) {
    Ent *e=world_ent((u32)arg);
    if(e && e->id!=W.me && e->guest && e->guest<4001000000u) {send_game(1,e->id,D.kind,0,0);show_toast("Challenge sent. Waiting for acceptance.",now_ms());}
  } else if(command==ACCEPT || command==DECLINE) {
    for(int i=0;i<8;i++)if(W.invites[i].from==(u32)arg) {send_game(2,(u32)arg,command==ACCEPT,0,0);D.pending=true;break;}
  } else if(command==RESIGN && W.duel.id && !W.duel.result) {D.confirm=true;client_ui_changed();}
  else if(command==CONFIRM && D.confirm && W.duel.id && !W.duel.result) {send_game(4,W.duel.id,0,0,0);D.pending=true;}
  else if(command==PROMOTION) {
    static const int values[]={5,4,3,2};for(int i=0;i<4;i++)if(D.promotion==values[i]){D.promotion=values[(i+1)%4];break;}
  } else if(command==CELL && my_turn()) {
    int from=W.duel.kind==3?D.picked:arg,to=W.duel.kind==3?arg:0;
    if(from>=0 && legal(from,to)) {send_game(3,W.duel.id,from,to,D.promotion);D.pending=true;D.picked=-1;}
    else if(W.duel.kind==3) {D.picked=selectable(arg)?arg:-1;client_ui_changed();}
  }
}
void client_games_draw(float x,float y,float w) {
  char text[160];
  for(int i=0;i<8;i++)if(W.invites[i].from) {
    Ent *e=world_ent(W.invites[i].from);if(e && client_social_blocked(e->guest))continue;
    snprintf(text,sizeof text,TR("%s invites you to play %s."),player(W.invites[i].from),name(W.invites[i].kind));
    client_ui_paragraph(text,x+10,y+38,w-20,4,0x18272a);
    client_ui_button(x+8,y+150,142,28,TR("Decline"),!D.pending?DECLINE:0,(int)W.invites[i].from);
    client_ui_button(x+156,y+150,146,28,TR("Accept"),!D.pending?ACCEPT:0,(int)W.invites[i].from);return;
  }
  Duel *g=&W.duel;
  if(g->id && !D.dismissed) {
    if(D.confirm && !g->result) {
      client_ui_paragraph(TR("Resign from this game? Your opponent will win."),x+10,y+40,w-20,4,0x18272a);
      client_ui_button(x+8,y+150,142,28,TR("Keep playing"),!D.pending?CANCEL:0,0);
      client_ui_button(x+156,y+150,146,28,TR("Resign"),!D.pending?CONFIRM:0,0);return;
    }
    client_ui_label(name(g->kind),x+10,y+29,w-20,0x18272a);
    bool mine=my_turn();int cols=g->kind==1?3:g->kind==2?7:8,rows=g->kind==1?3:g->kind==2?6:8,size=g->kind==1?48:g->kind==2?23:20;
    for(int row=0;row<rows;row++)for(int col=0;col<cols;col++) {
      bool flip=g->kind==3 && g->players[1]==W.me;
      int cell=g->kind==1?row*3+col:g->kind==2?(5-row)*7+col:flip?row*8+7-col:(7-row)*8+col;
      int from=g->kind==2?col:cell;u8 piece=g->board[cell];char label[3]="";
      if(piece && g->kind==3) {static const char white[]=" PNBRQK",black[]=" pnbrqk";if((piece&7)<=6)label[0]=(piece&8?black:white)[piece&7];}
      else if(piece)label[0]=piece==1?'X':'O';
      bool option=mine && (g->kind==3?selectable(cell) || (D.picked>=0 && legal(D.picked,cell)):legal(from,0));
      client_ui_button(x+8+col*size,y+49+row*size,size-1,size-1,label,option?CELL:0,from);
      if(g->kind==3 && D.picked==cell)fill_rect(x+10+col*size,y+50+row*size,size-5,2,0xff4dd3ff);
    }
    const char *state=g->result==3?"Draw":g->result?g->players[g->result-1]==W.me?"You win!":"You lost":D.pending?"Waiting for hotel":mine?g->check?"Check! Your move":"Your move":"Opponent's turn";
    client_ui_paragraph(state,x+180,y+50,w-190,3,0x18272a);
    client_ui_label(player(g->players[0]),x+180,y+105,w-190,0x26373a);
    client_ui_label(player(g->players[1]),x+180,y+123,w-190,0x26373a);
    if(g->kind==3) {
      static const char *promotions[]={"","","Knight","Bishop","Rook","Queen"};
      client_ui_label(TR("Promotion"),x+180,y+147,w-190,0x26373a);
      client_ui_button(x+180,y+164,w-188,23,promotions[D.promotion],PROMOTION,0);
    }
    client_ui_button(x+180,y+191,w-188,23,g->result?TR("Finished"):TR("Resign"),g->result?DISMISS:!D.pending?RESIGN:0,0);return;
  }
  if(center){
    client_ui_button(x+8,y+30,w-16,24,TR("Challenges"),CENTER,1);
    client_ui_button(x+8,y+65,w-16,26,TR("Fast Food"),FOOD,0);
    client_ui_button(x+8,y+96,w-16,26,TR("Snow Storm"),SNOW,0);
    client_ui_button(x+8,y+127,w-16,26,TR("Battle Ball"),BATTLE,0);
    client_ui_button(x+8,y+158,w/2-12,26,TR("Battle lobby"),BATTLE_LOBBY,0);
    client_ui_button(x+w/2+4,y+158,w/2-12,26,TR("Snow lobby"),SNOW_LOBBY,0);return;
  }
  client_ui_button(x+w/2-60,y+188,120,24,TR("Game Center"),CENTER,0);
  for(int i=1;i<=3;i++)client_ui_button(x+8+(i-1)*99,y+30,95,24,name(i),KIND,i);
  fill_rect(x+10+(D.kind-1)*99,y+55,91,2,0xff766d52);
  int entries[MAX_ENTS],n=0;
  for(int i=0;i<MAX_ENTS;i++){Ent *e=&W.ents[i];if(e->used && e->id!=W.me && e->guest && e->guest<4001000000u && !client_social_blocked(e->guest))entries[n++]=i;}
  int pages=(n+3)/4;if(pages<1)pages=1;if(D.page<0)D.page=0;if(D.page>=pages)D.page=pages-1;
  if(!n)client_ui_paragraph(TR("Invite a friend to this room, then select their name to challenge them."),x+10,y+75,w-20,4,0x26373a);
  for(int row=0;row<4 && D.page*4+row<n;row++) {Ent *e=&W.ents[entries[D.page*4+row]];client_ui_button(x+8,y+65+row*28,w-16,26,e->name,CHALLENGE,(int)e->id);}
  client_ui_button(x+8,y+188,80,24,TR("Previous"),D.page?PAGE:0,-1);
  client_ui_button(x+w-88,y+188,80,24,TR("Next"),D.page+1<pages?PAGE:0,1);
}
