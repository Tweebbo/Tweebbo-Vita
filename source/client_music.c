#include "language.h"
#include "client_music.h"
#include "gen/classic_music.h"
#include "official_music.h"
#include "client_ui.h"
#include "audio.h"
#include "assets.h"
#include "net.h"
#include "world.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_TRACKS 50
#define MAX_CLIPS 150
#define MAX_SONGS 50
#define MAX_SAMPLE 2048
#define CLASSIC_BASE 1000
#define CLASSIC_CHANNEL 12
#define UI_CHANNEL 13
#define RATE 16000
#define SLOT_MS 2000
typedef struct {u16 sample,start;} Clip;
typedef struct {char provider[12],id[24],title[80];u32 disc;int duration,slots;u8 clip_n[4];Clip clips[4][MAX_CLIPS];} Track;
typedef struct {u32 id;char title[80];} SavedSong;
static struct {
 bool known,playing,listening,classic_on; s64 offset,started,position;u64 revision,heard_key;
 u32 active,machine,inventory_revision;int index,track_n,song_n,disc_n,page,clip_on[4];
 Track tracks[MAX_TRACKS];SavedSong songs[MAX_SONGS];
 MusicDiscGroup discs[MUSIC_DISC_GROUPS];
 int job,sample_id;char server[128];bool requested;
} M={.listening=true};
static struct {int id;u8 *data;size_t n;u64 stamp;bool failed;} cache[32];
static size_t cache_bytes;
static void copy(char *out,size_t cap,const char *s){snprintf(out,cap,"%s",s?s:"");}
static bool music_machine(u16 def){for(int i=0;i<music_machine_count;i++)if(music_machines[i]==def)return true;return false;}
static int num(json_t *o, const char *k) { return (int)json_integer_value(json_object_get(o, k)); }

static void read_track(json_t *v, Track *t) {
  memset(t, 0, sizeof *t);
  copy(t->provider, sizeof t->provider, json_string_value(json_object_get(v, "provider")));
  copy(t->id, sizeof t->id, json_string_value(json_object_get(v, "id")));
  copy(t->title, sizeof t->title, json_string_value(json_object_get(v, "title")));
  t->duration = num(v, "duration");
  t->disc=(u32)json_integer_value(json_object_get(v,"disc"));
  json_t *song = json_object_get(v, "song");
  t->slots = num(song, "slots");
  size_t c; json_t *channel;
  json_array_foreach(json_object_get(song, "channels"), c, channel) {
    if (c >= 4) break;
    size_t i; json_t *clip;
    json_array_foreach(channel, i, clip) {
      if (t->clip_n[c] >= MAX_CLIPS) break;
      t->clips[c][t->clip_n[c]++] = (Clip){(u16)num(clip, "sample"), (u16)num(clip, "start")};
    }
  }
}


static struct { const char *name; u8 *data; size_t n; bool tried; } ui_sounds[3] = {{.name="message_new"},{.name="message_sent"},{.name="cash"}};
void client_music_ui_sound(const char *name) {
  for (int i = 0; i < 3; i++) {
    if (strcmp(ui_sounds[i].name, name)) continue;
    if (!ui_sounds[i].tried) {
      ui_sounds[i].tried = true;
      char path[80];
#if defined(__vita__)
      snprintf(path, sizeof path, "app0:ui/%s.pcm", name);
#else
      snprintf(path, sizeof path, "romfs:/ui/%s.pcm", name);
#endif
      FILE *f = fopen(path, "rb");
      if (f) {
        fseek(f, 0, SEEK_END); long bytes = ftell(f); rewind(f);
        if (bytes > 0 && bytes <= 128 * 1024) {
          u8 *data = malloc((size_t)bytes);
          if (data && fread(data, 1, (size_t)bytes, f) == (size_t)bytes) { ui_sounds[i].data = data; ui_sounds[i].n = (size_t)bytes; }
          else free(data);
        }
        fclose(f);
      }
    }
    if (ui_sounds[i].data) audio_play(UI_CHANNEL, ui_sounds[i].data, ui_sounds[i].n, 8, RATE, false, 0);
    return;
  }
}

