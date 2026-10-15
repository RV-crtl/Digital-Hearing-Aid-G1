#ifndef TC_CLEANUP_H
#define TC_CLEANUP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { float hp_lp; float lp; float envelope; } TcCleanup;
void TcCleanup_Reset(TcCleanup *st);
void TcCleanup_Block(TcCleanup *st,const int16_t *input,int16_t *output,size_t n);
#endif
