$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Dest = Join-Path $Root "Middlewares\Third_Party\FreeRTOS"
$Need = Join-Path $Dest "Source\tasks.c"
Write-Host "261104-Digital_Hearing_Aid preparation" -ForegroundColor Cyan
Write-Host "Project root: $Root" -ForegroundColor Cyan
Write-Host "Source check: $(Join-Path $Root 'CHECK_PROJECT.py')" -ForegroundColor Cyan

if (-not (Test-Path $Need)) {
  $Candidates = @(
    "$env:USERPROFILE\STM32Cube\Repository\STM32Cube_FW_L4_V1.18.2\Middlewares\Third_Party\FreeRTOS",
    "$env:USERPROFILE\STM32Cube\Repository\STM32Cube_FW_L4_V1.18.1\Middlewares\Third_Party\FreeRTOS",
    "$env:USERPROFILE\STM32Cube\Repository\STM32Cube_FW_L4_V1.18.0\Middlewares\Third_Party\FreeRTOS",
    "C:\ST\STM32Cube\Repository\STM32Cube_FW_L4_V1.18.2\Middlewares\Third_Party\FreeRTOS"
  )
  $Source = $null
  foreach ($c in $Candidates) { if (Test-Path (Join-Path $c "Source\tasks.c")) { $Source=$c; break } }
  if (-not $Source) {
    $Repo=Join-Path $env:USERPROFILE "STM32Cube\Repository"
    if (Test-Path $Repo) {
      $Source = Get-ChildItem $Repo -Directory -Filter "STM32Cube_FW_L4_V1.18*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "Middlewares\Third_Party\FreeRTOS" } |
        Where-Object { Test-Path (Join-Path $_ "Source\tasks.c") } |
        Select-Object -First 1
    }
  }
  if (-not $Source) {
    Write-Host "ERROR: STM32CubeL4 V1.18.x FreeRTOS package not found." -ForegroundColor Red
    Write-Host "Install the STM32Cube FW_L4 V1.18.x package in CubeIDE/CubeMX, then rerun PREPARE_PROJECT.bat."
    exit 2
  }
  Write-Host "Copying FreeRTOS from $Source" -ForegroundColor Yellow
  if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
  New-Item -ItemType Directory -Force -Path (Split-Path $Dest -Parent) | Out-Null
  Copy-Item $Source $Dest -Recurse -Force
}

# ST UM2609 debugger profiling workaround - applies ONLY to the copied local FreeRTOS.
# Keep the DSP/audio firmware at its existing optimization level. The FreeRTOS
# task scheduler implementation is compiled at -O0 in Debug so CubeIDE can
# locate the ulTotalRunTime counter, and that static counter is retained as a
# named volatile object. This does NOT guarantee support from every IDE plugin.
$kernelFile = Join-Path $Dest "Source\tasks.c"
$kernelText = [System.IO.File]::ReadAllText($kernelFile)
$kernelPattern = '(?m)(\b(?:volatile\s+static|static\s+volatile|static)\s+uint(?:32|64)_t\s+ulTotalRunTime)(\s+__attribute__\(\(used\)\))?(\s*)(?=[=;])'
if (-not [regex]::IsMatch($kernelText, $kernelPattern)) {
  Write-Host 'ERROR: FreeRTOS tasks.c has an unknown ulTotalRunTime declaration.' -ForegroundColor Red
  exit 4
}
# Idempotent on repeat runs; no duplicate volatile or GNU used attribute.
$kernelText = [regex]::Replace($kernelText,
  '\bstatic\s+volatile\s+(uint(?:32|64)_t)\s+ulTotalRunTime\b',
  'volatile static $1 ulTotalRunTime')
$kernelText = [regex]::Replace($kernelText,
  '(?<!volatile )\bstatic\s+(uint(?:32|64)_t)\s+ulTotalRunTime\b',
  'volatile static $1 ulTotalRunTime')
$kernelText = [regex]::Replace($kernelText,
  '(\bvolatile\s+static\s+uint(?:32|64)_t\s+ulTotalRunTime)(?!\s+__attribute__\(\(used\)\))(?=\s*[=;])',
  '$1 __attribute__((used))')