void client_music_receive(const char *text, u64 now) {
  json_t *j = json_loads(text, 0, NULL), *st = json_object_get(j, "state");
  if (!json_is_object(st)) { json_decref(j); return; }
  M.known = true;
  M.offset = (s64)json_integer_value(json_object_get(j, "now")) - (s64)now;
  json_t *active = json_object_get(st, "active");
  M.active = json_is_integer(active) ? (u32)json_integer_value(active) : 0;
  M.index = num(st, "index");
  M.playing = json_is_true(json_object_get(st, "playing"));
  M.started = (s64)json_integer_value(json_object_get(st, "started"));
  M.position = (s64)json_integer_value(json_object_get(st, "position"));
  M.revision = (u64)json_integer_value(json_object_get(st, "revision"));
  // The queue shown: the machine playing, else the first one with tracks.
  json_t *queues = json_object_get(st, "queues"), *queue = NULL;
  M.machine = 0;
  char key[16];
  if (M.active) { snprintf(key, sizeof key, "%u", (unsigned)M.active); queue = json_object_get(queues, key); M.machine = M.active; }
  if (!queue) {
    const char *k; json_t *v;
    json_object_foreach(queues, k, v) if (json_array_size(v)) { queue = v; M.machine = (u32)strtoul(k, NULL, 10); break; }
  }
  if (!M.machine) for (int i=0;i<MAX_ITEMS;i++) {
    if(W.items[i].used&&music_machine(W.items[i].def)){M.machine=W.items[i].id;break;}
  }
  M.track_n = 0;
  size_t i; json_t *v;
  json_array_foreach(queue, i, v) if (M.track_n < MAX_TRACKS) read_track(v, &M.tracks[M.track_n++]);
  M.song_n = 0;
  const char *k;
  json_object_foreach(json_object_get(st, "songs"), k, v) {
    if (M.song_n >= MAX_SONGS) break;
    M.songs[M.song_n].id = (u32)strtoul(k, NULL, 10);
    copy(M.songs[M.song_n++].title, sizeof M.songs[0].title, json_string_value(json_object_get(v, "title")));
  }
  json_t *discs=json_object_get(j,"owned_discs");if(!json_is_array(discs))discs=json_object_get(st,"owned_discs");
  M.disc_n=official_music_groups(discs,M.discs);
  json_decref(j);
}

bool client_music_context(char *label, int cap, u64 now) {
  Ent *me = world_ent(W.me);
  if (!me || ent_moving(me, now)) return false;
  for (int i = 0; i < MAX_ITEMS; i++) {
    const Item *it = &W.items[i];
    if (!it->used || !music_machine(it->def) || abs(it->x - me->x) > 1 || abs(it->y - me->y) > 1) continue;
    snprintf(label, cap,"%s",TR("Open the playlist"));
    if (!M.machine) M.machine = it->id;
    return true;
  }
  return false;
}

static void command(const char *op, const char *extra) {
  if (!M.machine) return;
  char json[128];
  snprintf(json, sizeof json, "{\"op\":\"%s\",\"item\":%u%s%s}", op, (unsigned)M.machine, extra ? "," : "", extra ? extra : "");
  send_music(json);
}

