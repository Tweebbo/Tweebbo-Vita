#include <assert.h>
#include <stdio.h>
#include "../source/show.c"

static float panel_left;
static BuildGhost ghost;
static FurniPart part = {.w=30,.h=30,.ox=-15,.oy=-30,.alpha=255};
static FurniVariant variant = {.count=1,.parts=&part};
const PublicSeat public_seats[1] = {{0}};
const int public_seat_count = 0;
float client_ui_room_left(void) { return panel_left; }
const BuildGhost *client_shop_ghost(void) { return &ghost; }
bool pool_camera(float *x,float *y,u64 now) { (void)x;(void)y;(void)now;return false; }
bool pool_hides(const Ent *e) { (void)e;return false; }
const FurniDef *def_get(u16 id) { (void)id;return NULL; }
void def_size(u16 id,u8 rot,int *w,int *h) { (void)id;(void)rot;*w=*h=1; }
const FurniVariant *variant_frame(u16 def,u8 rot,u8 state,u16 frame) { (void)def;(void)rot;(void)state;(void)frame;return &variant; }
u16 furni_frame(u16 def,u8 state,u64 now) { (void)def;(void)state;(void)now;return 0; }

int main(void) {
  W.in_room=true;W.me=1;W.w=W.h=20;memset(W.rows,'0',400);
  W.ents[0]=(Ent){.used=true,.id=1,.x=3,.y=3,.fx=3,.fy=3};
  V.bx0=-700;V.bx1=700;V.by0=-200;V.by1=700;
  for(int level=0;level<3;level++) {
    V.zoom_level=level;V.snapped=false;camera(1000);
    if(!level) {
      assert(scr_x(V.bx0)>=7 && scr_x(V.bx1)<=SCREEN_W-7);
      assert(scr_y(V.by0)>=31 && scr_y(V.by1)<=SCREEN_H-31);
    } else assert(V.zoom==(level==1?1:2));
    float sx,sy;iso(3,3,&sx,&sy);
    if(level==2)assert(scr_y(sy+HALF_H-ANCHOR_Y+8)-20*V.zoom>=64);
    RoomTarget t=room_target(scr_x(sx),scr_y(sy+HALF_H-30),1000);
    assert(t.person==1 && !t.furniture);
    assert(!room_target(-1000,-1000,1000).person);
    W.items[0]=(Item){.used=true,.id=2,.def=4,.x=6,.y=4};
    iso(6,4,&sx,&sy);
    t=room_target(scr_x(sx),scr_y(sy-15),1000);
    assert(t.furniture==2 && !t.person);
    W.items[0].slide_at=1000;W.items[0].fx=4;W.items[0].fy=4;
    iso(5,4,&sx,&sy);
    t=room_target(scr_x(sx),scr_y(sy-15),1250);
    assert(t.furniture==2);
    W.items[0].used=false;
  }
  panel_left=150;V.zoom_level=0;V.snapped=false;camera(1000);
  assert(scr_x(V.bx0)>=panel_left+7 && scr_x(V.bx1)<=SCREEN_W-7);
  V.zoom_level=2;V.snapped=false;camera(1000);
  float sx,sy;iso(3,3,&sx,&sy);
  assert(fabsf(scr_x(sx)-(SCREEN_W+panel_left)*.5f)<=1);
  puts("PASS: overview bounds, standard/close-up framing, panels and player/furniture picking at every zoom, including moving furniture.");
}
