#ifndef GROUP_ENCODER_H
#define GROUP_ENCODER_H
#include <stdint.h>
/* Quadrature decoder, no STM32 timer consumed.  The generic/default
 * threshold is 4 Gray transitions per detent; board code may select 2. */
typedef struct {
  uint8_t previous;
  int8_t quarter_steps;
  uint8_t transitions_per_step;
  uint32_t bad_edges;
} GroupEncoder;
void GroupEncoder_Init(GroupEncoder *e, uint8_t phase_a, uint8_t phase_b);
int GroupEncoder_Update(GroupEncoder *e, uint8_t phase_a, uint8_t phase_b);
#endif