# GCC scoped pragma instead of changing the application's FFT/audio optimization.
$startMarker = '/* R29.2: DBG FREE RTOS TASKS.C OPTIMISATION START */'
$endMarker = '/* R29.2: DBG FREE RTOS TASKS.C OPTIMISATION END */'
if (-not $kernelText.Contains($startMarker)) {
  $prefix = "$startMarker`r`n#if defined(DEBUG) && defined(__GNUC__)`r`n#pragma GCC push_options`r`n#pragma GCC optimize (`"O0`")`r`n#endif`r`n"
  $suffix = "`r`n$endMarker`r`n#if defined(DEBUG) && defined(__GNUC__)`r`n#pragma GCC pop_options`r`n#endif`r`n"
  $kernelText = $prefix + $kernelText + $suffix
}
[System.IO.File]::WriteAllText($kernelFile,$kernelText,(New-Object System.Text.UTF8Encoding($false)))
if (-not ($kernelText.Contains('ulTotalRunTime __attribute__((used))') -and $kernelText.Contains($startMarker))) {
  Write-Host 'ERROR: FreeRTOS debug patch could not be verified.' -ForegroundColor Red
  exit 5
}
Write-Host 'FreeRTOS tasks.c: retained runtime counter + Debug-only O0 profiling workaround' -ForegroundColor Green

# CubeMX regeneration can remove HAL/DFSDM source files that are not represented
# in the legacy .ioc. Restore a snapshot bundled with this project before build.
# This fixes fatal stm32l4xx_hal_dfsdm.h not found AND later undefined DFSDM symbols.
Write-Host "Restoring complete HAL/CMSIS driver snapshot..." -ForegroundColor Yellow
$GoldenDrivers = Join-Path $Root "Recovery\Drivers"
$LiveDrivers = Join-Path $Root "Drivers"
if (-not (Test-Path (Join-Path $GoldenDrivers "STM32L4xx_HAL_Driver\Inc\stm32l4xx_hal_dfsdm.h")) -or
    -not (Test-Path (Join-Path $Root "Recovery\Core\Inc\g1_embedded_hal_dfsdm.inc"))) {
    Write-Host "ERROR: packaged HAL/DFSDM driver recovery snapshot is absent." -ForegroundColor Red
    exit 7
}
Get-ChildItem $GoldenDrivers -File -Recurse | ForEach-Object {
    $rel = $_.FullName.Substring($GoldenDrivers.Length).TrimStart('\')
    $target = Join-Path $LiveDrivers $rel
    $targetDir = Split-Path -Parent $target
    if (-not (Test-Path $targetDir)) { New-Item -ItemType Directory -Force -Path $targetDir | Out-Null }
    Copy-Item $_.FullName $target -Force
}
# R15: One HAL implementation per symbol. CubeIDE 1.19 compiles vendor
# I2C and I2CEx sources normally from Drivers/Src. The microphone's
# DFSDM implementation remains embedded in g1_product_board.c ONLY.
foreach ($driver in @('stm32l4xx_hal_dfsdm.c')) {
  $candidate = Join-Path $LiveDrivers ("STM32L4xx_HAL_Driver\Src\" + $driver)
  if (Test-Path $candidate) { Remove-Item $candidate -Force }
}
foreach ($driver in @('stm32l4xx_hal_i2c.c','stm32l4xx_hal_i2c_ex.c')) {
  $candidate = Join-Path $LiveDrivers ("STM32L4xx_HAL_Driver\Src\" + $driver)
  if (-not (Test-Path $candidate)) { throw "Missing required standalone I2C driver $driver" }
}
Write-Host "HAL ownership: I2C from Drivers/Src (once); microphone DFSDM from board TU (once)." -ForegroundColor Green
# R17: touch software fallback lives entirely in g1_product_board.c.


# CubeIDE 1.19/CubeMX 6.15 Generate Code creates additional translation
# units and DMA ISR handlers that conflict with the manually validated
# microphone + speaker implementations.  Restore exactly one owner for each.
# These generated files are NOT part of the verified recovery source tree.
foreach ($stale in @('Core\Src\dfsdm.c','Core\Inc\dfsdm.h',
                     'Core\Src\i2c.c','Core\Inc\i2c.h')) {
  $candidate=Join-Path $Root $stale
  if (Test-Path -LiteralPath $candidate) {
    Write-Host "Removing unowned generated source: $stale" -ForegroundColor Yellow
    Remove-Item -LiteralPath $candidate -Force
  }
}
# Do not preserve CubeIDE-generated missing DMA handle references: golden
# stm32l4xx_it.c is restored in the next step.  DMA1 CH4 belongs to
# g1_product_board.c, DMA1 CH7 belongs to speaker_output.c.

# Always restore the validated audio/application files after any CubeMX Generate Code.
Write-Host "Restoring final connectivity application/audio files..." -ForegroundColor Yellow
$GoldenSrc=Join-Path $Root "Recovery\Core\Src"
$CoreSrc=Join-Path $Root "Core\Src"
Get-ChildItem $GoldenSrc -File -Filter '*.c' | ForEach-Object { Copy-Item $_.FullName (Join-Path $CoreSrc $_.Name) -Force }
# One visible main.c. Non-compilable golden snapshot restores vendor code generation safely.
Copy-Item (Join-Path $GoldenSrc 'main.c.golden') (Join-Path $CoreSrc 'main.c') -Force
Get-ChildItem (Join-Path $Root "Recovery\Core\Inc") -File | ForEach-Object { Copy-Item $_.FullName (Join-Path (Join-Path $Root "Core\Inc") $_.Name) -Force }
Copy-Item (Join-Path $Root "Recovery\.cproject") (Join-Path $Root ".cproject") -Force
# R26.7: CubeMX regeneration can silently restore a RAM1-only linker or
# startup image. Restore both files alongside FreeRTOSConfig and freertos.c.
foreach ($rel in @('STM32L476RGTX_FLASH.ld',
                   'Core\Startup\startup_stm32l476rgtx.s')) {
  $golden = Join-Path (Join-Path $Root 'Recovery') $rel
  $active = Join-Path $Root $rel
  if (-not (Test-Path -LiteralPath $golden)) { throw "Missing R26.7 SRAM2 recovery file: $rel" }
  $activeParent = Split-Path -Parent $active
  if (-not (Test-Path -LiteralPath $activeParent)) { New-Item -ItemType Directory -Force -Path $activeParent | Out-Null }
  Copy-Item -LiteralPath $golden -Destination $active -Force
}
Write-Host 'SRAM2 heap: startup/linker/FreeRTOS configuration recovered' -ForegroundColor Green
# Fail preparation early if any of the four coordinated memory changes disappear.
$heapCfg  = Get-Content (Join-Path $Root 'Core\Inc\FreeRTOSConfig.h') -Raw
$heapImpl = Get-Content (Join-Path $Root 'Core\Src\freertos.c') -Raw
$heapLink = Get-Content (Join-Path $Root 'STM32L476RGTX_FLASH.ld') -Raw
$heapBoot = Get-Content (Join-Path $Root 'Core\Startup\startup_stm32l476rgtx.s') -Raw
if (($heapCfg -notmatch 'configAPPLICATION_ALLOCATED_HEAP\s+1') -or
    ($heapImpl -notmatch 'ucHeap\[configTOTAL_HEAP_SIZE\]') -or
    ($heapLink -notmatch '\.freertos_heap_ram2') -or
    ($heapBoot -notmatch '_sram2_heap_start')) {
    throw 'R26.7 SRAM2 heap initialization incomplete; refusing to compile'
}

# Reject duplicate ISRs or generated peripheral files early.
$pyCheck=Get-Command py -ErrorAction SilentlyContinue
if ($pyCheck) { & py -3 (Join-Path $Root 'Tools\r14_build_guard.py') }
else { & python (Join-Path $Root 'Tools\r14_build_guard.py') }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Guard against a generated freertos.c still containing stale CubeMX user code.
$rtosCompat = Get-Content (Join-Path $CoreSrc "freertos.c") -Raw
if (($rtosCompat -notmatch 'void MX_FREERTOS_Init\(void\)') -or
    ($rtosCompat -match 'if\s*\(\s*defaultTaskHandle')) {
  Write-Host "ERROR: stale FreeRTOS-generated source survived recovery." -ForegroundColor Red
  exit 3
}
Write-Host "FreeRTOS compatibility source restored (native scheduler remains authoritative)." -ForegroundColor Green

# Stop here if CubeMX silently removed the instrumentation after generation.
$cfg = Get-Content (Join-Path $Root "Core\Inc\FreeRTOSConfig.h") -Raw
$main = Get-Content (Join-Path $Root "Core\Src\main.c") -Raw
if (($cfg -notmatch '#define configGENERATE_RUN_TIME_STATS\s+1') -or
    ($cfg -notmatch 'traceTASK_SWITCHED_OUT\(\)') -or
    ($main -notmatch 'Demo_TickProfilerSample\(\)') -or
    (-not (Test-Path (Join-Path $Root "Core\Src\rtos_tick_proof.c")))) {
    Write-Host "ERROR: profiler files missing/overwritten; rerun PREPARE after Generate Code." -ForegroundColor Red
    exit 6
}
Write-Host "PROFILE CONFIG VERIFIED: runtime hooks and TIM6 fallback restored AFTER CubeMX." -ForegroundColor Green
foreach ($name in @('dfsdm')) {
  if (-not (Test-Path (Join-Path $Root ("Core\Inc\g1_embedded_hal_" + $name + ".inc")))) {
    throw "Missing embedded HAL module: $name"
  }
}
if (-not (Test-Path (Join-Path $Root 'Core\Inc\stm32l4xx_hal_dfsdm.h'))) {
    throw 'Missing DFSDM public header'
}

$py = Get-Command py -ErrorAction SilentlyContinue
if ($py) { & py -3 (Join-Path $Root "CHECK_PROJECT.py") }
else { & python (Join-Path $Root "CHECK_PROJECT.py") }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$groupCheck=Join-Path $Root "CHECK_GROUP.py"
if ($py) { & py -3 $groupCheck }
else { & python $groupCheck }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Check the active project name: never silently build an old workspace copy.
if ($py) { & py -3 (Join-Path $Root "Tools\verify_active_project.py") }
else { & python (Join-Path $Root "Tools\verify_active_project.py") }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Check both the DFSDM header and DFSDM implementation before IDE import/build.
if ($py) { & py -3 (Join-Path $Root "Tools\verify_build_bundle.py") }
else { & python (Join-Path $Root "Tools\verify_build_bundle.py") }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Reject broken FreeRTOS include ordering after restoring recovery copies.
# A syntactically incorrect queue.h-before-FreeRTOS.h source previously yielded
# hundreds of misleading "storage class specified for parameter" errors.
Write-Host "Checking FreeRTOS header order in live and recovery sources..." -ForegroundColor Cyan
if ($py) { & py -3 (Join-Path $Root "Tools\\g1_preflight.py") }
else { & python (Join-Path $Root "Tools\\g1_preflight.py") }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Both touch HAL headers and microphone PDM clock must remain in recovered board file.
$boardSource = Get-Content (Join-Path $Root "Core\Src\g1_product_board.c") -Raw
if (($boardSource -notmatch 'stm32l4xx_hal_i2c.h') -or
    ($boardSource -notmatch 'stm32l4xx_hal_i2c_ex.h') -or
    ($boardSource -notmatch 'OutputClock\.Activation=ENABLE')) {
    throw "Full-system board regression: I2C HAL includes or microphone PDM clock enable missing"
}
# R10: require the module-enabling prelude in the active + golden board sources.
foreach ($boardPath in @((Join-Path $Root 'Core\Src\g1_product_board.c'),
                          (Join-Path $Root 'Recovery\Core\Src\g1_product_board.c'))) {
  $boardText = Get-Content $boardPath -Raw
  if (($boardText -notmatch '#define HAL_I2C_MODULE_ENABLED') -or
      ($boardText -notmatch '#define HAL_DFSDM_MODULE_ENABLED') -or
      ($boardText -match '#error "Enable HAL_I2C_MODULE_ENABLED')) {
    throw "R7 board HAL module configuration is missing or stale: $boardPath"
  }
}
Write-Host "R17 standalone HAL I2C and embedded microphone DFSDM verified" -ForegroundColor Green
# Current-release gate replaces obsolete R8/R19/R20 project-name and UI-layout tests.
# Those historic tests expected the retired G1_Product_R26_* .ioc and could fail
# while the current physical driver, RTOS and audio pipeline were healthy.
if ($py) { & py -3 (Join-Path $Root 'Tools\verify_261104_release.py') }
else { & python (Join-Path $Root 'Tools\verify_261104_release.py') }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host ""; Write-Host "FINAL PREPARE PASS - 261104 project" -ForegroundColor Green
Write-Host "If you need CubeMX Generate Code: GENERATE FIRST, then rerun PREPARE_PROJECT.bat, then Clean/Build/Flash."

