$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Dest = Join-Path $Root "Middlewares\Third_Party\FreeRTOS"
$Need = Join-Path $Dest "Source\tasks.c"
Write-Host "261013-Individual_Demonstration_Connectivity preparation" -ForegroundColor Cyan
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

# Always restore the validated audio/application files after any CubeMX Generate Code.
Write-Host "Restoring final connectivity application/audio files..." -ForegroundColor Yellow
$GoldenSrc=Join-Path $Root "Recovery\Core\Src"
$CoreSrc=Join-Path $Root "Core\Src"
Get-ChildItem $GoldenSrc -File | ForEach-Object { Copy-Item $_.FullName (Join-Path $CoreSrc $_.Name) -Force }
Get-ChildItem (Join-Path $Root "Recovery\Core\Inc") -File | ForEach-Object { Copy-Item $_.FullName (Join-Path (Join-Path $Root "Core\Inc") $_.Name) -Force }
Copy-Item (Join-Path $Root "Recovery\.cproject") (Join-Path $Root ".cproject") -Force
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

$py = Get-Command py -ErrorAction SilentlyContinue
if ($py) { & py -3 (Join-Path $Root "CHECK_PROJECT.py") }
else { & python (Join-Path $Root "CHECK_PROJECT.py") }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$groupCheck=Join-Path $Root "CHECK_GROUP.py"
if ($py) { & py -3 $groupCheck }
else { & python $groupCheck }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host ""; Write-Host "FINAL PREPARE PASS" -ForegroundColor Green
Write-Host "If you need CubeMX Generate Code: GENERATE FIRST, then rerun PREPARE_PROJECT.bat, then Clean/Build/Flash."
