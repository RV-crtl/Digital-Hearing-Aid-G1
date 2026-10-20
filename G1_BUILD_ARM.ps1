# G1-only headless STM32CubeIDE build. No flashing or PD5 operations.
[CmdletBinding()]
param(
  [string]$CubeIDEExe = "",
  [ValidateSet("Debug","Release")][string]$Configuration = "Debug",
  [string]$Workspace = "",
  [switch]$SkipPrepare
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Name = "261013-Individual_Demonstration_Connectivity"
if (-not (Test-Path (Join-Path $Root ".project"))) { throw "Not a G1 project: .project missing" }
$Actual = (Select-Xml -Path (Join-Path $Root ".project") -XPath "/projectDescription/name").Node.InnerText
if ($Actual -ne $Name -or -not (Test-Path (Join-Path $Root "Core\Inc\dha_build_mode.h"))) {
  throw "Refusing to operate outside the expected G1 source project."
}
Set-Location $Root

if (-not $Workspace) {
  $BaseDir = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { $env:TEMP }
  $Workspace = Join-Path $BaseDir "G1_Headless_STM32_Workspace"
}
$Workspace = [IO.Path]::GetFullPath($Workspace)
if ($Workspace.StartsWith($Root + [IO.Path]::DirectorySeparatorChar,
                          [StringComparison]::OrdinalIgnoreCase)) {
  throw "Headless Eclipse workspace must be outside the repository directory."
}
if (-not $CubeIDEExe) {
  $found=Get-Command stm32cubeidec.exe -ErrorAction SilentlyContinue
  if ($found) { $CubeIDEExe=$found.Source }
}
if (-not $CubeIDEExe -and (Test-Path "C:\ST")) {
  $CubeIDEExe = Get-ChildItem "C:\ST" -Directory -Filter "STM32CubeIDE*" |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "STM32CubeIDE\stm32cubeidec.exe" } |
    Where-Object { Test-Path $_ -PathType Leaf } |
    Select-Object -First 1
}
if (-not $CubeIDEExe -or -not (Test-Path $CubeIDEExe -PathType Leaf)) {
  throw "STM32CubeIDE command not found. Pass -CubeIDEExe with the full stm32cubeidec.exe path."
}
$CubeIDEExe=[IO.Path]::GetFullPath($CubeIDEExe)
if (-not $SkipPrepare) {
  Write-Host "Preparing G1 source and local FreeRTOS (CubeIDE should be closed)..." -ForegroundColor Cyan
  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "PREPARE_PROJECT.ps1")
  if ($LASTEXITCODE -ne 0) { throw "PREPARE failed ($LASTEXITCODE). Read its error; no flash attempted." }
}
if (-not (Test-Path (Join-Path $Root "Middlewares\Third_Party\FreeRTOS\Source\tasks.c"))) {
  throw "FreeRTOS is absent. Install STM32Cube FW_L4 V1.18.x, then rerun PREPARE."
}
New-Item -ItemType Directory -Force -Path $Workspace | Out-Null
$Results=Join-Path $Root "Validation\results"
New-Item -ItemType Directory -Force -Path $Results | Out-Null
$Log=Join-Path $Results "g1_arm_build.log"
$Errors=Join-Path $Results "g1_arm_build_stderr.log"
$Target="$Name/$Configuration"
$BuildArguments='--launcher.suppressErrors -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -no-indexer -data "{0}" -import "{1}" -cleanBuild "{2}" -markerType cdt -printErrorMarkers' -f $Workspace,$Root,$Target
Write-Host "CubeIDE headless Clean/Build $Target (NEVER FLASHES)" -ForegroundColor Cyan
$BuildStartUtc=(Get-Date).ToUniversalTime()
$Process=Start-Process -FilePath $CubeIDEExe -ArgumentList $BuildArguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $Log -RedirectStandardError $Errors
if ($Process.ExitCode -ne 0) {
  throw "CubeIDE failed ($($Process.ExitCode)). Inspect $Log and $Errors; do NOT flash."
}
if ((Get-Content $Log -Raw) -match '(?m)^\s*(?:fatal )?error:\s') {
  throw "CubeIDE reports compiler errors despite zero exit. Inspect $Log."
}
$Folder=Join-Path $Root $Configuration
$Elf=Get-ChildItem -Path $Folder -Filter "*.elf" -File -Recurse -ErrorAction SilentlyContinue |
  Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if ($null -eq $Elf -or $Elf.Length -lt 4096) { throw "Nonempty ELF not produced. No ARM build PASS." }
if($Elf.LastWriteTimeUtc -lt $BuildStartUtc.AddSeconds(-3)) {
  throw "ELF is older than this headless build: $($Elf.FullName). Refusing stale binary PASS."
}
$Bytes=[IO.File]::ReadAllBytes($Elf.FullName)
if ($Bytes.Length -lt 64 -or $Bytes[0] -ne 127 -or $Bytes[1] -ne 69 -or
    $Bytes[2] -ne 76 -or $Bytes[3] -ne 70 -or $Bytes[18] -ne 40 -or $Bytes[19] -ne 0) {
  throw "ELF header is not for ARM Cortex-M. Do not flash."
}
$Sha=(Get-FileHash -LiteralPath $Elf.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$FirmwareCommit="UNKNOWN"
if (Get-Command git -ErrorAction SilentlyContinue) {
  $GitSha=(& git -C $Root rev-parse HEAD 2>$null)
  if ($LASTEXITCODE -eq 0 -and $GitSha -match '^[0-9a-fA-F]{40}
  status="ARM_CLEAN_BUILD_PASS"; project=$Name; configuration=$Configuration;
  firmware_commit=$FirmwareCommit;
  time_utc=(Get-Date).ToUniversalTime().ToString("o");
  executable_path=$Elf.FullName; executable_sha256=$Sha; executable_bytes=$Elf.Length;
  cubide_path=$CubeIDEExe; stdout_log=$Log; stderr_log=$Errors;
  physical_tests_performed=$false; device_flashed=$false
}
$Report | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $Results "g1_arm_build.json")
Write-Host "G1 ARM ELF successfully built: $($Elf.FullName)" -ForegroundColor Green
Write-Warning "Build success is NOT evidence of physical audio performance or custom PCB readiness."
) {
    $FirmwareCommit=[string]$GitSha
  }
}
$Report=[ordered]@{
  status="ARM_CLEAN_BUILD_PASS"; project=$Name; configuration=$Configuration;
  time_utc=(Get-Date).ToUniversalTime().ToString("o");
  executable_path=$Elf.FullName; executable_sha256=$Sha; executable_bytes=$Elf.Length;
  cubide_path=$CubeIDEExe; stdout_log=$Log; stderr_log=$Errors;
  physical_tests_performed=$false; device_flashed=$false
}
$Report | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $Results "g1_arm_build.json")
Write-Host "G1 ARM ELF successfully built: $($Elf.FullName)" -ForegroundColor Green
Write-Warning "Build success is NOT evidence of physical audio performance or custom PCB readiness."
