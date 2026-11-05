#!/usr/bin/env python3
"""Fail-closed source and metadata checks. A PASS IS NOT hardware/ARM link proof."""
from pathlib import Path
import xml.etree.ElementTree as ET
import subprocess,sys,shutil,json,hashlib,datetime
R=Path(__file__).resolve().parents[1]
status={};checks=[]
def ck(label,cond):
 checks.append((label,bool(cond))); print(('PASS ' if cond else 'FAIL ')+label)
name='G1_Product_R26_Adaptive_Audio_CubeIDE119'
ck('Unique CubeIDE identity',name in (R/'.project').read_text())
ck('IOC matches project name',(R/(name+'.ioc')).is_file())
for p in ('.project','.cproject'):
 try: ET.parse(R/p);ok=True
 except Exception:ok=False
 ck('Valid XML '+p,ok)
for rel in ('Core/Src/demo_app.c','Core/Src/g1_product_board.c','Core/Inc/g1_product_board.h','.cproject'):
 ck('Recovery matches '+rel,(R/rel).read_bytes()==(R/'Recovery'/rel).read_bytes())
s=(R/'Core/Src/demo_app.c').read_text()
ck('MIC independent input DMA','const uint32_t topology=dma_desired_topology();' in s and 'const bool need_bt=(topology&1U)!=0U;' in s and 'SpeakerOutput_StartDma(' in s)
ck('BT does not depend on TC','g_demo_control.direct_source==DEMO_DIRECT_BT) return 1U;' in s and 'const bool need_tc=(topology&2U)!=0U;' in s)
ck('Source changes reconfigure capture','g_r28_route_restart_pending=1U;' in s)
ck('Audio DMA keeps partial input','Partial-input operation' in s)
ck('Actual 1 Hz UART live telemetry','r10_snapshot("LIVE")' in s)
ck('BT module AT passthrough','s_r10_bt_at_pending' in s and 'G1|BT|AT|UART_SENT' in s)
ck('Serial probes and controls',all(x in s for x in ['WATCH ON','WATCH OFF','DIAG ALL','PINS','BT STATUS','BOARD RAW']))
b=(R/'Core/Src/g1_product_board.c').read_text()
ck('Encoder interrupt and poll fallback','encoder_poll_edges' in b and 'G1_EncoderEdgeIRQ' in b)
ck('MPR121 probes four addresses','G1_TOUCH_ADDR_7BIT' in b and 'HAL_I2C_IsDeviceReady' in b and 'HAL_I2C_IsDeviceReady' in b)
ck('Mic DFSDM callback and queue','HAL_DFSDM_FilterRegConvHalfCpltCallback' in b and 'G1_MicTakeBlock' in b)
ck('Vendor I2C DFSDM linked once','g1_embedded_hal_i2c.inc' not in b and 'g1_embedded_hal_dfsdm.inc' in b and (R/'Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_i2c.c').is_file())
ck('Bundled FreeRTOS sources', all((R/'Middlewares/Third_Party/FreeRTOS/Source'/n).is_file() for n in ['tasks.c','list.c','queue.c','portable/GCC/ARM_CM4F/port.c','portable/MemMang/heap_4.c']))
# Execute native control tests, not just search for expected lines.
gcc=shutil.which('gcc') or shutil.which('cc')
if gcc:
 import tempfile
 with tempfile.TemporaryDirectory() as p:
  cmd=[gcc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(R/'Core/Inc'),str(R/'Tools/test_r10_controls.c'),str(R/'Core/Src/g1_user_controls.c'),'-o',str(Path(p)/'test')]
  x=subprocess.run(cmd,capture_output=True,text=True)
  y=subprocess.run([str(Path(p)/'test')],capture_output=True,text=True) if x.returncode==0 else x
  ck('Compiled and executed C UI tests',x.returncode==0 and y.returncode==0 and 'R16_ANNA_UI_CONTROL_TEST_PASS' in y.stdout)
  if x.returncode or y.returncode:print((x.stderr+y.stderr)[:800])
else:print('SKIP native C unit test (host gcc not found). NOT PASS.')
# Additional regression scripts must execute and succeed.
for script in ('CHECK_PROJECT.py','CHECK_GROUP.py','Tools/g1_preflight.py'):
 q=subprocess.run([sys.executable,str(R/script)],cwd=R,capture_output=True,text=True)
 ck('Regression '+script,q.returncode==0)
 if q.returncode:print(q.stdout[-600:]+q.stderr[-600:])
result={'date_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(), 'source_checked':True,'full_arm_link_verified':False,'custom_pcb_verified':False,'checks':checks, 'passed':sum(c for _,c in checks),'failed':sum(not c for _,c in checks)}
res=R/'Validation/R10_VALIDATION_RESULT.json';res.write_text(json.dumps(result,indent=2))
print('\nR10 SOURCE VALIDATION:',result['passed'],'PASS',result['failed'],'FAIL; FULL ARM LINK & PCB UNVERIFIED')
sys.exit(1 if result['failed'] else 0)
