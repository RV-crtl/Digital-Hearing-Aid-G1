#!/usr/bin/env python3
"""Cross-platform portable C test runner; explicitly NOT a Cortex-M4 build."""
from __future__ import annotations
import argparse,os,shutil,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/"Validation/results"
def run(*argv):
    argv=list(map(str,argv));print("+"," ".join(argv),flush=True)
    subprocess.run(argv,cwd=ROOT,check=True)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--compiler",default=os.getenv("CC","gcc"))
    args=p.parse_args();cc=shutil.which(args.compiler)
    if not cc:p.exit(2,"Host GCC/Clang not found; use GitHub Actions or install host compiler.\n")
    OUT.mkdir(parents=True,exist_ok=True)
    suffix=".exe" if os.name=="nt" else ""
    flags=["-std=c11","-O2","-Wall","-Wextra","-Werror","-I","Core/Inc"]
    groups=(
      ("features","Tests/test_group_features.c","Core/Src/group_features.c"),
      ("encoder","Tests/test_encoder.c","Core/Src/group_encoder.c"),
      ("guardian","Tests/test_group_guardian.c","Core/Src/group_guardian.c"),
      ("group_ui","Tests/test_group_ui.c","Core/Src/group_ui.c"),
      ("control_protocol","Tests/test_control_protocol.c","Core/Src/control_protocol.c"),
      ("pcm_transport","Tests/test_pcm_transport.c","Core/Src/pcm_transport.c","Core/Src/audio_processing.c"),
      ("audio_vectors","Validation/c/audio_vectors.c","Core/Src/audio_dynamics.c"),
      ("benchmark_profiles","Validation/c/benchmark_profiles.c","Core/Src/group_features.c"),
      ("connectivity","Tests/test_connectivity_scenarios.c","Core/Src/connectivity_controller.c"),
    )
    # Compile every application module that has no HAL/FreeRTOS dependency.
    # A successful subset of unit tests must not hide errors in unused portable files.
    portable_modules=("audio_processing","pcm_transport","audio_dynamics","telecoil_filter","telecoil_detector","signal_quality","connectivity_service","connectivity_actions","bt_profile","control_protocol","tc_cleanup","group_features","group_guardian","group_ui","group_encoder","connectivity_controller",)
    objects=OUT/"portable_compile"
    objects.mkdir(exist_ok=True)
    for module in portable_modules:
        run(cc,*flags,"-c",f"Core/Src/{module}.c","-o",objects/(module+".o"))
    start=time.monotonic()
    for name,*sources in groups:
        run(cc,*flags,*sources,"-o",OUT/(name+suffix))
    for name in ("features","encoder","guardian","group_ui","control_protocol","pcm_transport","connectivity"):
        run(OUT/(name+suffix))
    run(OUT/("audio_vectors"+suffix),OUT/"audio_vectors.csv")
    with (OUT/"profile_host_benchmark.csv").open("w",encoding="utf-8") as f:
        subprocess.run([str(OUT/("benchmark_profiles"+suffix))],cwd=ROOT,stdout=f,check=True)
    for script in ("Validation/python/check_vectors.py","CHECK_PROJECT.py","CHECK_GROUP.py",
                   "Validation/python/audit_kicad.py","Validation/python/check_evidence.py",
                   "Validation/python/check_repository.py","Validation/python/verify_rtos_contract.py",
                   "Validation/python/test_tree_integrity.py","Validation/python/check_tree_integrity.py",
                   "Tools/test_verify_arm_evidence.py",
                   "Tests/test_publication_audit.py","Tests/test_dashboard_static.py",
                   "Tools/publication_audit.py"):
        cmd=[sys.executable,script]
        if script.endswith("check_vectors.py"):cmd+=["Validation/results/audio_vectors.csv"]
        run(*cmd)
    run(sys.executable,"-m","compileall","-q","Dashboard")
    print(f"PASS: host C/contract checks ({time.monotonic()-start:.1f}s). NOT an ARM or hardware PASS.")
    return 0
if __name__=="__main__":
    try:sys.exit(main())
    except subprocess.CalledProcessError as e:
        print(f"FAIL: {e.cmd} exit code {e.returncode}",file=sys.stderr);sys.exit(max(1,e.returncode))
