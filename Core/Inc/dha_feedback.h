#ifndef DHA_FEEDBACK_H
#define DHA_FEEDBACK_H
#include <stdint.h>
#include <stddef.h>
/* Original, generated transient feedback (not copied from any commercial product).
 * Single writer: SAI2B DMA refill ISR. Other tasks post atomic events only.
 * No allocation, blocking calls, floating point or HAL calls in render path. */
typedef enum {
 DHA_CUE_BOOT=0, DHA_CUE_BT_CONNECTED=1, DHA_CUE_MIC=2, DHA_CUE_BT=3,
 DHA_CUE_TC=4, DHA_CUE_MUTE=5, DHA_CUE_UNMUTE=6,
 DHA_CUE_SPATIAL_ON=7, DHA_CUE_SPATIAL_OFF=8,
 DHA_CUE_NR_ON=9, DHA_CUE_NR_OFF=10, DHA_CUE_BT_DISCONNECTED=11,
 DHA_CUE_COUNT=12
} DhaCue;
void DhaFeedback_Request(DhaCue cue);
void DhaFeedback_Render(int16_t *interleaved_stereo, size_t frames);
void DhaFeedback_ClearPending(void);
uint32_t DhaFeedback_Pending(void);
#endif
