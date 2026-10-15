#ifndef __USART_H__
#define __USART_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "main.h"

/* USART1: on-board BT401 control link (PA9 TX, PA10 RX). */
extern UART_HandleTypeDef huart1;
/* USART2: primary PC console via NUCLEO ST-LINK VCP (PA2 TX, PA3 RX). */
extern UART_HandleTypeDef huart2;

void MX_USART1_UART_Init(void);
void MX_USART2_UART_Init(void);

#ifdef __cplusplus
}
#endif
#endif
