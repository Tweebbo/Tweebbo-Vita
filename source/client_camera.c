#include "language.h"
#include "client_camera.h"
#include "client_ui.h"
#include "catalog.h"
#include "gfx.h"
#include "net.h"
#include "show.h"
#include "world.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TAKE=12000, RETAKE, ZOOM, CAPTION, SAVE };
static struct {
  u8 shot[PHOTO_W*PHOTO_H],pixels[PHOTO_W*PHOTO_H];
  int capture,job,zoom;
  bool still,dirty;
  u64 capture_started;
  char caption[256],status[160],identity[128];
} C;
bool client_camera_owned(void) {
  for(int i=0;i<W.inv_n;i++){const FurniDef *d=def_get(W.inv[i].def);if(d && !strcmp(d->name,"Camera"))return true;}
  return false;
}
static void message(const char *text){snprintf(C.status,sizeof C.status,"%s",text);client_ui_changed();}
bool client_camera_capturing(void){return C.capture!=0;}
void client_camera_scene(void){if(C.capture==1){gfx_photo_request(C.shot);C.capture=2;}}
static void preview(void) {
  int z=C.zoom?C.zoom:1,w=PHOTO_W/z,h=PHOTO_H/z;
  for(int y=0;y<PHOTO_H;y++)for(int x=0;x<PHOTO_W;x++)
    C.pixels[y*PHOTO_W+x]=C.shot[((PHOTO_H-h)/2+y*h/PHOTO_H)*PHOTO_W+(PHOTO_W-w)/2+x*w/PHOTO_W];
  C.dirty=true;
}
static bool caption(const char *text){snprintf(C.caption,sizeof C.caption,"%s",text);return true;}
void client_camera_action(int cmd,int arg) {
  if(C.job || C.capture)return;
  if(cmd==TAKE || cmd==RETAKE) {
    if(!W.in_room || !net_online() || !client_camera_owned()){message("Keep a Camera in your inventory and connect to the hotel.");return;}
    C.still=false;C.capture=1;C.capture_started=now_ms();C.zoom=1;message("Taking photo...");
  } else if(cmd==ZOOM && C.still){C.zoom+=arg;if(C.zoom<1)C.zoom=1;if(C.zoom>11)C.zoom=11;preview();}
  else if(cmd==CAPTION)client_ui_edit(TR("Photo caption (96 characters)"),C.caption,caption);
  else if(cmd==SAVE && C.still && net_online()) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n=sizeof C.pixels;char *b64=malloc(4*((n+2)/3)+1);if(!b64){message("Not enough memory. Try again.");return;}
    size_t j=0;
    for(size_t i=0;i<n;i+=3){unsigned v=(unsigned)C.pixels[i]<<16;if(i+1<n)v|=(unsigned)C.pixels[i+1]<<8;if(i+2<n)v|=C.pixels[i+2];b64[j++]=alphabet[v>>18];b64[j++]=alphabet[(v>>12)&63];b64[j++]=i+1<n?alphabet[(v>>6)&63]:'=';b64[j++]=i+2<n?alphabet[v&63]:'=';}
    b64[j]=0;json_t *body=json_pack("{s:s,s:s}","gray",b64,"caption",C.caption);free(b64);
    char *payload=body?json_dumps(body,JSON_COMPACT):NULL;json_decref(body);
    if(payload){C.job=net_http("POST","/api/photo",payload,true);free(payload);}
    if(C.job){snprintf(C.identity,sizeof C.identity,"%s",net_token());message("Saving photo...");}
    else message("Could not send photo. Try Save again.");
  }
  client_ui_changed();
}
void client_camera_update(void) {
  if(C.capture==2){int result=gfx_photo_status();if(result){C.capture=0;C.still=result>0;if(C.still){preview();message("Review your photo before saving.");}else message("Could not capture the room. Try again.");}}
  if(C.capture==1 && (!W.in_room || now_ms()-C.capture_started>3000)){C.capture=0;message("Return to the room view before taking a photo.");}
  // Texture uploads happen between frames, after Vita's rendering fence.
  if(C.dirty){C.dirty=false;if(!gfx_photo_preview(C.pixels)){C.still=false;message("Could not load the photo preview.");}}
  HttpResult r;
  if(C.job && net_http_result(C.job,&r)) {
    C.job=0;json_t *j=r.body?json_loads(r.body,0,NULL):NULL;
    u32 id=(u32)json_integer_value(json_object_get(j,"item"));u16 def=(u16)json_integer_value(json_object_get(j,"def"));
    if(r.status==200 && id && def && !strcmp(C.identity,net_token())) {
      bool found=false;for(int i=0;i<W.inv_n;i++)found|=W.inv[i].id==id;
      if(!found && W.inv_n<MAX_INV)W.inv[W.inv_n++]=(InvItem){id,def};
      C.still=false;C.caption[0]=0;message("Photo saved in your inventory.");show_toast(C.status,now_ms());
    } else {const char *err=json_string_value(json_object_get(j,"error"));message(err?err:"Could not confirm the save. Check inventory before retrying.");}
    memset(C.identity,0,sizeof C.identity);json_decref(j);free(r.body);
  }
}
void client_camera_draw(float x,float y,float w,float h) {
  if(C.still)gfx_photo_draw(x+10,y+29,161,117);
  else {fill_rect(x+10,y+29,161,117,client_ui_rgb(0x222b2e));client_ui_paragraph(TR("Captures the centre of your room without the interface or chat."),x+20,y+43,141,5,0xffffff);}
  char zoom[32];snprintf(zoom,sizeof zoom,"Zoom %dx",C.zoom?C.zoom:1);
  client_ui_label(zoom,x+182,y+30,w-192,0x18272a);
  bool ready=C.still&&!C.job&&!C.capture;
  client_ui_button(x+182,y+50,50,24,"-",ready&&C.zoom>1?ZOOM:0,-1);
  client_ui_button(x+238,y+50,w-248,24,"+",ready&&C.zoom<11?ZOOM:0,1);
  client_ui_button(x+182,y+82,w-192,26,TR("Caption"),ready?CAPTION:0,0);
  client_ui_paragraph(C.caption[0]?C.caption:TR("No caption"),x+182,y+114,w-192,2,0x26373a);
  client_ui_label(C.status[0]?C.status:client_camera_owned()?TR("Ready to take a photo."):TR("Buy a Camera and keep it in your inventory."),x+10,y+153,w-20,0x26373a);
  client_ui_button(x+8,y+h-36,142,26,C.still?TR("Retake"):TR("Take photo"),!C.job&&!C.capture&&W.in_room&&net_online()&&client_camera_owned()?TAKE:0,0);
  client_ui_button(x+158,y+h-36,w-166,26,C.job?TR("Saving..."):TR("Save to inventory"),ready&&net_online()?SAVE:0,0);
}
