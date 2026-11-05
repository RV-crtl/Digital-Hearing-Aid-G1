#!/usr/bin/env python3
"""Check canonical QR261104.8 STM32CubeIDE tree (source-only, not an ARM build)."""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
checks = []
def ck(value, label):
    checks.append((bool(value), label))
    print(("PASS " if value else "FAIL ") + label)
def read(relative):
    p=ROOT/relative
    return p.read_text(errors="replace") if p.is_file() else ""
for xml in (".project",".cproject"):
    try:
        ET.parse(ROOT/xml)
        ck(True, f"{xml} is valid Eclipse XML")
    except (OSError, ET.ParseError) as e:
        ck(False, f"{xml} is valid Eclipse XML: {e}")
name="261104-Digital_Hearing_Aid"
ck(f"<name>{name}</name>" in read(".project"),"STM32CubeIDE project identity")
ck((ROOT/(name+".ioc")).is_file(),"matching CubeMX .ioc")
ck(len(list(ROOT.rglob("main.c")))==1,"exactly one main.c")
ck(len(list(ROOT.rglob("*.ioc")))==1,"exactly one .ioc")
ck(not (ROOT/"Recovery").exists(),"no Recovery copy")
ck(not (ROOT/"References").exists(),"no duplicate reference prototype project")
ck((ROOT/"STM32L476RGTX_FLASH.ld").is_file(),"linker script")
for p in ("Core/Src/main.c","Core/Src/demo_app.c","Core/Src/rtos_bootstrap.c",
          "Core/Src/sai.c","Core/Src/pcm_transport.c","Core/Src/g1_product_board.c",
          "Core/Src/g1_user_controls.c","Core/Src/tc_s24_recovery.c",
          "Core/Src/tc_cleanup.c","Core/Inc/tc_cleanup.h",
          "Core/Src/tc_idle_squelch.c","Core/Inc/tc_idle_squelch.h",
          "Core/Src/g1_ambient_dsp.c","Core/Src/speaker_output.c",
          "Core/Startup/startup_stm32l476rgtx.s",
          "Drivers/CMSIS/Device/ST/STM32L4xx/Include/stm32l476xx.h",
          "Middlewares/Third_Party/FreeRTOS/Source/tasks.c"):
    ck((ROOT/p).is_file(),"required implementation: "+p)
s=read("Core/Src/demo_app.c")
ck("Demo_StartNativeRTOS();" in read("Core/Src/main.c"),"main launches native RTOS")
ck("vTaskStartScheduler();" in read("Core/Src/rtos_bootstrap.c"),"FreeRTOS scheduler start")
ck('xTaskCreate(StartAudioTask, "AudioTask", 1024U, NULL, 7U' in s,"priority 7 AudioTask")
ck("vTaskNotifyGiveFromISR(g_audio_task" in s,"interrupt-to-AudioTask notification")
ck("capture_push_bt_from_isr" in s and "capture_push_tc_from_isr" in s,"independent BT/TC DMA capture")
ck("TcS24Recovery_Process(" in s,"telecoil full-precision recovery")
ck("TcCleanup_Block(" in s and "TcIdleSquelch_Process(" in s,"TC fixed filtering and idle comfort")
ck("process_selected_audio(" in s,"single selected-source processing")
ck("HAL_SAI_Transmit_DMA(" in (s+read("Core/Src/speaker_output.c")),"speaker I2S DMA")
ck("G1_MicTakeBlock(" in s,"PDM microphone integration")
ck("#define DHA_ENABLE_RTOS_STRESS 0" in read("Core/Inc/dha_build_mode.h"),"stress task default disabled")
failed=[label for ok,label in checks if not ok]
print(f"\nCANONICAL SOURCE CHECK: {len(checks)-len(failed)}/{len(checks)} passed.")
print("Does not establish a native STM32CubeIDE GCC build or a hardware/acoustic PASS.")
raise SystemExit(1 if failed else 0)
