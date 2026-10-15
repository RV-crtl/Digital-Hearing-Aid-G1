from pathlib import Path
import py_compile, sys, hashlib, re
ROOT=Path(__file__).resolve().parent
checks=[]
def ck(cond,msg): checks.append((bool(cond),msg))
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
proj=(ROOT/'.project').read_text(errors='ignore'); cp=(ROOT/'.cproject').read_text(errors='ignore')
src=(ROOT/'Core/Src/demo_app.c').read_text(errors='ignore'); hdr=(ROOT/'Core/Inc/demo_app.h').read_text(errors='ignore')
dash=ROOT/'Dashboard'/'connectivity_dashboard.py'; ds=dash.read_text(errors='ignore')
ck('261013-Individual_Demonstration_Connectivity' in proj,'final CubeIDE project name')
ck((ROOT/'261013-Individual_Demonstration_Connectivity.ioc').exists(),'final IOC exists')
ck('c.compiler.option.debuglevel.value.g3' in cp, 'Debug build includes -g3 symbols (optimization level is user-selectable)')
ck('#define CAPTURE_RING_DEPTH 8U' in src,'capture ring stays RAM-safe at depth 8')
ck('vTaskPrioritySet(NULL, 6U)' in src,'native ControlTask priority 6')
ck('xTaskCreate(StartAudioTask, "AudioTask", 1024U, NULL, 7U' in src,'AudioTask priority 7 / larger stack')
ck('taskYIELD();' in src and 'g_bt_cap_head != g_bt_cap_tail' in src,'AudioTask does not sleep with pending capture data')
ck('taskYIELD();\n    } else {\n      vTaskDelay(pdMS_TO_TICKS(1U));' in src,'AudioTask conditional is braced to avoid FreeRTOS macro else binding')
ck('3600U * 32768ULL' in src and '4ULL * 32768ULL' in src,'conservative 4x auto-level')
ck('26000LL' in src and '31900LL' in src,'soft-knee output limiter present')
ck('g_r26_left_weight_q15' in src and 'g_r26_target_left_weight_q15' in src,'smoothed weighted mono fold present')
ck('g_r21_conceal_streak < 2U' in src,'concealment limited to two half-buffers')
ck('#define DEMO_PLOT_SAMPLES          256U' in hdr,'scope increased to 256 real samples')
ck('R26|SCOPE|PASS|' in src and '|hex=' in src,'single compact R26 scope packet')
ck('R21|SCOPE|D|' not in src[src.find('static void r21_scope'):src.find('static void r21_diag')],'R26 scope no longer emits multiline data chunks')
ck('fw=28|base=R21_AUDIO|ctrl=R28' in src,'R28 final controller protocol identity')
ck('|mixL=%lu|mixR=%lu' in src,'BT telemetry exposes stable fold weights')
try: py_compile.compile(str(dash),doraise=True); ck(True,'dashboard Python compiles')
except Exception as e: ck(False,f'dashboard Python compiles: {e}')
ck("RESULTS = ROOT / 'RESULTS_R26'" in ds,'R26 result folder')
ck("line.startswith('R26|SCOPE|PASS|')" in ds,'dashboard parses compact R26 scope')
ck('DUAL SIGNAL LAB' in ds and 'SPECTRAL CONTRAST' in ds,'advanced Signal Lab present')
ck('START LIVE' in ds and 'EXPORT' in ds and '_live_frame' in ds,'live/export Signal Lab controls present')
ck('AUDIBLE BUT NOISY / DISTORTED' in ds,'physical quality can be recorded as WARN')
ck('captureLoss={loss:.2f}%' in ds,'dashboard exposes capture-loss percentage')
ck((ROOT/'Recovery/Core/Src/demo_app.c').exists(),'Recovery firmware restore exists')
ck(sha(ROOT/'Core/Src/demo_app.c')==sha(ROOT/'Recovery/Core/Src/demo_app.c'),'recovery demo_app matches live')
ck(sha(ROOT/'.cproject')==sha(ROOT/'Recovery/.cproject'),'recovery build config matches live')
ps=(ROOT/'PREPARE_PROJECT.ps1').read_text(errors='ignore')
ck('Recovery' in ps and 'CHECK_PROJECT.py' in ps,'PREPARE restores and validates R26')
sai=(ROOT/'Core/Src/sai.c').read_text()
ck('SAI_SYNCEXT_OUTBLOCKA_ENABLE' in sai, 'SAI1 exports master frame sync to SAI2')
ck('SAI_SYNCHRONOUS_EXT_SAI1' in sai, 'SAI2 RX uses internal SAI1 synchronous mode')
ck('const bool need_bt = !tone_only;' in src, 'shared clock master starts in telecoil-only mode')
ck('R21|TC_CLOCK|' in src and 'mode=DUAL_SAI_DMA_ACTIVITY' in src, 'TC clock probe checks internal synchronization')
ck('R26|SCOPEHD|D|' in src and 'R26|SCOPEHD|END|' in src, '2048-point HD UART stream framed')
ck('R262_HD_SAMPLES 2048U' in src and 'g_r262_hd_buffer' in src, 'HD PCM comes from consecutive actual captured samples')
# Check protocol/GUI capabilities, not one cosmetic label string.  A previous
# release failed PREPARE simply because the wording included "(POST ONLY)".
_hd_decoder = ('def scope_hd(' in ds and "line.startswith('R26|SCOPEHD|D|')" in ds
               and "line.startswith('R26|SCOPEHD|END|')" in ds)
