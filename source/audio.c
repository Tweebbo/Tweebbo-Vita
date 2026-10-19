#include "audio.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static struct {s16 *data;size_t n;u64 cursor;int rate;bool loop;} voices[12];
static bool muted;
void audio_mix(s16 *out,int frames){
  pthread_mutex_lock(&mu);
  for(int f=0;f<frames;f++){
    int sum=0;
    for(int c=0;c<12;c++){
      if(!voices[c].data||!voices[c].n)continue;
      size_t index=voices[c].cursor/48000;
      if(index>=voices[c].n){if(voices[c].loop){voices[c].cursor%=voices[c].n*48000;index=voices[c].cursor/48000;}else continue;}
      sum+=voices[c].data[index]/3;voices[c].cursor+=voices[c].rate;
    }
    if(sum>32767)sum=32767;
    if(sum<-32768)sum=-32768;
    out[f*2]=out[f*2+1]=muted?0:(s16)sum;
  }
  pthread_mutex_unlock(&mu);
}
void audio_stop(int c){if(c<0||c>=12)return;pthread_mutex_lock(&mu);free(voices[c].data);memset(&voices[c],0,sizeof voices[c]);pthread_mutex_unlock(&mu);}
void audio_clear(void){for(int c=0;c<12;c++)audio_stop(c);}
bool audio_play(int c,const void *data,size_t n,int bits,int rate,bool loop,size_t skip){
  if(c<0||c>=12||!data||!n||n>512*1024||skip>=n||(bits!=8&&bits!=16)||rate<8000||rate>48000)return false;
  s16 *copy=malloc(n*sizeof *copy);if(!copy)return false;
  if(bits==16)memcpy(copy,data,n*2);else for(size_t i=0;i<n;i++)copy[i]=(s16)((const s8*)data)[i]*256;
  pthread_mutex_lock(&mu);free(voices[c].data);voices[c].data=copy;voices[c].n=n;voices[c].cursor=skip*48000;voices[c].rate=rate;voices[c].loop=loop;pthread_mutex_unlock(&mu);return true;
}
bool audio_busy(int c){if(c<0||c>=12)return false;pthread_mutex_lock(&mu);bool busy=voices[c].data&&(voices[c].loop||voices[c].cursor/48000<voices[c].n);pthread_mutex_unlock(&mu);return busy;}
void audio_mute(bool value){pthread_mutex_lock(&mu);muted=value;pthread_mutex_unlock(&mu);}
bool audio_muted(void){pthread_mutex_lock(&mu);bool value=muted;pthread_mutex_unlock(&mu);return value;}
static const char *snow_names[]={"LS-miss","LS-throw","LS-hit-1","LS-hit-2","LS-hit-3","LS-walk-loop-1","LS-getsnowball","LS-C64-win-1","LS-C64-loose-1","LS-C64-draw-1"};
static struct {s16 *data;size_t n;} snow[10];
void audio_snow_load(void){
  for(int i=0;i<10;i++){
    if(snow[i].data)continue;
    char path[100];
#if defined(__vita__)
    snprintf(path,sizeof path,"app0:snow/%s.pcm",snow_names[i]);
#else
    snprintf(path,sizeof path,"romfs:/snow/%s.pcm",snow_names[i]);
#endif
    FILE *f=fopen(path,"rb");if(!f)continue;
    fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);
    if(bytes>0&&bytes<=256*1024&&bytes%2==0){s16 *p=malloc(bytes);if(p){if(fread(p,1,bytes,f)==(size_t)bytes){snow[i].data=p;snow[i].n=bytes/2;}else free(p);}}
    fclose(f);
  }
}
void audio_snow_play(const char *name,bool loop){
  for(int i=0;i<10;i++)if(!strcmp(name,snow_names[i])&&snow[i].data){
    int c=0;if(loop){if(audio_busy(0))return;}else{for(c=1;c<8;c++)if(!audio_busy(c))break;if(c==8)return;}
    audio_play(c,snow[i].data,snow[i].n,16,22050,loop,0);return;
  }
}
void audio_snow_free(void){for(int c=0;c<8;c++)audio_stop(c);for(int i=0;i<10;i++){free(snow[i].data);memset(&snow[i],0,sizeof snow[i]);}}
