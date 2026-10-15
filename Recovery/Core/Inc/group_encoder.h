#ifndef GROUP_ENCODER_H
#define GROUP_ENCODER_H
#include <stdint.h>
/* Quadrature decoder, no STM32 timer consumed.  Four valid Gray transitions
 * constitute a detent.  Invalid jumps are ignored; physical input still
 * needs pull-ups and suitable noise filtering. */
typedef struct { uint8_t previous; int8_t quarter_steps; uint32_t bad_edges; } GroupEncoder;
void GroupEncoder_Init(GroupEncoder *e, uint8_t phase_a, uint8_t phase_b);
int GroupEncoder_Update(GroupEncoder *e, uint8_t phase_a, uint8_t phase_b);
#endif
