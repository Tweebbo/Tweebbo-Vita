#include "client_wired_services.h"
#include "client_ui.h"
#include "wired_services.h"
#include "language.h"
#include <stdio.h>

enum { SERVICE_ACTION=18000,SERVICE_SELECT=18070,SERVICE_PAGE,SERVICE_EDIT };
static int editing,body_page;
static size_t offsets[64],body_next;
static u32 revision;
static bool edited(const char *text){return wired_services_edit(editing,text);}
void client_wired_services_action(int command,int argument){
    if(command==SERVICE_SELECT)wired_services_select(argument);
    else if(command==SERVICE_PAGE)wired_service_panel.page+=argument;
    else if(command==SERVICE_EDIT){editing=argument;client_ui_edit_limit(wired_services_field_label(argument),wired_services_field_value(argument),wired_services_field_limit(argument),edited);}
    else if(command==18074&&body_page<63)offsets[++body_page]=body_next;
    else if(command==18075&&body_page>0)body_page--;
    else if(command>=SERVICE_ACTION&&command<SERVICE_ACTION+64)wired_services_action(command-SERVICE_ACTION);
    client_ui_changed();
}
void client_wired_services_draw(float x,float y,float w,float h){
    wired_services_update();WiredServicePanel *panel=&wired_service_panel;bool ready=!panel->pending;float top=y+33;
    if(revision!=panel->revision){revision=panel->revision;body_page=0;offsets[0]=0;}
    client_ui_label(panel->view==WS_DETAIL||panel->view==WS_POSITION?panel->title:TR(panel->title),x+10,top,w-20,0x18272a);top+=20;
    int pages=(panel->rows+WS_PAGE_ROWS-1)/WS_PAGE_ROWS;if(pages<1)pages=1;if(panel->page<0)panel->page=0;if(panel->page>=pages)panel->page=pages-1;
    if(panel->view==WS_LIST){
        for(int row=0;row<WS_PAGE_ROWS&&panel->page*WS_PAGE_ROWS+row<panel->rows;row++){char title[160],detail[96],label[260];int index=panel->page*WS_PAGE_ROWS+row;wired_services_row(index,title,sizeof title,detail,sizeof detail);snprintf(label,sizeof label,"%s - %s",title,detail);client_ui_button(x+10,top+row*29,w-20,26,label,ready?SERVICE_SELECT:0,index);}
        if(!panel->rows)client_ui_paragraph(TR(panel->body),x+12,top+8,w-24,4,0x18272a);
        float footer=y+h-58,available=w-124;
        for(int index=0;index<panel->button_count;index++){WiredServiceButton *button=&panel->buttons[index];float width=available/panel->button_count-4;client_ui_button(x+10+index*(width+4),footer,width,25,button->label,button->enabled&&ready?SERVICE_ACTION+button->action:0,0);}
        client_ui_button(x+w-110,footer,45,25,TR("Prev"),panel->page>0?SERVICE_PAGE:0,-1);client_ui_button(x+w-59,footer,49,25,TR("Next"),panel->page+1<pages?SERVICE_PAGE:0,1);
    }else{
        if(panel->view==WS_FORM){
            client_ui_paragraph(TR(panel->body),x+12,top,w-24,2,0x18272a);top+=31;
            for(int field=0;field<panel->field_count;field++){client_ui_label(wired_services_field_label(field),x+12,top,w-24,0x26373a);client_ui_field(x+10,top+12,w-20,21,wired_services_field_value(field),wired_services_field_label(field),ready?SERVICE_EDIT:0,field);top+=35;}
        }else{const char *body=TR(panel->body);int lines=panel->view==WS_CONFIRM?5:3;body_next=offsets[body_page]+client_ui_paragraph(body+offsets[body_page],x+12,top,w-24,lines,0x18272a);top+=panel->view==WS_CONFIRM?85:53;}
        for(int index=0;index<panel->button_count;index++){WiredServiceButton *button=&panel->buttons[index];float width=(w-26)/2;client_ui_button(x+10+(index%2)*(width+6),top+(index/2)*26,width,23,button->label,button->enabled&&ready?SERVICE_ACTION+button->action:0,0);}
        client_ui_button(x+10,y+h-34,84,25,TR("Back"),ready?SERVICE_ACTION+WS_BACK:0,0);
        if(panel->view!=WS_FORM){client_ui_button(x+103,y+h-34,60,25,TR("Prev text"),body_page>0?18075:0,0);client_ui_button(x+171,y+h-34,w-181,25,TR("More text"),TR(panel->body)[body_next]&&body_page<63?18074:0,0);}
    }
    if(panel->status[0])client_ui_label(TR(panel->status),x+12,y+h-12,w-24,0x5c6264);
}
