#include "client_furniture.h"
#include "client_ui.h"
#include "furniture.h"
#include "language.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int page;
static u32 revision;
static int reading=-1,read_page;
static size_t read_offsets[64],read_next;
void client_furniture_action(int command,int argument){
  FurniturePanel *p=&furniture_panel;
  switch(command){
    case 17000:if(argument>=0&&argument<p->field_count&&p->fields[argument].editable){p->selected=argument;client_ui_edit(TR(p->fields[argument].label),p->fields[argument].value,furniture_edit);}break;
    case 17001:furniture_save();break;
    case 17002:furniture_apply();break;
    case 17003:page+=argument;if(page<0)page=0;break;
    case 17004:furniture_choice(argument,-1);break;
    case 17005:furniture_choice(argument,1);break;
    case 17006:furniture_target(argument,false);break;
    case 17007:furniture_target(0,true);break;
    case 17008:furniture_throw();break;
    case 17009:if(argument>=0&&argument<p->field_count){reading=argument;read_page=0;read_offsets[0]=0;}break;
    case 17010:reading=-1;break;
    case 17011:if(read_page>0)read_page--;break;
    case 17012:if(read_page<63)read_offsets[++read_page]=read_next;break;
    case 17013:furniture_toggle_target_kind();break;
    case 17014:client_ui_dress();break;
  }
  client_ui_changed();
}
void client_furniture_draw(float x,float y,float w,float h){
  FurniturePanel *p=&furniture_panel;if(revision!=p->revision){revision=p->revision;page=0;reading=-1;}int rows=h<280?2:3;int pages=(furniture_display_count()+rows-1)/rows;if(pages<1)pages=1;if(page>=pages)page=pages-1;
  if(reading>=0&&reading<p->field_count){
    const char *text=p->fields[reading].value;
    read_next=read_offsets[read_page]+client_ui_paragraph(text+read_offsets[read_page],x+12,y+37,w-24,(int)((h-80)/17),0x18272a);
    client_ui_button(x+10,y+h-33,70,26,TR("Back"),17010,0);
    client_ui_button(x+90,y+h-33,70,26,TR("Prev"),read_page>0?17011:0,0);
    client_ui_button(x+w-80,y+h-33,70,26,TR("Next"),text[read_next]&&read_page<63?17012:0,0);
    return;
  }
  float top=y+31,field_h=45;
  int drawn=0;
  for(int r=0;r<rows;r++){
    int i=furniture_display_index(page*rows+r);if(i<0)break;drawn=r+1;FurnitureField *f=&p->fields[i];float fy=top+r*field_h;
    client_ui_label(TR(f->label),x+10,fy,w-20,0x18272a);
    bool choice=furniture_has_choices(i);
    if(p->kind==F_WIRED&&i==0){
      client_ui_label(furniture_operation_label(),x+10,fy+20,w-20,0x18272a);
    }else if(choice){
      client_ui_button(x+10,fy+15,28,24,"<",p->editable?17004:0,i);
      client_ui_label(TR(furniture_choice_label(i)),x+44,fy+20,w-88,0x18272a);
      client_ui_button(x+w-38,fy+15,28,24,">",p->editable?17005:0,i);
    }else{
      bool long_text=strlen(f->value)>32;
      client_ui_field(x+10,fy+15,w-20-(long_text?62:0),24,f->value,TR(f->label),f->editable?17000:0,i);
      if(long_text)client_ui_button(x+w-68,fy+15,58,24,TR("Read"),17009,i);
    }
  }
  if(p->compact_motion&&!furniture_display_count())client_ui_paragraph(TR(furniture_help_text()),x+10,top,w-20,2,0x18272a);
  float controls=top+(p->kind==F_CHANGING||p->kind==F_MANNEQUIN?drawn:rows)*field_h+3;
  if(furniture_target_picker()){
    client_ui_button(x+10,controls,80,24,TR(furniture_target_kind()),furniture_target_switchable()?17013:0,0);
    client_ui_button(x+94,controls,24,24,"<",17006,-1);
    const char *target=furniture_target_label();int check=furniture_target_check();
    if(check>=0)client_ui_check_button(x+124,controls,w-164,24,target,check==1,p->editable?17007:0,0);else client_ui_button(x+124,controls,w-164,24,target,0,0);
    client_ui_button(x+w-34,controls,24,24,">",17006,1);controls+=28;
  }
  if(pages>1){char count[32];snprintf(count,sizeof count,"%d / %d",page+1,pages);client_ui_button(x+10,controls,60,22,TR("Prev"),page?17003:0,-1);client_ui_label(count,x+w/2-20,controls+4,40,0x18272a);client_ui_button(x+w-70,controls,60,22,TR("Next"),page+1<pages?17003:0,1);controls+=26;}
  if(p->kind==F_CHANGING||p->kind==F_FOOTBALL_GATE){client_ui_button(x+10,controls,w-20,26,TR("Change clothes"),17014,0);controls+=30;}
  if(p->status[0]){client_ui_paragraph(TR(p->status),x+10,controls,w-20,2,0x18272a);controls+=36;}
  /* Short outfit panels keep their actions under the controls instead of leaving a gap. */
  float actions=p->kind==F_CHANGING||p->kind==F_MANNEQUIN?fminf(controls+2,y+h-33):y+h-33;
  bool save=p->kind==F_TROPHY?p->fields[0].editable:p->kind!=F_GIFT&&(p->editable||p->kind==F_GIFT_BUY);
  if(save)client_ui_button(x+10,actions,w/2-14,26,TR(p->kind==F_GIFT_BUY?"Buy and send":p->kind==F_CLOCK?"Run command":p->kind==F_FOOTBALL_GATE?"Store kit":"Save"),17001,0);
  bool apply=p->can_apply;
  if(apply)client_ui_button(x+w/2+4,actions,w/2-14,26,TR(p->kind==F_GIFT?"Open gift":"Wear outfit"),17002,0);
}
