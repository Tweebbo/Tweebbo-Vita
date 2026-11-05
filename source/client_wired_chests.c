#include "client_wired_chests.h"
#include "client_ui.h"
#include "wired_chests.h"
#include "language.h"
#include <stdio.h>

enum { CHEST_ACTION=19000,CHEST_SELECT=19070,CHEST_PAGE,CHEST_EDIT };
static int editing;
static bool edited(const char *text){return wired_chests_edit(editing,text);}
void client_wired_chests_action(int command,int argument){
    if(command==CHEST_SELECT)wired_chests_select(argument);else if(command==CHEST_PAGE)wired_chests_page(argument);else if(command==CHEST_EDIT){editing=argument;client_ui_edit_limit(wired_chests_field_label(argument),wired_chests_field_value(argument),wired_chests_field_limit(argument),edited);}else if(command>=CHEST_ACTION&&command<CHEST_ACTION+64)wired_chests_action(command-CHEST_ACTION);
    if(!wired_chest_panel.open){client_ui_close();}client_ui_changed();
}
void client_wired_chests_draw(float x,float y,float w,float h){
    wired_chests_update();WiredChestPanel *p=&wired_chest_panel;bool ready=!p->pending;float top=y+36;
    bool list=p->view!=WC_MAIN&&p->view!=WC_FORM&&p->view!=WC_REVIEW&&p->view!=WC_RESULT;
    if(list){int pages=(p->rows+WC_PAGE_ROWS-1)/WC_PAGE_ROWS;if(pages<1)pages=1;if(p->page<0)p->page=0;if(p->page>=pages)p->page=pages-1;
        for(int row=0;row<WC_PAGE_ROWS&&p->page*WC_PAGE_ROWS+row<p->rows;row++){char title[192],detail[128],label[324];int index=p->page*WC_PAGE_ROWS+row;wired_chests_row(index,title,sizeof title,detail,sizeof detail);snprintf(label,sizeof label,"%s%s%s",title,detail[0]?" - ":"",detail);int check=wired_chests_row_check(index);if(check>=0)client_ui_check_button(x+10,top+row*29,w-20,26,label,check==1,ready&&wired_chests_row_enabled(index)?CHEST_SELECT:0,index);else if(wired_chests_row_enabled(index))client_ui_button(x+10,top+row*29,w-20,26,label,ready?CHEST_SELECT:0,index);else{client_ui_label(title,x+12,top+row*29,w-24,0x18272a);if(detail[0])client_ui_label(detail,x+12,top+row*29+14,w-24,0x5c6264);}}
        if(!p->rows)client_ui_paragraph(TR(p->pending?"Loading furniture...":"No furniture here. Use Back to choose another action."),x+12,top+8,w-24,4,0x18272a);
        float footer=y+h-56,available=w-124;for(int i=0;i<p->button_count;i++){WiredChestButton *b=&p->buttons[i];float width=available/p->button_count-4;client_ui_button(x+10+i*(width+4),footer,width,25,b->label,b->enabled&&ready?CHEST_ACTION+b->action:0,0);}
        client_ui_button(x+w-110,footer,45,25,TR("Prev"),ready&&wired_chests_has_page(-1)?CHEST_PAGE:0,-1);client_ui_button(x+w-59,footer,49,25,TR("Next"),ready&&wired_chests_has_page(1)?CHEST_PAGE:0,1);
    }else{client_ui_paragraph(p->kind==WC_LABEL&&p->view==WC_MAIN?p->body:TR(p->body),x+12,top,w-24,p->view==WC_FORM?2:3,0x18272a);top+=p->view==WC_FORM?31:57;
        if(p->view==WC_FORM)for(int field=0;field<p->field_count;field++){client_ui_label(wired_chests_field_label(field),x+12,top,w-24,0x26373a);client_ui_field(x+10,top+13,w-20,22,wired_chests_field_value(field),wired_chests_field_label(field),ready?CHEST_EDIT:0,field);top+=35;}
        for(int i=0;i<p->button_count;i++){WiredChestButton *b=&p->buttons[i];float width=(w-26)/2;client_ui_button(x+10+(i%2)*(width+6),top+(i/2)*25,width,22,b->label,b->enabled&&ready?CHEST_ACTION+b->action:0,0);}
    }
    if(p->status[0])client_ui_label(TR(p->status),x+12,y+h-12,w-24,0x5c6264);
}
