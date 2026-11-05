#!/usr/bin/env python3
"""Fail-closed firmware source and portable host verification.
Native STM32CubeIDE 1.19.0/GNU/Newlib ELF and PCB tests are separate obligations.
"""
from pathlib import Path
import subprocess,shutil,sys,re,tempfile,hashlib
P=Path(__file__).resolve().parents[1]
name='261104-Digital_Hearing_Aid'
def check(cond,msg):
 if not cond:raise RuntimeError(msg)
 print('PASS',msg)
check((P/'.project').exists() and (P/'.cproject').exists() and (P/(name+'.ioc')).exists(),'editable named STM32CubeIDE project, metadata and IOC')
check(len(list(P.rglob('main.c')))==1 and len(list(P.rglob('*.ioc')))==1,'single compilable main.c and single IOC in project')
check(name in (P/'.project').read_text() and name in (P/'.cproject').read_text(),'project name consistent in Eclipse/CubeIDE')
check('FREERTOS.configTOTAL_HEAP_SIZE=24576' in (P/(name+'.ioc')).read_text(),'CubeMX heap matches the linker')
ld=(P/'STM32L476RGTX_FLASH.ld').read_text()
config=(P/'Core/Inc/FreeRTOSConfig.h').read_text()
app=(P/'Core/Src/demo_app.c').read_text()
check('configAPPLICATION_ALLOCATED_HEAP         1' in config and '(size_t)24576' in config,'single application-allocated 24 KiB RTOS arena')
check('.capture_ram2 (NOLOAD)' in ld and 'extern uint8_t _capture_ram2_start[];' in app and '(uint32_t *)SRAM2_BASE' not in app,'8 KiB recording scratch has correct C array declaration and does not alias heap')
check('_capture_ram2_end <= ORIGIN(RAM2_CAPTURE)' in ld and '} >RAM2_HEAP' in ld and '} >RAM2_CAPTURE' in ld,'independent heap and capture MEMORY regions enforced at link')
check('#define DEMO_RAW_CAPTURE_FRAMES     1024U' in (P/'Core/Inc/demo_app.h').read_text(),'capture capacity bounded by RAM2 scratch')
check('DhaFeedback_Render(dst' in app and 'DhaFeedback_Request(DHA_CUE_BOOT)' in app,'power and UI cues integrated into real speaker ISR')
check('DhaEffects_Process(g_demo_processed_mono' in app and 'source==2U' in (P/'Core/Src/dha_effects.c').read_text(),'optional spatial effect applies only to actual Bluetooth source 2')
check('QrDemoTest_Render(&s_test_generator' in app and 'QR_TEST_PEAK_PCM 32200' in (P/'Core/Inc/qr_demo_test.h').read_text(),'bounded test generator executed in speaker DMA refill')
check('QrSleep_Update(&s_qr_sleep' in app and 'if (s_audio_sleep_active)' in app,'proximity-woken software sleep and IRQ-level output silence')
check('return true; /* SAI1A / SAI2A / SAI2B' in app,'telecoil clock-locked output, mic elastic fallback')
check('AT+BA01' in app and 'AT+B500' in app and 'AT+CM08' in app,'BtTask sleep disconnect and disable commands')
check('QrBtVolume_ConnectedEdge(&volume_tracker,linked)' in app and 'QR_BT_CONNECT_VOLUME_COMMAND' in app and 'AT+CA100' in (P/'Core/Inc/qr_bt_volume.h').read_text(),'user-requested CA100 issued for each observed BT link-up; handset volume unverified')
for rel in ['Core/Inc/FreeRTOSConfig.h','Core/Src/freertos.c','Core/Src/demo_app.c','Core/Src/g1_user_controls.c','Core/Src/dha_feedback.c','Core/Src/dha_effects.c','Core/Src/qr_demo_test.c','Core/Src/qr_sleep_policy.c','Core/Inc/qr_demo_test.h','Core/Inc/qr_sleep_policy.h','Core/Inc/qr_bt_volume.h','Core/Src/qr_bt_volume.c','Core/Startup/startup_stm32l476rgtx.s','STM32L476RGTX_FLASH.ld','.cproject']:
 check((P/rel).read_bytes()==(P/'Recovery'/rel).read_bytes(),'recovery parity '+rel)

def run(argv):
 q=subprocess.run(argv,cwd=P,text=True,capture_output=True)
 if q.returncode:
  print(q.stdout[-2500:]);print(q.stderr[-2500:]);raise RuntimeError('FAIL: '+' '.join(map(str,argv)))
 print(q.stdout[-800:].strip());return q
for f in ['CHECK_PROJECT.py','CHECK_GROUP.py','Tools/r14_build_guard.py','Tools/verify_r267_memory.py']:
 run([sys.executable,str(P/f)])
# Windows STM32CubeIDE contains ARM GCC, not necessarily a native host GCC.
# These portable PCM tests are optional locally but mandatory in CI/release.
cc=shutil.which('gcc') or shutil.which('clang')
if cc:
 for f in ['Tools/test_r24_operating.py','Tools/test_r264_s24_recovery.py','Tools/test_r266_calibration.py','Tools/test_qr_demo_release.py']:
  run([sys.executable,str(P/f)])
else:
 print('SKIP: portable PCM/control host regressions (no native host gcc/clang). Release CI already passed them; perform CubeIDE native build.')
if cc:
 with tempfile.TemporaryDirectory() as d:
  exe=Path(d)/('release_tests.exe' if sys.platform.startswith('win') else 'release_tests')
  run([cc,'-std=c11','-Wall','-Wextra','-Werror','-O2','-I'+str(P/'Core/Inc'),str(P/'Tests/test_dha_release.c'),str(P/'Core/Src/g1_user_controls.c'),str(P/'Core/Src/dha_feedback.c'),str(P/'Core/Src/dha_effects.c'),'-o',str(exe)])
  run([str(exe)])
else:print('SKIP: portable host C tests (host gcc/clang not found). Run in CI or install host compiler.')
if cc:
 with tempfile.TemporaryDirectory() as d:
  exe=Path(d)/('bt_edges.exe' if sys.platform.startswith('win') else 'bt_edges')
  run([cc,'-std=c11','-Wall','-Wextra','-Werror','-O2','-I'+str(P/'Core/Inc'),str(P/'Tests/test_qr_bt_volume.c'),str(P/'Core/Src/qr_bt_volume.c'),'-o',str(exe)])
  run([str(exe)])
if shutil.which('clang') and shutil.which('ld.lld') and shutil.which('readelf'):
 run([sys.executable,str(P/'Tools/verify_r267_memory.py'),'--arm-smoke'])
 run([sys.executable,str(P/'Tools/verify_arm_source_link.py')])
else:print('SKIP: ARM Clang/LLD layout smoke (tools missing); this is NOT the CubeIDE GNU link.')
print('PASS SOURCE RELEASE VERIFICATION. Native STM32CubeIDE GNU ELF and PCB hardware validation still REQUIRED.')
