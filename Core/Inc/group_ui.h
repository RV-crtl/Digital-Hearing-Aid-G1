#ifndef GROUP_UI_H
#define GROUP_UI_H
#include <stdint.h>
/* Portable debounced Nucleo B1 gesture decoder. One ControlTask owner.
 * Poll ~every 5 ms; raw=1 means pressed. No heap, interrupts or RTOS calls.
 */
typedef enum {
 GROUP_UI_NONE=0,
 GROUP_UI_SINGLE=1,
 GROUP_UI_DOUBLE=2,
 GROUP_UI_TRIPLE=3,
 GROUP_UI_LONG=4,
 GROUP_UI_RESET=5
} GroupUiGesture;
typedef struct {
 uint32_t changed_ms, down_ms, last_up_ms, dispatch_count;
 uint8_t raw_prev, stable, pending_clicks, initialized;
} GroupUiButton;
void GroupUi_Init(GroupUiButton *ui);
GroupUiGesture GroupUi_Poll(GroupUiButton *ui, uint32_t now_ms, uint8_t raw_pressed);
#endif
