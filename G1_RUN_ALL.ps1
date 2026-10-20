# Run in the G1 repository root. Does not access the separate PD5 repository.
[CmdletBinding()]
param(
  [switch]$SkipPortableTests,
  [switch]$SkipPdf,
  [switch]$RequireArmReady,
  [switch]$BuildArm,
  [string]$CubeIDEExe = ""
)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$Root=Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

if (Get-Command py -ErrorAction SilentlyContinue) {
  $Python="py"; $PythonArgs=@("-3")
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
  $Python=(Get-Command python).Source; $PythonArgs=@()
} else { throw "Python 3 required. Install Python 3 first." }

function Run-G1Python {
  param([string[]]$Arguments)
  Write-Host ">> python $($Arguments -join ' ')" -ForegroundColor Cyan
  & $Python @PythonArgs @Arguments
  if ($LASTEXITCODE -ne 0) { throw "G1 Python check failed ($LASTEXITCODE): $($Arguments -join ' ')" }
}

$ResultDir=Join-Path $Root "Validation\results"
New-Item -ItemType Directory -Force -Path $ResultDir | Out-Null

Run-G1Python -Arguments @("Assessment/validate_pack.py")
Run-G1Python -Arguments @("Assessment/test_validate_pack.py")
Run-G1Python -Arguments @("Assessment/test_evidence_audit.py")
$Preflight=@("Tools/g1_preflight.py")
if($RequireArmReady) { $Preflight+="--require-arm-ready" }
Run-G1Python -Arguments $Preflight
Run-G1Python -Arguments @("Assessment/evidence_audit.py")

if ($SkipPortableTests) {
  Write-Warning "Host C checks SKIPPED. Manifest will report partial validation."
} else {
  Run-G1Python -Arguments @("Validation/run_host_validation.py")
}

if ($SkipPdf) {
  Write-Warning "Draft assessment PDFs SKIPPED."
} else {
  $Venv=Join-Path $Root ".g1-tools-venv"
  $VenvPython=Join-Path $Venv "Scripts\python.exe"
  if(-not (Test-Path $VenvPython)) { Run-G1Python -Arguments @("-m","venv",$Venv) }
  & $VenvPython -m pip --disable-pip-version-check install "reportlab>=4,<5"
  if($LASTEXITCODE -ne 0) { throw "ReportLab install failed in isolated G1 environment." }
  & $VenvPython Assessment/generate_pack.py
  if($LASTEXITCODE -ne 0) { throw "G1 PDF generation failed." }
}

