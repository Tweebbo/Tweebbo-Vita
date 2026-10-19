#include "audio.h"
#include <psp2/audioout.h>
#include <pthread.h>
#include <stdatomic.h>
static pthread_t worker;
static atomic_bool stop;
static int port=-1;
static bool started;
static void *work(void *unused){
  (void)unused;s16 samples[1024*2];
  while(!atomic_load(&stop)){audio_mix(samples,1024);if(sceAudioOutOutput(port,samples)<0)break;}
  return NULL;
}
bool audio_init(void){
  if(started)return true;
  port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,1024,48000,SCE_AUDIO_OUT_MODE_STEREO);if(port<0)return false;
  int volume[2]={SCE_AUDIO_VOLUME_0DB,SCE_AUDIO_VOLUME_0DB};sceAudioOutSetVolume(port,SCE_AUDIO_VOLUME_FLAG_L_CH|SCE_AUDIO_VOLUME_FLAG_R_CH,volume);
  atomic_store(&stop,false);
  if(pthread_create(&worker,NULL,work,NULL)){sceAudioOutReleasePort(port);port=-1;return false;}
  started=true;return true;
}
void audio_exit(void){
  if(started){atomic_store(&stop,true);pthread_join(worker,NULL);sceAudioOutReleasePort(port);started=false;port=-1;}
  audio_snow_free();audio_clear();
}
