#ifndef G1_BOOT_DIAG_H
#define G1_BOOT_DIAG_H
#include <stdint.h>
/* Real boot reset flags and selected oscillator; stdout via USART2 only. */
void G1_BootCapture(uint32_t reset_csr, uint32_t debug_at_reset,
                    uint32_t fallback, uint32_t hse_attempt, uint32_t sysclk_hz);
void G1_BootReport(void);
#endif