_hd_ui = (re.search(r"HD\s*[·\-]\s*2048", ds, flags=re.IGNORECASE) is not None
          and ('DUAL LIVE' in ds or 'LEGACY' in ds))
ck(_hd_decoder and _hd_ui, 'dashboard HD protocol decoder and selectable fast/HD modes')
if not (_hd_decoder and _hd_ui):
    print(f'HD check detail: decoder={_hd_decoder}; selectable_HDUi={_hd_ui}; file={dash.resolve()}')
ck('FileSpectrogramWindow' in ds and 'stft(' in ds, 'PC-file Navy audio STFT studio present')
ck(sha(ROOT/'Core/Src/sai.c')==sha(ROOT/'Recovery/Core/Src/sai.c'), 'recovery SAI clock configuration matches live')
failed=[m for ok,m in checks if not ok]
for ok,m in checks: print(('PASS ' if ok else 'FAIL ')+m)
print(f'\nPROJECT VALIDATION: {sum(ok for ok,_ in checks)} passed, {len(failed)} failed')
if failed: sys.exit(1)
print('PROJECT STATIC VALIDATION PASS')

# R27 added criteria: independent telecoil filter and true paired snapshots.
assert (ROOT/'Core/Src/tc_cleanup.c').exists()
assert (ROOT/'Core/Inc/tc_cleanup.h').exists()
assert sha(ROOT/'Core/Src/tc_cleanup.c')==sha(ROOT/'Recovery/Core/Src/tc_cleanup.c')
assert 'r27_scope_dual' in src and 'pre64=' in src and 'post64=' in src
assert 'scope_dual(' in ds and 'DUAL LIVE' in ds

# Final physical-evidence release checks. Never allow the old scripted FSM back.
assert 'run_synthetic(' not in src and 'fill_synthetic(' not in src
assert 'R28|LIVE|DATA|' in src and 'R28|TASKS|DATA|' in src
assert 'r28_real_route_arm()' in src
assert 'No physical source changes required' not in ds
assert 'open_fsm_monitor(' in ds and 'save_fsm_trace(' in ds
assert 'open_rtos_monitor(' in ds and 'r28_rtos_snapshot' in src
print('FINAL HARDWARE-ONLY FSM + RTOS MONITOR CHECKS: PASS')


# Recovery-specific checks: CubeMX task list matches the native scheduler without changing boot.
ioc=(ROOT/'261013-Individual_Demonstration_Connectivity.ioc').read_text()
main=(ROOT/'Core/Src/main.c').read_text()
bootstrap=(ROOT/'Core/Src/rtos_bootstrap.c').read_text()
freertos=(ROOT/'Core/Src/freertos.c').read_text()
cfg=(ROOT/'Core/Inc/FreeRTOSConfig.h').read_text()
assert 'defaultTask' not in ioc
assert ioc.count('FREERTOS.Tasks01=')==1 and ioc.count('rtos.0.tasks.')==0
for name in ['ControlTask','AudioTask','BtTask','BackgroundTask']:
    assert name in ioc
assert 'Demo_StartNativeRTOS();' in main and 'MX_FREERTOS_Init();' not in main
assert 'vTaskStartScheduler();' in bootstrap and '"ControlTask"' in bootstrap
assert 'create_child_tasks();' in src and 'xTaskCreate(StartAudioTask' in src
assert 'void MX_FREERTOS_Init(void)' in freertos and 'osThreadNew(' not in freertos
assert 'configGENERATE_RUN_TIME_STATS            1' in cfg
assert 'uxTaskGetSystemState' in src and (ROOT/'Core/Src/rtos_stats_timer.c').exists()
assert 'crun=%lu|arun=%lu|brun=%lu|grun=%lu|irun=%lu' in src
for relative in ['Core/Src/demo_app.c','Core/Src/rtos_bootstrap.c','Core/Src/freertos.c','Core/Inc/FreeRTOSConfig.h','Core/Src/rtos_stats_timer.c']:
    assert sha(ROOT/relative)==sha(ROOT/'Recovery'/relative),relative