static void silence(void){for(int c=0;c<4;c++){audio_stop(8+c);M.clip_on[c]=-1;}audio_stop(CLASSIC_CHANNEL);M.classic_on=false;M.heard_key=0;}
void client_music_reset(void){
  silence();M.known=M.playing=M.requested=false;M.active=M.machine=0;M.track_n=M.song_n=M.disc_n=M.page=0;
}
static int cached(int id){for(int i=0;i<32;i++)if(cache[i].id==id){cache[i].stamp=now_ms();return i;}return -1;}
static void want(int id){
  if(id<=0||id>=MAX_SAMPLE||M.job||cached(id)>=0||!net_online())return;
  char path[96];if(id>=CLASSIC_BASE)snprintf(path,sizeof path,"/static/trax/3ds/classic_%d.pcm",id-CLASSIC_BASE);else snprintf(path,sizeof path,"/static/trax/3ds/%d.pcm",id);
  M.job=net_http("GET",path,NULL,false);if(M.job)M.sample_id=id;
}
static void sample_result(void){
  HttpResult r;if(!M.job||!net_http_result(M.job,&r))return;M.job=0;
  int slot=0;for(int i=0;i<32;i++)if(!cache[i].id){slot=i;break;}else if(cache[i].stamp<cache[slot].stamp)slot=i;
  cache_bytes-=cache[slot].n;free(cache[slot].data);memset(&cache[slot],0,sizeof cache[slot]);cache[slot].id=M.sample_id;cache[slot].stamp=now_ms();
  size_t limit=M.sample_id==OFFICIAL_MUSIC_SAMPLE?OFFICIAL_MUSIC_BYTES:512*1024;
  if(r.status==200&&r.body&&r.len>0&&r.len<=limit&&(M.sample_id!=OFFICIAL_MUSIC_SAMPLE||r.len==OFFICIAL_MUSIC_BYTES)){cache[slot].data=(u8*)r.body;cache[slot].n=r.len;cache_bytes+=r.len;r.body=NULL;}
  else cache[slot].failed=true;
  free(r.body);
  while(cache_bytes>8*1024*1024){int old=-1;for(int i=0;i<32;i++)if(i!=slot&&cache[i].n&&(old<0||cache[i].stamp<cache[old].stamp))old=i;if(old<0)break;cache_bytes-=cache[old].n;free(cache[old].data);memset(&cache[old],0,sizeof cache[old]);}
}
void client_music_update(void){
  if(M.known&&M.machine&&M.inventory_revision!=W.inv_rev){M.inventory_revision=W.inv_rev;send_music("{\"op\":\"sync\"}");}
  if(strcmp(M.server,net_server())){
    client_music_reset();M.job=0;snprintf(M.server,sizeof M.server,"%s",net_server());
    for(int i=0;i<32;i++){free(cache[i].data);memset(&cache[i],0,sizeof cache[i]);}cache_bytes=0;
  }
  sample_result();
  if(!net_online()||!M.known||!M.playing||!M.listening||!M.active||M.index<0||M.index>=M.track_n){if(M.heard_key)silence();return;}
  Track *t=&M.tracks[M.index];bool classic=!strcmp(t->provider,"classic")||official_music_sample(t->provider,t->id);
  if(!classic&&strcmp(t->provider,"trax")){if(M.heard_key)silence();return;}
  u64 key=M.revision*64+(u64)M.index+1;
  if(key!=M.heard_key){silence();M.heard_key=key;}
  s64 elapsed=M.position+(s64)now_ms()+M.offset-M.started;if(elapsed<0)elapsed=0;
  if(classic){
    int id=official_music_sample(t->provider,t->id);if(!id)id=CLASSIC_BASE+atoi(t->id);want(id);int at=cached(id);
    if(!M.classic_on&&at>=0&&cache[at].data){
      size_t skip=(size_t)(elapsed*RATE/1000);if(skip<cache[at].n)M.classic_on=audio_play(CLASSIC_CHANNEL,cache[at].data,cache[at].n,8,RATE,false,skip);
    }
    return;
  }
  for(int c=0;c<4;c++){
    int found=-1,entry=-1;
    for(int i=0;i<t->clip_n[c];i++){
      Clip *clip=&t->clips[c][i];s64 from=(s64)clip->start*SLOT_MS;
      if(from>elapsed+8000)continue;
      int at=cached(clip->sample);s64 duration=at>=0&&cache[at].n?(s64)cache[at].n*1000/RATE:SLOT_MS;
      if(from+duration<elapsed)continue;
      want(clip->sample);
      if(elapsed>=from&&elapsed<from+duration){found=i;entry=at;break;}
    }
    if(found==M.clip_on[c])continue;
    audio_stop(8+c);M.clip_on[c]=-1;
    if(found>=0&&entry>=0&&cache[entry].data){
      size_t skip=(elapsed-(s64)t->clips[c][found].start*SLOT_MS)*RATE/1000;
      if(audio_play(8+c,cache[entry].data,cache[entry].n,8,RATE,false,skip))M.clip_on[c]=found;
    }
  }
}
enum { SYNC=11000, MUTE, LISTEN, PLAY, PAUSE, NEXT, PAGE, ADD, ADD_CLASSIC, ADD_DISC, REMOVE_DISC };
void client_music_action(int cmd,int arg){
  if(cmd==MUTE){audio_mute(!audio_muted());store_save("audio-muted",audio_muted()?"1":"0");return;}
  if(cmd==LISTEN){M.listening=!M.listening;if(!M.listening)silence();return;}
  if(!net_online())return;
  if(cmd!=SYNC && cmd!=PAGE && (!world_item(M.machine)||!world_can_arrange(world_item(M.machine))))return;
  if(cmd==SYNC){send_music("{\"op\":\"sync\"}");M.requested=true;return;}
  if(cmd==PAGE){M.page+=arg;if(M.page<0)M.page=0;client_ui_changed();return;}
  if(cmd==PLAY && arg>=0&&arg<M.track_n){char extra[32];snprintf(extra,sizeof extra,"\"index\":%d",arg);command("play",extra);}
  if(cmd==PAUSE)command("pause",NULL);
  if(cmd==NEXT)command("next",NULL);
  if(cmd==ADD&&arg>=0&&arg<M.song_n){char extra[40];snprintf(extra,sizeof extra,"\"song\":%u",M.songs[arg].id);command("add_song",extra);}
  if(cmd==ADD_CLASSIC&&arg>=0&&arg<CLASSIC_TRACK_COUNT){char extra[32];snprintf(extra,sizeof extra,"\"set\":%u",(unsigned)classic_tracks[arg].id);command("add_classic",extra);}
  if(cmd==ADD_DISC&&arg>=0&&arg<M.disc_n){char extra[32];snprintf(extra,sizeof extra,"\"disc\":%u",(unsigned)M.discs[arg].item);command("add_disc",extra);}
  if(cmd==REMOVE_DISC&&arg>=0&&arg<M.track_n&&M.tracks[arg].disc){const Item *machine=world_item(M.machine);if(machine&&machine->owner==W.guest_id){char extra[32];snprintf(extra,sizeof extra,"\"index\":%d",arg);command("remove",extra);}}
}
void client_music_draw(float x,float y,float w){
  if(!M.requested&&net_online()){send_music("{\"op\":\"sync\"}");M.requested=true;}
  const Track *t=M.playing&&M.index>=0&&M.index<M.track_n?&M.tracks[M.index]:NULL;
  const Item *machine=world_item(M.machine);
  bool control=net_online()&&machine&&world_can_arrange(machine);
  client_ui_label(t?t->title:TR("Room playlist"),x+10,y+30,w-20,0x18272a);
  client_ui_label(t&&official_music_sample(t->provider,t->id)?TR("Original music discs play on this console."):t&&!strcmp(t->provider,"classic")?TR("Kepler Classic music plays on this console."):t&&strcmp(t->provider,"trax")?TR("This provider plays on PC / phones."):M.job?TR("Loading Trax samples..."):!net_online()?TR("Connect to use the room playlist."):TR("Original Trax samples play on this console."),x+10,y+48,w-20,0x18272a);
  client_ui_button(x+8,y+72,144,24,audio_muted()?TR("Sound: muted"):TR("Sound: on"),MUTE,0);
  client_ui_button(x+158,y+72,144,24,M.listening?TR("Mute room music"):TR("Listen to music"),LISTEN,0);
  client_ui_button(x+8,y+102,94,24,M.playing?TR("Pause"):TR("Play"),control&&M.track_n?(M.playing?PAUSE:PLAY):0,M.index>=0&&M.index<M.track_n?M.index:0);
  client_ui_button(x+108,y+102,90,24,TR("Next track"),control&&M.track_n?NEXT:0,0);
  client_ui_button(x+208,y+102,94,24,TR("Refresh"),net_online()?SYNC:0,0);
  int classic_base=M.track_n+M.song_n+M.disc_n;
  int count=classic_base+CLASSIC_TRACK_COUNT;if(M.page*2>=count)M.page=0;
  for(int row=0;row<2&&M.page*2+row<count;row++){
    int i=M.page*2+row;char label[110];
    if(i<M.track_n)snprintf(label,sizeof label,"Play: %s",M.tracks[i].title);
    else if(i<M.track_n+M.song_n)snprintf(label,sizeof label,"Add: %s",M.songs[i-M.track_n].title);
    else if(i<classic_base)snprintf(label,sizeof label,"Disc: %s (x%u)",M.discs[i-M.track_n-M.song_n].title,(unsigned)M.discs[i-M.track_n-M.song_n].count);
    else snprintf(label,sizeof label,"Classic: %s",classic_tracks[i-classic_base].name);
    int cmd=i<M.track_n?PLAY:ADD,arg=i<M.track_n?i:i-M.track_n;
    if(i>=M.track_n+M.song_n){cmd=ADD_DISC;arg=i-M.track_n-M.song_n;}
    if(i>=classic_base){cmd=ADD_CLASSIC;arg=i-classic_base;}
    bool own=machine&&machine->owner==W.guest_id;
    if(i<M.track_n&&M.tracks[i].disc){client_ui_button(x+8,y+135+row*28,w-76,25,label,control?cmd:0,arg);client_ui_button(x+w-64,y+135+row*28,56,25,TR("Remove"),control&&own?REMOVE_DISC:0,i);}
    else client_ui_button(x+8,y+135+row*28,w-16,25,label,control&&(cmd!=ADD_DISC||own)?cmd:0,arg);
  }
  client_ui_button(x+8,y+193,140,23,TR("Previous"),M.page?PAGE:0,-1);client_ui_button(x+156,y+193,146,23,TR("Next page"),(M.page+1)*2<count?PAGE:0,1);
}
