#pragma once
#include "common.h"

typedef struct { u16 def; u8 page; bool used; vita2d_texture *tex; u32 stamp, bytes; } FurniTex;

static FurniTex *furni_slot(FurniTex *entries, int count, u16 def, u16 page) {
  FurniTex *victim=&entries[0];
  for(int i=0;i<count;i++) {
    FurniTex *c=&entries[i];
    if(c->used && c->def==def && c->page==page)return c;
    if((!c->used && victim->used) || (c->used==victim->used && c->stamp<victim->stamp))victim=c;
  }
  return victim;
}

static FurniTex *furni_oldest(FurniTex *entries, int count, const FurniTex *keep) {
  FurniTex *oldest=NULL;
  for(int i=0;i<count;i++) {
    FurniTex *c=&entries[i];
    if(c!=keep && c->used && c->tex && (!oldest || c->stamp<oldest->stamp))oldest=c;
  }
  return oldest;
}