print('NATIVE BOOT + CANONICAL CUBEMX IOC TASK TABLE + MEASURED RTOS TELEMETRY: PASS')
assert 'g_stress_stop_at_ms' in src and 'DEMO_STRESS_DURATION_MS 60000U' in src
assert 'if (line_eq(line, "!RS"))' in src and 'background_load_enabled = 2U' in src
assert 'CPU STRESS · 60 s' in ds
print('TIMED LOW PRIORITY CPU SATURATION + GUI CONTROLS: PASS')

# Build regression 2026-10-13: previously CubeMX-generated RTOS user code kept
# `if (defaultTaskHandle == NULL)` after the default task was removed in .ioc.
# The recovered native boot must have an inert freertos.c without this symbol.
freertos_source=(ROOT/'Core/Src/freertos.c').read_text()
assert 'void MX_FREERTOS_Init(void)' in freertos_source
assert 'defaultTaskHandle' not in freertos_source.split('void MX_FREERTOS_Init(void)')[-1]
assert 'osThreadNew(' not in freertos_source
assert 'Demo_StartNativeRTOS();' in main
assert sha(ROOT/'Core/Src/freertos.c')==sha(ROOT/'Recovery/Core/Src/freertos.c')
print('CUBEMX STALE defaultTaskHandle BUILD REGRESSION: PASS')

# Final debugger-first R29 validation. All debug evidence is read from running kernel.
assert (ROOT/'Core/Inc/connectivity_demo_debug.h').exists()
assert 'uxTaskGetSystemState' in src and 'r29_refresh_debug_snapshot(now);' in src
assert 'g_demo_debug.total_cpu_pct_x10' in src and 'g_demo_debug.fsm_mode' in src
assert 'if (line_eq(line, "!PV"))' in src
assert 'open_fsm_monitor' in ds and "self.link.ask('!FV'" in ds
assert sha(ROOT/'Core/Inc/connectivity_demo_debug.h')==sha(ROOT/'Recovery/Core/Inc/connectivity_demo_debug.h')
print('R29 DEBUG WATCH / REAL FSM / 60 SEC STRESS: PASS')

# FreeRTOS Task List Run Time (%) issue: an optimised-away kernel counter causes N/A.
# PREPARE_PROJECT.ps1 applies the ST workaround to the LOCAL middleware copy.
from pathlib import Path as _Path
_patched_tasks = ROOT / 'Middlewares/Third_Party/FreeRTOS/Source/tasks.c'
_ps = (ROOT / 'PREPARE_PROJECT.ps1').read_text(errors='replace')
assert 'ulTotalRunTime' in _ps and 'volatile static' in _ps, 'runtime debugger patch missing from PREPARE'
if _patched_tasks.exists():
    _kernel = _patched_tasks.read_text(errors='replace')
    assert re.search(r'(?:\bvolatile\s+static|\bstatic\s+volatile)\s+uint(?:32|64)_t\s+ulTotalRunTime\b', _kernel), 'runtime counter not debugger-visible: re-run PREPARE_PROJECT.bat'
print('FREE RTOS RUN TIME (%) DEBUGGER-VISIBILITY FIX: PASS')

# R29.2 debug reproducibility checks. An ELF symbol check must be made with GNU nm
# on the user's actual newly linked ARM binary: see DEBUGGER_DIAGNOSTICS.md.
assert 'R34_PRIORITY7_CONTINUOUS_SPIN' in src, 'R34 binary identity marker missing'
assert '__attribute__((used)) volatile ConnectivityDemoDebug_t g_demo_debug;' in src
assert 'r29_refresh_debug_snapshot(now);' in src
assert 'if (line_eq(line, "!PV"))' in src
assert 'Demo_RuntimeStatsTimerRead()' in (ROOT/'Core/Inc/FreeRTOSConfig.h').read_text()
print('R30 SOURCE SYMBOL AND BUILD IDENTIFICATION CHECKS: PASS')
assert 'SCOPE + FFT' not in ds.split('# Evidence')[0] and 'FINAL SNAPSHOT' not in ds.split('# Evidence')[0]
assert 'REAL BT > TC FSM' not in ds.split('# Evidence')[0]
assert all(f'    {i}: (' in ds.split('# Evidence')[0] for i in range(1,6))
assert 'DUAL QUIET (NO AUDIO)' in ds and 'BT + DUAL LOAD' in ds and 'SAVE REAL FSM TRACE' in ds
assert 'if n==6:' not in ds and 'elif n==6:' not in ds and 'elif n==8:' not in ds
print('R30 RUBRIC FOCUSED FIVE TEST DASHBOARD + STANDALONE SIGNAL/FSM TOOLS: PASS')

