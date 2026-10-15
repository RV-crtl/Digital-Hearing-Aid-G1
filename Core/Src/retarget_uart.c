/* USER CODE BEGIN Header
 * printf/stdout retarget to the primary USART2 console.
 * This is intentionally separate from the command parser so existing demo code
 * can use printf() and still appear in the NUCLEO ST-LINK Virtual COM Port.
 * USER CODE END Header */
#include "usart.h"
#include <stdint.h>

int _write(int file, char *ptr, int len)
{
  (void)file;
  if (ptr == 0 || len <= 0) return 0;
  if (huart2.Instance != USART2) return len;
  if (HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, 1000U) != HAL_OK) return -1;
  return len;
}
