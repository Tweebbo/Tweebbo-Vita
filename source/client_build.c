#include "client_build.h"
#include "catalog.h"
#include "world.h"
#include "pets.h"
#include <limits.h>
_Static_assert(sizeof(BuildGhost)==28,"Precise placement must retain the ghost memory budget");

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
bool client_build_valid(BuildGhost *g) {
  const FurniDef *d=def_get(g->def);
  const Item *original=g->ignore?world_item(g->ignore):NULL;
  if (!W.in_room || !d || !world_can_decorate() || (g->ignore && (!original || !world_can_arrange(original)))) return false;
  if (d->kind==KIND_WALL) {
    g->z_fine=0;
    int lo,hi;
    bool ranged=furni_wall_range(g->def,&lo,&hi);
    return world_tile(g->x,g->y) && !world_tile(g->x-(g->rot%2==0),g->y-(g->rot%2==1)) && g->z>=8 && g->z<=110
      && (!ranged || (g->z>=lo && g->z<=hi))
      && !(g->rot%2==0 && g->x==W.door_x+1 && g->y==W.door_y);
  }
  bool magic=def_height_helper(d)!=0;
  if (original) {
    if(world_supports_items(original))return false;
    int w,h;def_size(original->def,original->rot,&w,&h);
    for (int y=original->y;y<original->y+h;y++) for (int x=original->x;x<original->x+w;x++) {
      if (!magic&&(d->kind!=KIND_FLOOR || d->seat) && actor_at(x,y,false)) return false;
    }
  }
  WorldPlacement placement;if(!world_placement(g->def,g->x,g->y,g->rot,g->ignore,0,&placement))return false;
  int w,h;def_size(g->def,g->rot,&w,&h);
  for (int y=g->y;y<g->y+h;y++) for (int x=g->x;x<g->x+w;x++) {
    int cell=(y-g->y)*w+x-g->x;
    if (!magic&&!(placement.helper_cells&(1ull<<cell))&&actor_at(x,y,d->kind==KIND_FLOOR && !d->seat)) return false;
  }
  u32 height=placement.height100;
  g->z=(int)(height/100);g->z_fine=(u8)(height%100);return true;
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

void client_build_wall_clamp(BuildGhost *g) {
  int lo,hi;
  if(!g->wall || !furni_wall_range(g->def,&lo,&hi))return;
  if(g->z<lo)g->z=lo;
  if(g->z>hi)g->z=hi;
}