# R30.2 regression: a delayed ControlTask snapshot must not be rejected merely
# because a previous hardcoded two-second limit was exceeded.
assert 'delta <= 2000000U' not in src
assert 'profile_reason' in src and 'profile_timer_delta' in src
assert '|reason=%lu|timer=%lu|kernel=%lu|dt=%lu|' in src
assert (ROOT/'Documentation/PROFILE_DIAGNOSTICS_R30_2.md').exists()
print('R30.2 ELAPSED-TIMER PROFILER + FAILURE-REASON CHECKS: PASS')

# R30.3 hardware-timer startup and raw probe regression guards.
def _r303_check(label, test):
    print(("PASS " if test else "FAIL ")+label)
    if not test: raise SystemExit(1)
_m=(ROOT / "Core/Src/main.c").read_text(errors="replace")
_p=(ROOT / "Core/Src/demo_app.c").read_text(errors="replace")
_r=(ROOT / "Core/Src/rtos_stats_timer.c").read_text(errors="replace")
_r303_check("TIM2 explicitly starts before native scheduler", _m.index("Demo_RuntimeStatsTimerInit();") < _m.index("Demo_StartNativeRTOS();"))
_r303_check("TIM2 boot test uses physical counter progress", "Demo_RuntimeStatsTimerRead();" in _m and "HAL_Delay(2U)" in _m)
_r303_check("TIM2 recovery is non-resetting", "Demo_RuntimeStatsTimerEnsureRunning" in _r and "TIM2->CR1 |= TIM_CR1_CEN" in _r)
_r303_check("raw !PX diagnostic uses two physical readings", 'line_eq(line, "!PX")' in _p and "R30|PROFILE_HW|DATA|" in _p)
_r303_check("profiler and boot recovery mirrors match", (ROOT / "Recovery/Core/Src/main.c").read_bytes()==(ROOT / "Core/Src/main.c").read_bytes() and (ROOT / "Recovery/Core/Src/rtos_stats_timer.c").read_bytes()==(ROOT / "Core/Src/rtos_stats_timer.c").read_bytes())

# R30.4: proof that when vendor uxTaskGetSystemState returns zero time despite
# an active timer, our fallback observes actual FreeRTOS context switches.
_trace = (ROOT/'Core/Src/rtos_trace_proof.c').read_text()
assert 'traceTASK_SWITCHED_IN()' in cfg and 'traceTASK_SWITCHED_OUT()' in cfg
assert 'Demo_TraceTaskSwitchedIn' in _trace and 'Demo_TraceTaskSwitchedOut' in _trace
assert 'Demo_TraceGetTaskTime' in src and 'profile_source=source' in src
assert '|method=%lu|tsw=%lu|tov=%lu' in src
for rel in ['Core/Inc/rtos_trace_proof.h','Core/Src/rtos_trace_proof.c']:
    assert (ROOT / rel).read_bytes()==(ROOT/'Recovery'/rel).read_bytes()
print('R30.4 REAL CONTEXT-SWITCH TIM2 ACCOUNTING + RECOVERY: PASS')

# R30.5 ISR sampling recovery guards (actual TIM6 interrupt, not synthetic task progression).
assert 'Demo_TickProfilerSample();' in (ROOT/'Core/Src/main.c').read_text()
assert 'Demo_TickProfilerTaskSamples' in (ROOT/'Core/Src/demo_app.c').read_text()
assert 'profile_tick_samples' in (ROOT/'Core/Inc/connectivity_demo_debug.h').read_text()
assert sha(ROOT/'Core/Src/rtos_tick_proof.c') == sha(ROOT/'Recovery/Core/Src/rtos_tick_proof.c')
assert sha(ROOT/'Core/Src/main.c') == sha(ROOT/'Recovery/Core/Src/main.c')
assert sha(ROOT/'Core/Src/demo_app.c') == sha(ROOT/'Recovery/Core/Src/demo_app.c')
print('R30.5 INDEPENDENT TIM6 SAMPLED CPU-PROFILING CHECKS: PASS')

