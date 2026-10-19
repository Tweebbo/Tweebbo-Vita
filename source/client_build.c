#include "client_build.h"
#include "catalog.h"
#include "world.h"
#include "pets.h"
#include <limits.h>

static bool actor_at(int x, int y, bool floor) {
  for(int i=0;i<pets_count();i++)if(pet_at(i)->x==x && pet_at(i)->y==y)return true;
  for (int i=0;i<MAX_ENTS;i++) {
    const Ent *e=&W.ents[i];
    if (!e->used) continue;
    if (!floor && e->x==x && e->y==y) return true;
    if (ent_moving(e,now_ms()) && (int)(e->fx+.5f)==x && (int)(e->fy+.5f)==y) return true;
  }
  return false;
}
static bool covers(const Item *it, int x, int y) {
  const FurniDef *d=def_get(it->def);
  if (!it->used || !d || d->kind==KIND_WALL) return false;
  int w,h; def_size(it->def,it->rot,&w,&h);
  return x>=it->x && x<it->x+w && y>=it->y && y<it->y+h;
}
bool client_build_valid(BuildGhost *g) {
  const FurniDef *d=def_get(g->def);
  const Item *original=g->ignore?world_item(g->ignore):NULL;
  if (!W.in_room || !d || !world_can_decorate() || (g->ignore && (!original || !world_can_arrange(original)))) return false;
  if (d->kind==KIND_WALL)
    return world_tile(g->x,g->y) && !world_tile(g->x-(g->rot%2==0),g->y-(g->rot%2==1)) && g->z>=8 && g->z<=110
      && !(g->rot%2==0 && g->x==W.door_x+1 && g->y==W.door_y);
  if (original) {
    int w,h;def_size(original->def,original->rot,&w,&h);
    for (int y=original->y;y<original->y+h;y++) for (int x=original->x;x<original->x+w;x++) {
      if ((d->kind!=KIND_FLOOR || d->seat) && actor_at(x,y,false)) return false;
      if (d->seat) for (int i=0;i<MAX_ITEMS;i++) {
        const Item *it=&W.items[i];
        if (it->id!=g->ignore && covers(it,x,y) && it->z>=original->z+d->seat) return false;
      }
    }
  }
  int w,h,height=-1;def_size(g->def,g->rot,&w,&h);
  for (int y=g->y;y<g->y+h;y++) for (int x=g->x;x<g->x+w;x++) {
    if (!world_tile(x,y) || (x==W.door_x && y==W.door_y) || actor_at(x,y,d->kind==KIND_FLOOR && !d->seat)) return false;
    int surface=0;
    for (int i=0;i<MAX_ITEMS;i++) {
      const Item *it=&W.items[i];
      if (it->id==g->ignore || !covers(it,x,y)) continue;
      const FurniDef *support=def_get(it->def);
      if (support->kind!=KIND_FLOOR && !(support->flags&DEF_STACK)) return false;
      int top=it->z+support->seat;if(top>surface)surface=top;
    }
    if(height>=0 && height!=surface)return false;
    height=surface;
  }
  g->z=height<0?0:height;
  return g->z<256;
}
void client_build_wall(BuildGhost *g, int step, bool nearest) {
  struct { int x,y,rot; } slots[MAX_SIDE*MAX_SIDE*2];
  int n=0,best=0,distance=INT_MAX;
  for(int y=0;y<W.h;y++)for(int x=0;x<W.w;x++)for(int rot=0;rot<2;rot++) {
    if(!world_tile(x,y) || world_tile(x-(rot==0),y-(rot==1)) || (rot==0 && x==W.door_x+1 && y==W.door_y))continue;
    slots[n].x=x;slots[n].y=y;slots[n].rot=rot;
    int dd=(x-g->x)*(x-g->x)+(y-g->y)*(y-g->y);
    if((nearest && dd<distance) || (!nearest && x==g->x && y==g->y && rot==g->rot)) {best=n;distance=dd;}
    n++;
  }
  if(n) {best=(best+step+n)%n;g->x=slots[best].x;g->y=slots[best].y;g->rot=slots[best].rot;}
}