$Revision="UNKNOWN"
if(Get-Command git -ErrorAction SilentlyContinue) {
  $value=(& git rev-parse HEAD 2>$null)
  if($LASTEXITCODE -eq 0 -and $value -match '^[0-9a-fA-F]{40}
$Art=@(
  "Assessment/requirements.csv",
  "Assessment/test_cases.json",
  "Assessment/EVIDENCE_REGISTER.csv",
  "Validation/results/g1_preflight.json",
  "Validation/results/g1_evidence_audit.json"
)
if(-not $SkipPortableTests) {
  $Art+=@("Validation/results/audio_vectors.csv",
          "Validation/results/profile_host_benchmark.csv",
          "Validation/results/kicad_audit.json",
          "Validation/results/publication_audit.json")
}
if($BuildArm) {
  $Art+=@("Validation/results/g1_arm_build.json",
          "Validation/results/g1_arm_build.log",
          "Validation/results/g1_arm_build_stderr.log")
}
if(-not $SkipPdf) {
  $Art+=@("Assessment/generated/SYSTEM_REQUIREMENTS_AND_BLACK_BOX_TEST_PLAN_DRAFT.pdf",
          "Assessment/generated/PRINT_A3_SYSTEM_SOFTWARE_HARDWARE_INTERFACES.pdf")
}
$Manifest=[ordered]@{
  repository="RV-crtl/Digital-Hearing-Aid-G1"
  firmware_commit=$Revision
  generated_utc=(Get-Date).ToUniversalTime().ToString("o")
  portable_C_tests_executed=(-not $SkipPortableTests)
  draft_PDFs_generated=(-not $SkipPdf)
  arm_compiled_by_this_script=$ArmBuildVerified
  arm_evidence_cryptographically_verified=$ArmBuildVerified
  physical_hardware_verified_by_this_script=$false
  artifacts=@()
}
$Staging=Join-Path $ResultDir "g1_bundle_stage"
if(Test-Path $Staging) { Remove-Item -Force -Recurse $Staging }
New-Item -ItemType Directory -Force -Path $Staging | Out-Null
foreach($rel in $Art) {
  $native=$rel.Replace("/",[IO.Path]::DirectorySeparatorChar)
  $source=Join-Path $Root $native
  if(-not (Test-Path $source -PathType Leaf)) { throw "Missing expected evidence: $rel" }
  $target=Join-Path $Staging $native
  New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
  Copy-Item -LiteralPath $source -Destination $target
  $Manifest.artifacts+=@{path=$rel;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$manifestFile=Join-Path $Staging "G1_BUNDLE_MANIFEST.json"
$Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestFile -Encoding UTF8
$stamp=Get-Date -Format "yyyyMMdd_HHmmss"
$Archive=Join-Path $Root "G1_QUALITY_EVIDENCE_$stamp.zip"
Compress-Archive -Path (Join-Path $Staging "*") -DestinationPath $Archive -CompressionLevel Optimal
Write-Host "G1 audit bundle: $Archive" -ForegroundColor Green
Write-Warning "This proves source and host checks only. ARM build/physical acoustic measurements are separate gates."
) { $Revision=([string]$value).Trim() }
}

$ArmBuildVerified=$false
if($BuildArm) {
  # An old PASS JSON is never evidence for this invocation.
  $ReportPath=Join-Path $ResultDir "g1_arm_build.json"
  if (Test-Path $ReportPath) { Remove-Item -LiteralPath $ReportPath -Force }
  $ArmBuildStarted=(Get-Date).ToUniversalTime()
  & (Join-Path $Root "G1_BUILD_ARM.ps1") -CubeIDEExe $CubeIDEExe
  Run-G1Python -Arguments @("Tools/verify_arm_evidence.py",
    "--report", $ReportPath,
    "--project-root", $Root,
    "--not-before", $ArmBuildStarted.ToString("o"),
    "--expected-revision", $Revision)
  $ArmBuildVerified=$true
}
$Art=@(
  "Assessment/requirements.csv",
  "Assessment/test_cases.json",
  "Assessment/EVIDENCE_REGISTER.csv",
  "Validation/results/g1_preflight.json",
  "Validation/results/g1_evidence_audit.json"
)
if(-not $SkipPortableTests) {
  $Art+=@("Validation/results/audio_vectors.csv",
          "Validation/results/profile_host_benchmark.csv",
          "Validation/results/kicad_audit.json")
}
if($BuildArm) {
  $Art+=@("Validation/results/g1_arm_build.json",
          "Validation/results/g1_arm_build.log",
          "Validation/results/g1_arm_build_stderr.log")
}
if(-not $SkipPdf) {
  $Art+=@("Assessment/generated/SYSTEM_REQUIREMENTS_AND_BLACK_BOX_TEST_PLAN_DRAFT.pdf",
          "Assessment/generated/PRINT_A3_SYSTEM_SOFTWARE_HARDWARE_INTERFACES.pdf")
}
$Manifest=[ordered]@{
  repository="RV-crtl/Digital-Hearing-Aid-G1"
  firmware_commit=$Revision
  generated_utc=(Get-Date).ToUniversalTime().ToString("o")
  portable_C_tests_executed=(-not $SkipPortableTests)
  draft_PDFs_generated=(-not $SkipPdf)
  arm_compiled_by_this_script=[bool]$BuildArm
  physical_hardware_verified_by_this_script=$false
  artifacts=@()
}
$Staging=Join-Path $ResultDir "g1_bundle_stage"
if(Test-Path $Staging) { Remove-Item -Force -Recurse $Staging }
New-Item -ItemType Directory -Force -Path $Staging | Out-Null
foreach($rel in $Art) {
  $native=$rel.Replace("/",[IO.Path]::DirectorySeparatorChar)
  $source=Join-Path $Root $native
  if(-not (Test-Path $source -PathType Leaf)) { throw "Missing expected evidence: $rel" }
  $target=Join-Path $Staging $native
  New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
  Copy-Item -LiteralPath $source -Destination $target
  $Manifest.artifacts+=@{path=$rel;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$manifestFile=Join-Path $Staging "G1_BUNDLE_MANIFEST.json"
$Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestFile -Encoding UTF8
$stamp=Get-Date -Format "yyyyMMdd_HHmmss"
$Archive=Join-Path $Root "G1_QUALITY_EVIDENCE_$stamp.zip"
Compress-Archive -Path (Join-Path $Staging "*") -DestinationPath $Archive -CompressionLevel Optimal
Write-Host "G1 audit bundle: $Archive" -ForegroundColor Green
Write-Warning "This proves source and host checks only. ARM build/physical acoustic measurements are separate gates."
