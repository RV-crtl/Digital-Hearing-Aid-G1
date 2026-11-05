param(
  [string]$OutputPath = 'dist'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$proj = [xml](Get-Content -LiteralPath (Join-Path $repo '.project') -Raw)
$name = $proj.projectDescription.name
if ([string]::IsNullOrWhiteSpace($name)) { throw 'STM32CubeIDE .project is missing a project name' }
$sourceDirectories = @('Core', 'Drivers', 'Middlewares')
$ioc = @(Get-ChildItem -LiteralPath $repo -File -Filter '*.ioc')
if ($ioc.Count -ne 1) { throw "Expected exactly one root .ioc file, got $($ioc.Count)" }
if (-not (Test-Path -LiteralPath (Join-Path $repo 'Core/Src/main.c'))) { throw 'Missing application entry point Core/Src/main.c' }
$destination = if ([IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $repo $OutputPath }
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$temp = Join-Path ([IO.Path]::GetTempPath()) ('cubeide-export-' + [guid]::NewGuid().ToString('N'))
$stage = Join-Path $temp $name
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
  foreach ($dir in $sourceDirectories) {
    $src = Join-Path $repo $dir
    if (-not (Test-Path -LiteralPath $src)) { throw "Missing required source directory: $dir" }
    Copy-Item -LiteralPath $src -Destination $stage -Recurse -Force
  }
  $metadata = @('.project', '.cproject', '.clang-format', '.editorconfig')
  foreach ($file in $metadata) {
    $p = Join-Path $repo $file
    if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $stage -Force }
  }
  $settings = Join-Path $repo '.settings'
  if (Test-Path -LiteralPath $settings) { Copy-Item -LiteralPath $settings -Destination $stage -Recurse -Force }
  Copy-Item -LiteralPath $ioc[0].FullName -Destination $stage
  foreach ($ld in @(Get-ChildItem -LiteralPath $repo -File -Filter '*.ld')) {
    Copy-Item -LiteralPath $ld.FullName -Destination $stage -Force
  }
  $mainFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter 'main.c')
  if ($mainFiles.Count -ne 1) { throw "Expected one main.c in package, got $($mainFiles.Count)" }
  $iocFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter '*.ioc')
  if ($iocFiles.Count -ne 1) { throw "Expected one .ioc in package, got $($iocFiles.Count)" }
  $unwanted = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Extension -in @('.md', '.py', '.o', '.elf', '.map', '.hex', '.bin', '.log') -or $_.FullName -match '[\\/](Debug|Release|__pycache__)[\\/]' })
  foreach ($file in $unwanted) { Remove-Item -LiteralPath $file.FullName -Force }
  $zip = Join-Path $destination ($name + '-CubeIDE.zip')
  if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
  Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
  Write-Host "CubeIDE project packaged: $zip"
  Write-Host 'Import in STM32CubeIDE: File > Import > Existing Projects into Workspace'
} finally {
  if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}