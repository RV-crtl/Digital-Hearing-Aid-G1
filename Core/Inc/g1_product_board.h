#ifndef G1_PRODUCT_BOARD_H
#define G1_PRODUCT_BOARD_H
/* Standalone STM32L476RG U13 / original KiCad PCB hardware abstraction.
 * No PC or Nucleo needed at runtime; UART is an OPTIONAL test connector.
 * This firmware requires electrical verification before use near the ear. */
#include "main.h"
#include <stdint.h>
#define G1_MIC_FRAMES 128U
#define G1_TOUCH_ADDR_7BIT 0x5AU

typedef struct {
  int8_t encoder_clicks;       /* signed detents observed since previous poll */
  uint16_t touch_rising;       /* MPR121 electrodes 0..11, rising-edge bitmap */
} G1PhysicalInput;
typedef struct {
  volatile uint32_t mic_half, mic_full, mic_overflow, mic_errors;
  volatile uint32_t mic_blocks, mic_peak, mic_last_ms;
  volatile uint32_t touch_reads, touch_errors, encoder_detents;
  volatile uint32_t touch_present, mic_ready, mic_running;
  volatile uint32_t init_error_bits;
  volatile uint32_t mic_init_attempts, mic_start_attempts;
  volatile uint32_t mic_init_stage, mic_last_hal_status;
  volatile uint32_t touch_address, touch_raw;
  volatile uint32_t touch_proximity, touch_ecr, touch_stage;
  volatile uint32_t touch_probe_attempts, touch_acks, touch_hal_error;
  volatile uint32_t touch_hal_state, touch_isr;
  volatile uint32_t touch_transport; /* 0=STM32 HAL, 1=GPIO software I2C */
  volatile uint32_t touch_soft_scans, touch_soft_scan_mask;
  volatile uint32_t touch_soft_clock_rises, touch_soft_clock_failures;
  volatile uint32_t touch_last_probe_status;
  volatile uint32_t encoder_irq_edges, encoder_bad_edges, encoder_phase, encoder_poll_edges;
} G1BoardDiagnostics;
extern G1BoardDiagnostics g_g1_board;
void G1_BoardInit(void);
HAL_StatusTypeDef G1_MicReinit(void);
void G1_BoardPoll(uint32_t now, G1PhysicalInput *input);
void G1_TouchReprobe(void); /* defer recovery to the normal ControlTask poll */
void G1_EncoderEdgeIRQ(void);
HAL_StatusTypeDef G1_MicStart(void);
HAL_StatusTypeDef G1_MicStop(void);
uint8_t G1_MicTakeBlock(int16_t dest[G1_MIC_FRAMES]);
#endif
