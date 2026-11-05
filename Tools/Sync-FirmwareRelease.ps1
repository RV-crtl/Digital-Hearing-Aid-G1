<#
.SYNOPSIS
  Safely synchronize a complete, already validated STM32CubeIDE release ZIP
  into this repository's active firmware tree.

.DESCRIPTION
  Preserves GitHub-only Documentation/, Tools/, Validation/, Assets/, Hardware/,
  website and other repository support files. Replaces Core/, Drivers/,
  Middlewares/, .ioc, .project and .cproject from one validated project ZIP.
  Does not push or commit unless -Apply is explicitly specified.

.EXAMPLE
  ./Tools/Sync-FirmwareRelease.ps1 -ZipPath 'C:/Downloads/Quiet_Riot_R26_CubeIDE_Clean_Firmware.zip'
  ./Tools/Sync-FirmwareRelease.ps1 -ZipPath 'C:/Downloads/Quiet_Riot_R26_CubeIDE_Clean_Firmware.zip' -Apply
#>
param(
  [Parameter(Mandatory = $true)]
  [string]$ZipPath,
  [switch]$Apply
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$zip = (Resolve-Path $ZipPath).Path

if (-not (Test-Path -LiteralPath (Join-Path $repo '.git'))) {
  throw 'Run this script in a full git clone of this repository, not GitHub Code ZIP.'
}
$dirty = @(git -C $repo status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirty.Count -gt 0) {
  throw 'Working tree is not clean. Commit or stash changes before synchronization.'
}
$branch = (git -C $repo branch --show-current).Trim()
if ($branch -ne 'main') { throw "Expected main branch, found '$branch'. Checkout main first." }

$temp = Join-Path ([IO.Path]::GetTempPath()) ('qr-release-sync-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp -Force | Out-Null
try {
  Expand-Archive -LiteralPath $zip -DestinationPath $temp -Force
  $projects = @(Get-ChildItem -LiteralPath $temp -Recurse -File -Filter '.project')
  if ($projects.Count -ne 1) { throw "Release must contain exactly one CubeIDE .project (found $($projects.Count))." }
  $project = $projects[0].Directory.FullName
  $expected = @('Core/Src/main.c', '.cproject', '.project')
  foreach ($name in $expected) {
    if (-not (Test-Path -LiteralPath (Join-Path $project $name))) { throw "Release missing: $name" }
  }
  $ioc = @(Get-ChildItem -LiteralPath $project -File -Filter '*.ioc')
  if ($ioc.Count -ne 1) { throw "Release must contain exactly one root .ioc (found $($ioc.Count))." }
  $mains = @(Get-ChildItem -LiteralPath $project -Recurse -File -Filter 'main.c')
  if ($mains.Count -ne 1) { throw "Release must contain exactly one main.c (found $($mains.Count))." }

  $toReplace = @('Core', 'Drivers', 'Middlewares')
  foreach ($name in $toReplace) {
    $src = Join-Path $project $name
    if (-not (Test-Path -LiteralPath $src)) { throw "Release missing required directory: $name" }
  }

  Write-Host "Release project: $project"
  Write-Host 'Preserving GitHub-only documentation, website, tools, hardware and validation resources.'
  Write-Host 'Replacing active firmware: Core, Drivers, Middlewares, .project, .cproject, root .ioc.'
  if (-not $Apply) {
    Write-Host 'DRY RUN: validated release structure; no files changed.'
    Write-Host 'Review the ZIP, then rerun with -Apply to update, commit and push.'
    return
  }

  foreach ($name in $toReplace) {
    $target = Join-Path $repo $name
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    Copy-Item -LiteralPath (Join-Path $project $name) -Destination $target -Recurse -Force
  }
  foreach ($name in @('.project', '.cproject')) {
    Copy-Item -LiteralPath (Join-Path $project $name) -Destination (Join-Path $repo $name) -Force
  }
  Get-ChildItem -LiteralPath $repo -File -Filter '*.ioc' | Remove-Item -Force
  Copy-Item -LiteralPath $ioc[0].FullName -Destination $repo -Force
  foreach ($ld in @(Get-ChildItem -LiteralPath $project -File -Filter '*.ld')) {
    Copy-Item -LiteralPath $ld.FullName -Destination $repo -Force
  }
  git -C $repo add -A -- Core Drivers Middlewares .project .cproject '*.ioc' '*.ld'
  if ($LASTEXITCODE -ne 0) { throw 'git add failed' }
  git -C $repo diff --cached --stat
  if ($LASTEXITCODE -ne 0) { throw 'git diff failed' }
  git -C $repo diff --cached --quiet
  if ($LASTEXITCODE -eq 0) {
    Write-Host 'No firmware changes to commit.'
    return
  }
  git -C $repo commit -m 'firmware: synchronize validated QR261104.8 CubeIDE release'
  if ($LASTEXITCODE -ne 0) { throw 'git commit failed; changes remain staged' }
  git -C $repo push origin main
  if ($LASTEXITCODE -ne 0) { throw 'git push failed; commit remains local for safe retry' }
  Write-Host 'Release synchronization pushed successfully.'
} finally {
  if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
