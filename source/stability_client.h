#pragma once
#include <stdint.h>
#ifndef STABILITY_DIAGNOSTICS
#define STABILITY_DIAGNOSTICS 0
#endif
#if STABILITY_DIAGNOSTICS
void stability_client_tick(uint64_t now);
void stability_client_metrics(unsigned memory_kib,unsigned power);
void stability_client_close(void);
#else
static inline void stability_client_tick(uint64_t now){(void)now;}
static inline void stability_client_metrics(unsigned memory_kib,unsigned power){(void)memory_kib;(void)power;}
static inline void stability_client_close(void){}
#endif
