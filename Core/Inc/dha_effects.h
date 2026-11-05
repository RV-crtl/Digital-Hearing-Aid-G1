#ifndef DHA_EFFECTS_H
#define DHA_EFFECTS_H
#include <stdint.h>
#include <stddef.h>
/* Both optional effects are continuous: no noise gate or signal absence mute.
 * ROOM is a subtle mono room ambience, NOT 3D surround (single transducer).
 * NR is a conservative microphone rumble high-pass, NOT true feed-forward ANC.
 * AudioTask exclusively owns DSP memory. */
void DhaEffects_Init(void);
void DhaEffects_SetRoom(uint8_t enabled);
void DhaEffects_SetNoiseReduction(uint8_t enabled);
void DhaEffects_Process(int16_t *audio,size_t frames,uint8_t source);
#endif
