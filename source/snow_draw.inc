/* Shared clipping for the generated SnowWar atlas; backend draw_tex guards GPU use. */
void gfx_snow_sprite(unsigned id,float x,float y,float kx,float ky,u32 color,float left,float top,float right,float bottom) {
  if(id>=snow_sprite_count || !kx || ky<=0)return;
  const SnowSprite *s=&snow_sprites[id];if(s->page>=snow_loaded)return;
  bool flip=kx<0;float scale=fabsf(kx);
  x+=flip?-(s->ox+s->w)*scale:s->ox*scale;y+=s->oy*ky;
  int l=(int)fmaxf(0,ceilf((left-x)/scale)),t=(int)fmaxf(0,ceilf((top-y)/ky));
  int r=(int)fminf(s->w,floorf((right-x)/scale)),b=(int)fminf(s->h,floorf((bottom-y)/ky));
  if(r<=l || b<=t)return;
  int sx=s->x+(flip?s->w-r:l);
  draw_tex(snow_pages[s->page],sx,s->y+t,r-l,b-t,x+l*scale,y+t*ky,scale,ky,flip,color);
}
