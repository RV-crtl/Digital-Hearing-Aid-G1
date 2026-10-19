#include "group_ui.h"
#include <stddef.h>
#define GROUP_UI_DEBOUNCE_MS 45U
#define GROUP_UI_CLICK_WINDOW_MS 340U
#define GROUP_UI_LONG_MS 1200U
#define GROUP_UI_RESET_MS 4000U
void GroupUi_Init(GroupUiButton *ui) {
 if (!ui) return;
 *ui=(GroupUiButton){0};
}
GroupUiGesture GroupUi_Poll(GroupUiButton *ui, uint32_t now_ms, uint8_t raw_pressed) {
 GroupUiGesture gesture=GROUP_UI_NONE;
 if (!ui) return gesture;
 raw_pressed=(uint8_t)(raw_pressed!=0U);
 if (!ui->initialized) {
  ui->initialized=1U; ui->raw_prev=raw_pressed; ui->stable=raw_pressed;
  ui->changed_ms=now_ms; ui->down_ms=now_ms;
  return gesture;
 }
 if (raw_pressed!=ui->raw_prev) {ui->raw_prev=raw_pressed;ui->changed_ms=now_ms;}
 if (raw_pressed!=ui->stable &&
     (uint32_t)(now_ms-ui->changed_ms)>=GROUP_UI_DEBOUNCE_MS) {
  ui->stable=raw_pressed;
  if (raw_pressed) ui->down_ms=now_ms;
  else {
   uint32_t duration=(uint32_t)(now_ms-ui->down_ms);
   if (duration>=GROUP_UI_RESET_MS) {gesture=GROUP_UI_RESET;ui->pending_clicks=0U;}
   else if (duration>=GROUP_UI_LONG_MS) {gesture=GROUP_UI_LONG;ui->pending_clicks=0U;}
   else {
    /* Short electrical bounce cannot make a full debounced click. */
    if (ui->pending_clicks<3U) ++ui->pending_clicks;
    ui->last_up_ms=now_ms;
    if (ui->pending_clicks==3U) {gesture=GROUP_UI_TRIPLE;ui->pending_clicks=0U;}
   }
  }
 }
 /* Do not emit an early single click while a multi-click is in progress. */
 if (gesture==GROUP_UI_NONE && ui->stable==0U && ui->pending_clicks!=0U &&
     (uint32_t)(now_ms-ui->last_up_ms)>=GROUP_UI_CLICK_WINDOW_MS) {
  gesture=ui->pending_clicks==1U?GROUP_UI_SINGLE:GROUP_UI_DOUBLE;
  ui->pending_clicks=0U;
 }
 if (gesture!=GROUP_UI_NONE) ++ui->dispatch_count;
 return gesture;
}
