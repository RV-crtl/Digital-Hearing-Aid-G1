param([string]$NmExe='')
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $MyInvocation.MyCommand.Path
$Artifact=Join-Path $Root 'Debug\Core\Src\g1_product_board.o'
if (-not (Test-Path $Artifact)) {throw 'Board object missing: build Debug first'}
if (-not $NmExe) {
  $tool=Get-ChildItem 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins' -Recurse -Filter arm-none-eabi-nm.exe -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $tool) {throw 'Cannot find arm-none-eabi-nm.exe; pass -NmExe with full path'}
  $NmExe=$tool.FullName
}
$symbols=& $NmExe -g $Artifact
if ($LASTEXITCODE -ne 0) {throw 'ARM nm failed'}
foreach($entry in @('HAL_I2C_IsDeviceReady','HAL_I2C_Mem_Write','HAL_I2C_Mem_Read','HAL_DFSDM_FilterRegularStart_DMA','HAL_DFSDM_FilterRegularStop_DMA')) {
  $line=$symbols|Where-Object {$_ -match ('\b[TW]\s+'+[regex]::Escape($entry)+'$')}
  if (-not $line) {throw "HAL function $entry not DEFINED in g1_product_board.o"}
  Write-Host "PASS defined: $entry" -ForegroundColor Green
}
Write-Host 'R7 vendor HAL functions linked directly into board object' -ForegroundColor Green
