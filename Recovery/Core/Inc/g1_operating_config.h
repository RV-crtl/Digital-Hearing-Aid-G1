#ifndef G1_OPERATING_CONFIG_H
#define G1_OPERATING_CONFIG_H
/* R26.6 BENCH DEMO: serial diagnostic UART is enabled so real interference
 * can be measured and calibrated. No PC is needed for ordinary audio.
 * Change G1_DIAGNOSTICS_UART_ENABLE=0 only after accepting that HUM LEARN
 * commands will be unavailable (volatile calibration does not persist).
 * R24: factory operating image is completely untethered.
 * A missing PC, ST-LINK or serial monitor must not affect its audio/UI tasks.
 * Define G1_DIAGNOSTICS_UART_ENABLE=1 at compile time for a BENCH build with
 * USART2 serial diagnostics. USART1 is the BT401 link and ALWAYS stays active.
 * Disconnecting ST-LINK requires correct independent board VDD/VDDA, NRST,
 * BOOT0 and ground; software cannot provide any missing power/reset path. */
#ifndef G1_DIAGNOSTICS_UART_ENABLE
#define G1_DIAGNOSTICS_UART_ENABLE 1
#endif
#if (G1_DIAGNOSTICS_UART_ENABLE != 0) && (G1_DIAGNOSTICS_UART_ENABLE != 1)
#error "G1_DIAGNOSTICS_UART_ENABLE must be 0 or 1"
#endif
/* 100/100 is the bounded maximum user-controlled digital gain, NOT a
 * calibrated sound-pressure ceiling. Do not wear or in-ear test without a
 * measured hardware limiter / electroacoustic characterisation. */
#define G1_OUTPUT_MAX_Q15 24576U
#endif
