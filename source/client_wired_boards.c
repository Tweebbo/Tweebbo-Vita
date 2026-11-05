#include "client_wired_boards.h"
#include "client_ui.h"
#include "wired_boards.h"
#include <stdio.h>

enum { BOARD_ACTION=20000,BOARD_SELECT=20070,BOARD_PAGE };
void client_wired_boards_action(int command,int argument){if(command==BOARD_SELECT)wired_boards_select(argument);else if(command==BOARD_PAGE)wired_board_panel.page+=argument;else if(command>=BOARD_ACTION&&command<BOARD_ACTION+64)wired_boards_action(command-BOARD_ACTION);client_ui_changed();}
void client_wired_boards_draw(float x,float y,float w,float h){
    wired_boards_update();WiredServicePanel *panel=&wired_board_panel;bool ready=!panel->pending,choosing=panel->button_count==1;int pages=(panel->rows+1)/2;if(pages<1)pages=1;if(panel->page<0)panel->page=0;if(panel->page>=pages)panel->page=pages-1;
    client_ui_label(panel->title,x+10,y+32,w-20,0x18272a);
    for(int row=0;row<2&&panel->page*2+row<panel->rows;row++){char title[160],detail[160],label[324];int index=panel->page*2+row;wired_boards_row(index,title,sizeof title,detail,sizeof detail);snprintf(label,sizeof label,"%s - %s",title,detail);if(choosing)client_ui_button(x+10,y+54+row*29,w-20,26,label,ready?BOARD_SELECT:0,index);else{client_ui_label(title,x+12,y+53+row*29,w-24,0x18272a);client_ui_label(detail,x+12,y+67+row*29,w-24,0x5c6264);}}
    if(!panel->rows)client_ui_paragraph(panel->status[0]?panel->status:panel->body,x+12,y+56,w-24,3,0x18272a);
    for(int index=0;index<panel->button_count;index++){WiredServiceButton *button=&panel->buttons[index];float width=(w-26)/2;client_ui_button(x+10+(index%2)*(width+6),y+115+(index/2)*25,width,23,button->label,button->enabled&&ready?BOARD_ACTION+button->action:0,0);}
    client_ui_button(x+10,y+h-29,94,23,"Previous rows",panel->page>0?BOARD_PAGE:0,-1);client_ui_button(x+w-104,y+h-29,94,23,"Next rows",panel->page+1<pages?BOARD_PAGE:0,1);
}
