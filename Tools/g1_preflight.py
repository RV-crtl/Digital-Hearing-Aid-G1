#!/usr/bin/env python3
"""G1 source/build preflight; source PASS never implies an ARM or acoustic PASS."""
from __future__ import annotations
import argparse, hashlib, json, re, shutil, subprocess, sys
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def check():
    bad=[]; notes=[]
    for rel in (".project",".cproject","261013-Individual_Demonstration_Connectivity.ioc",
                "Core/Src/demo_app.c","Core/Inc/dha_build_mode.h","Core/Src/group_ui.c",
                "Core/Inc/group_ui.h","Core/Src/sai.c","Core/Inc/FreeRTOSConfig.h",
                "Drivers/CMSIS/Device/ST/STM32L4xx/Include/stm32l476xx.h"):
        if not (ROOT/rel).is_file(): bad.append("Missing: "+rel)
    pairs=(".cproject","Core/Src/demo_app.c","Core/Src/group_ui.c",
           "Core/Inc/group_ui.h","Core/Inc/dha_build_mode.h","Core/Src/sai.c",
           "Core/Inc/FreeRTOSConfig.h")
    for rel in pairs:
        a,b=ROOT/rel,ROOT/"Recovery"/rel
        if not a.is_file() or not b.is_file() or sha(a)!=sha(b):
            bad.append("Recovery mismatch: "+rel+"; rerun PREPARE after CubeMX")
    for rel in (".project",".cproject"):
        try: ET.parse(ROOT/rel)
        except (OSError,ET.ParseError) as exc: bad.append(f"Invalid {rel}: {exc}")
    hdr=ROOT/"Core/Inc/dha_build_mode.h"
    if hdr.exists() and not re.search(r"#define\s+DHA_ENABLE_RTOS_STRESS\s+0\b",hdr.read_text()):
        bad.append("Operating image must default to RTOS stress disabled")
    cp=ROOT/".cproject"
    if cp.exists() and re.search(r"(?:-D)?DHA_ENABLE_RTOS_STRESS\s*=\s*1",cp.read_text()):
        bad.append("CubeIDE build enables RTOS stress in normal configuration")
    app=ROOT/"Core/Src/demo_app.c"
    if app.exists():
        source=app.read_text()
        for token in ('xTaskCreate(StartAudioTask, "AudioTask"',
                      'xTaskCreate(StartBtTask, "BtTask"',
                      'vTaskNotifyGiveFromISR(g_audio_task',
                      'ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5U))',
                      'g_children_started = (a == pdPASS && b == pdPASS) ? 1U : 0U;'):
            if token not in source: bad.append("Missing firmware invariant: "+token)
        if "#if DHA_ENABLE_RTOS_STRESS" not in source:
            bad.append("Stress task compilation no longer gated")
    source=ROOT/"Middlewares/Third_Party/FreeRTOS/Source"
    needed=("tasks.c","queue.c","include/FreeRTOS.h","portable/GCC/ARM_CM4F/port.c")
    missing=[str((source/f).relative_to(ROOT)) for f in needed if not (source/f).is_file()]
    if missing: notes.append("FreeRTOS dependency absent: install STM32Cube FW_L4 V1.18.x and run PREPARE_PROJECT.bat before CubeIDE Clean/Build")
    arm_gcc=shutil.which("arm-none-eabi-gcc") is not None
    if not arm_gcc: notes.append("arm-none-eabi-gcc not on PATH (may be bundled inside CubeIDE)")
    try:
        revision=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,
                                         text=True,stderr=subprocess.DEVNULL,timeout=5).strip()
    except (OSError,subprocess.CalledProcessError,subprocess.TimeoutExpired):
        revision=None
    return {"status":"FAIL" if bad else "SOURCE_PREFLIGHT_PASS",
            "source_failures":bad,"environment_notices":notes,
            "proof":{"recovery_pairs_expected":len(pairs),
                     "middleware_ready":not missing,"middleware_missing":missing,
                     "arm_gcc_on_path":arm_gcc,"git_sha":revision,
                     "arm_binary_built":False,"physical_hardware_verified":False}}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-arm-ready",action="store_true")
    parser.add_argument("--output",type=Path,default=ROOT/"Validation/results/g1_preflight.json")
    args=parser.parse_args(); result=check()
    if args.require_arm_ready and (not result["proof"]["middleware_ready"] or
                                   not result["proof"]["arm_gcc_on_path"]):
        result["source_failures"].append("ARM-ready preflight requires FreeRTOS and ARM GCC")
        result["status"]="FAIL"
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    for m in result["source_failures"]: print("FAIL:",m)
    for m in result["environment_notices"]: print("NOTICE:",m)
    print(result["status"],"(NOT an ARM build or physical validation)")
    return int(result["status"]=="FAIL")
if __name__=="__main__": sys.exit(main())
