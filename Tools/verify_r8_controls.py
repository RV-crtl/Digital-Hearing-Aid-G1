#!/usr/bin/env python3
"""Explicit R9 build gates: active/recovery parity, mappings and command routing."""
from pathlib import Path
import re,sys,shutil,subprocess,tempfile
P=Path(__file__).resolve().parents[1]
checks=0
def ck(ok,msg):
 global checks
 assert ok,msg
 checks+=1
for rel in ['Core/Src/demo_app.c','Core/Src/g1_user_controls.c','Core/Inc/g1_user_controls.h']:
 target=P/'Recovery'/rel
 ck(target.exists() and (P/rel).read_bytes()==target.read_bytes(),f'Recovery drift: {rel}')
for rel in ['.project','.cproject','Recovery/.cproject','G1_Product_R26_Adaptive_Audio_CubeIDE119.ioc']:
 ck((P/rel).exists(), f'Missing project file {rel}')
core=(P/'Core/Src/demo_app.c').read_text()
cli=(P/'Core/Src/g1_user_controls.c').read_text()
ck('G1Ui_Command(&s_product_ui,line)' in core,'UART not connected to shared state machine')
ck('G1Ui_Touch(&s_product_ui,e)' in core,'physical touch not connected')
ck('G1Ui_Encoder(&s_product_ui,(int)event.encoder_clicks)' in core,'rotary encoder not connected')
ck('G1|EVENT|via=' in core, 'status change event absent')
ck('G1|UI|STATUS|' in core, 'UI status absent')
ck('G1|ACK|OK|' in core, 'command acknowledgement absent')
ck('g_demo_control.requested_source_mode=s_product_ui.source' in core,'source not routed to audio')
ck('g_demo_control.output_enabled=(uint32_t)(!s_product_ui.muted && s_product_ui.source!=G1_UI_OFF)' in core,'mute not applied to audio')
ck('s_user_gain_q15=s_product_ui.gain_q15;' in core,'physical gain not applied to audio')
ck('G1_MicTakeBlock(g_mono)' in core and 'process_selected_audio(g_mono,3U)' in core,'mic audio path missing')
ck('process_bt_words(g_bt_work_words' in core and 'process_tc_words(g_tc_work_words' in core,'BT/TC routing missing')
ck('output_queue_push(g_demo_processed_mono)' in core,'speaker output not connected')
ck('G1Ui_Percent' in cli and 'G1_UI_MAX_GAIN_Q15' in cli, 'volume safe cap missing')
ck('g1_user_controls.c' not in (P/'Core/Inc/g1_user_controls.h').read_text(), 'bad header composition')
ck('G1_Product_R26_Adaptive_Audio_CubeIDE119' in (P/'.project').read_text(),'stale CubeIDE identity')
# Windows may lack a native host compiler; when present, execute the assertions.
cc=shutil.which('gcc') or shutil.which('clang')
if cc:
 with tempfile.TemporaryDirectory() as temp:
  out=Path(temp)/'ui_test'
  cmd=[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I',str(P/'Core/Inc'),str(P/'Tests/test_g1_user_controls.c'),str(P/'Core/Src/g1_user_controls.c'),'-o',str(out)]
  subprocess.run(cmd,check=True)
  subprocess.run([str(out)],check=True)
else: print('NOTE: Native compiler absent; C assertions are not executed on this machine.')
print(f'R9 COMPLETE USER CONTROL CHECK: {checks} source gates PASS')
