"""Static DMA/RTOS handoff invariants; not hardware timing evidence."""
from pathlib import Path
import re
R=Path(__file__).resolve().parents[2]
app=(R/'Core/Src/demo_app.c').read_text()
dma=(R/'Core/Src/dma.c').read_text()
conf=(R/'Core/Inc/FreeRTOSConfig.h').read_text()
checks={
 'DMA producer notifies AudioTask twice':app.count('vTaskNotifyGiveFromISR(g_audio_task')==2,
 'Completed ring head published before notification':all(
   app.index('g_%s_cap_head = next;' % k) <
   app.index('vTaskNotifyGiveFromISR(g_audio_task',app.index('g_%s_cap_head = next;' % k))
   for k in ('bt','tc')),
 'AudioTask bounded blocking wait':'ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5U))' in app,
 'Capture drain bounded':'while (handled < (CAPTURE_RING_DEPTH - 1U)' in app,
 'FSM periodic servicing':'step_service(now)' in app and '>= 20U' in app,
 'User events produced through queue':'xQueueSend(g_group_events' in app and 'GROUP_EVENT_SOURCE' in app,
 'User events consumed by AudioTask':'xQueueReceive(g_group_events' in app,
 'DMA NVIC priorities syscall-safe':all(re.search(
   r'HAL_NVIC_SetPriority\(\s*%s\s*,\s*5\s*,\s*0\s*\)'%irq,dma)
   for irq in ('DMA1_Channel6_IRQn','DMA2_Channel1_IRQn')),
 'FreeRTOS syscall ISR ceiling':bool(re.search(
   r'#define\s+configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY\s+5\b',conf)),
 'Existing profiler retained':'Demo_RuntimeStatsTimerRead()' in app,
}
for label,ok in checks.items():print(('PASS' if ok else 'FAIL')+': '+label)
if not all(checks.values()):raise SystemExit(1)
print('PASS: 10 static RTOS contracts; real NVIC and deadlines require hardware tests')
