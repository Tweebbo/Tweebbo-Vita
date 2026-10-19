#pragma once
#include "common.h"
bool audio_init(void);
void audio_exit(void);
void audio_mix(s16 *stereo,int frames);
void audio_clear(void);
void audio_stop(int channel);
bool audio_play(int channel,const void *data,size_t samples,int bits,int rate,bool loop,size_t skip);
bool audio_busy(int channel);
void audio_mute(bool muted);
bool audio_muted(void);
void audio_snow_load(void);
void audio_snow_free(void);
void audio_snow_play(const char *name,bool loop);
