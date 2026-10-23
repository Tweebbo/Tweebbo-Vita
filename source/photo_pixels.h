#pragma once
#include "common.h"
/* Platform framebuffer pixels are ABGR; sample the same 322x234 logical centre crop. */
static inline void photo_sample(u8 *gray,const u32 *pixels,int stride,int origin_x,int origin_y,int scale) {
  for(int y=0;y<234;y++)for(int x=0;x<322;x++) {
    u32 c=pixels[(origin_y+y*scale)*stride+origin_x+x*scale];
    gray[y*322+x]=(u8)((77*(c&255)+150*((c>>8)&255)+29*((c>>16)&255)+128)>>8);
  }
}
static inline u32 photo_duotone(u8 gray) {
  unsigned r=104+(151*gray+127)/255,g=31+(173*gray+127)/255,b=16+(86*gray+127)/255;
  return 0xff000000|r|(g<<8)|(b<<16);
}
