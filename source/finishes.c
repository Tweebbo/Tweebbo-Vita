#include "finishes.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const FinishChoice *choice(int kind, const char *id) {
  for(int i=0;i<finish_choice_count;i++)
    if(finish_choices[i].kind==kind && !strcmp(finish_choices[i].id,id))return &finish_choices[i];
  return NULL;
}

static u8 *pixels(const char *root, const FinishChoice *f) {
  if(!f || !f->w || !f->h || f->w>4096 || f->h>4096)return NULL;
  char path[256];snprintf(path,sizeof path,"%s/%s.rgb",root,f->texture);
  FILE *file=fopen(path,"rb");if(!file)return NULL;
  size_t n=(size_t)f->w*f->h*3;
  u8 *data=malloc(n);
  if(data && (fread(data,1,n,file)!=n || fgetc(file)!=EOF)) {free(data);data=NULL;}
  fclose(file);return data;
}

bool finishes_paint(const char *root, const RoomBackground *bg, int page,
                    const char *wall, const char *floor, u8 *rgba,
                    int width, int height, size_t stride) {
  if(!bg || page<0 || page>=bg->tile_count || !rgba || width<=0 || height<=0 || stride<(size_t)width*4)return false;
  const RoomTile *tile=&bg->tiles[page];
  if(width<tile->w || height<tile->h)return false;
  const FinishChoice *choices[2]={choice(1,wall),choice(2,floor)};
  if(!choices[0] && !choices[1])return true;
  u8 *textures[2]={pixels(root,choices[0]),pixels(root,choices[1])};
  bool ok=false;
  FILE *mask=NULL;
  for(int i=0;i<2;i++)if(choices[i] && !textures[i])goto done;
  char path[256];snprintf(path,sizeof path,"%s/%s.mask",root,bg->model);
  mask=fopen(path,"rb");if(!mask)goto done;
  u8 head[4],run[4];
  if(fread(head,1,4,mask)!=4)goto done;
  unsigned mw=head[0]|head[1]<<8,mh=head[2]|head[3]<<8,pos=0;
  if(!mw || !mh || mw>4096 || mh>4096 || tile->x+tile->w>mw || tile->y+tile->h>mh)goto done;
  while(pos<mw*mh) {
    if(fread(run,1,4,mask)!=4)goto done;
    unsigned count=run[0]|run[1]<<8,label=run[2];
    if(!count || count>mw*mh-pos)goto done;
    int which=label==3 || label==4?0:label==1 || label==2 || label==5?1:-1;
    for(unsigned end=pos+count;pos<end;pos++) {
      unsigned x=pos%mw,y=pos/mw;
      if(which<0 || !choices[which] || x<tile->x || y<tile->y || x>=tile->x+tile->w || y>=tile->y+tile->h)continue;
      const FinishChoice *f=choices[which];
      int u=which==0?(int)x+bg->x:(int)floorf(((int)x+bg->x)/2.f+(int)y+bg->y);
      int v=which==0?run[3]:(int)floorf((int)y+bg->y-((int)x+bg->x)/2.f);
      u=(u%f->w+f->w)%f->w;v=(v%f->h+f->h)%f->h;
      u8 *pixel=rgba+(y-tile->y)*stride+(x-tile->x)*4;
      for(int c=0;c<3;c++) {
        int color=f->color>>(16-c*8)&255;
        if(label==3)color=color>16?color-16:0;
        float factor=label==2?.67f:label==5?1.f:textures[which][((size_t)v*f->w+u)*3+c]/255.f;
        pixel[c]=(u8)lrintf(color*factor);
      }
    }
  }
  ok=fgetc(mask)==EOF && !ferror(mask);
done:
  if(mask)fclose(mask);
  free(textures[0]);free(textures[1]);return ok;
}