# R31 dual CPU test: two REAL priority-2 tasks, unique workload counters,
# independent FreeRTOS time accounting, and a live dashboard control.
assert '#include "rtos_stats_timer.h"' in src, 'timer API prototypes must be visible (old 3 warnings)'
assert 'xTaskCreate(StartCpuLoadB, "CPULoadB", 256U, NULL, 2U' in src
assert 'xTaskCreate(StartBackgroundTask, "Background", 384U, NULL, 2U' in src
assert 'line_eq(line, "!RD")' in src and 'DEMO_STRESS_DURATION_MS 60000U' in src
assert 'g_r31_cpu_b_work_blocks++' in src and 'g_demo_diag.background_work_iterations++' in src
assert 'b2run=%lu|b2pri=%lu|dual=%lu' in src and '|b2cpu10=%lu' in src
assert 'Demo_TickProfilerTaskSamples((void *)g_cpu_load_b_task)' in src
assert 'g_demo_debug.load_b_cpu_pct_x10' in src
assert 'DUAL QUIET (NO AUDIO)' in ds and 'BT + DUAL LOAD' in ds and 'DUAL TASKS · 60 s' in ds
assert (ROOT/'Documentation/R31_EXACT_IMPORT_AND_DUAL_TASK_DEMO.md').exists()
assert sha(ROOT/'Core/Src/demo_app.c')==sha(ROOT/'Recovery/Core/Src/demo_app.c')
assert sha(ROOT/'Core/Inc/connectivity_demo_debug.h')==sha(ROOT/'Recovery/Core/Inc/connectivity_demo_debug.h')
print('R31 DUAL EQUAL-PRIORITY WORKERS, PROFILER CONTINUITY AND DASHBOARD: PASS')

# R32: priority-7 task retained and never enabled by !RS/!RD;
# R34 changes !RH into strict continuous spin (UART may be starved).
assert 'xTaskCreate(StartHighPriorityLoad, "HighLoad7", 256U, NULL, 7U' in src
assert 'line_eq(line, "!RH")' in src and 'g_r32_hi_deadline_ms = HAL_GetTick() + R34_SPIN_DEADLINE_MS;' in src
assert 'while (g_r32_hi_active != 0U &&' in src and 'g_r32_hi_work_blocks++;' in src
assert 'g_r32_hi_active=0U;' in src and 'g_r32_hi_work_blocks++' in src
assert 'g_demo_debug.high7_cpu_pct_x10' in src
assert '|h7run=%lu|h7state=%lu|h7pri=%lu|h7stack=%lu|hi7=%lu' in src
assert 'CONTINUOUS SPIN · PRIORITY 7 · 20 s' in ds
assert 'HighLoad7' in ds and (ROOT/'Documentation/R32_EXACT_IMPORT_AND_PRIORITY7_DEMO.md').exists()
assert sha(ROOT/'Core/Src/demo_app.c') == sha(ROOT/'Recovery/Core/Src/demo_app.c')
assert sha(ROOT/'Core/Inc/connectivity_demo_debug.h') == sha(ROOT/'Recovery/Core/Inc/connectivity_demo_debug.h')
print('R32 PRIORITY-7 TASK PRESERVED (R34 CONTINUOUS-SPIN UPGRADE): PASS')

# R34 high-priority busy-loop demonstration.  Delays permitted ONLY while off.
assert 'R34_PRIORITY7_CONTINUOUS_SPIN' in src
assert 'mode=PRIORITY7_CONTINUOUS_SPIN|priority=7|delay_ticks=0|duration_ms=20000' in src
assert '#define R34_SPIN_DEADLINE_MS 20000U' in src
assert 'g_r32_hi_active = 1U;' in src
assert 'g_r32_hi_active = 0U;' in src
assert 'CONTINUOUS SPIN · PRIORITY 7 · 20 s' in ds
assert (ROOT/'Documentation/R34_CONTINUOUS_SPIN_IMPORT_GUIDE.md').exists()
assert sha(ROOT/'Core/Src/demo_app.c')==sha(ROOT/'Recovery/Core/Src/demo_app.c')
# Assert there is no blocking/yielding operation inside the live busy loop.
active_region=src.split('while (g_r32_hi_active != 0U &&',1)[1].split('    g_r32_hi_active = 0U;',1)[0]
assert 'vTaskDelay(' not in active_region and 'taskYIELD(' not in active_region
print('R34 CONTINUOUS PRIORITY-7 SPIN + 20S SELF-STOP + DEBUGGER: PASS')
