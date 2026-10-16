from __future__ import annotations
from pathlib import Path
import sys, subprocess, json, textwrap, os

def find_target(arg=None):
    candidates=[]
    if arg:
        candidates.append(Path(arg).expanduser())
    here=Path.cwd()
    candidates += [here, here.parent]
    for base in [here, here.parent]:
        try:
            candidates += [p for p in base.iterdir() if p.is_dir() and p.name.startswith("APL_RYDBERG_FINAL_CONVERGENCE_V3")]
        except Exception:
            pass
    for p in candidates:
        if (p/"02_FULL_STOKES_FINAL"/"gen2_hfss.py").exists():
            return p.resolve()
    try:
        import tkinter as tk
        from tkinter import filedialog
        root=tk.Tk(); root.withdraw()
        sel=filedialog.askdirectory(title="Select the extracted APL_RYDBERG_FINAL_CONVERGENCE_V3 folder")
        root.destroy()
        if sel:
            p=Path(sel)
            if (p/"02_FULL_STOKES_FINAL"/"gen2_hfss.py").exists():
                return p.resolve()
    except Exception:
        pass
    raise SystemExit("Could not find the extracted V3 folder. Re-run with: py -3.11 APL_V3_1_INSTALL_AND_RUN.py C:\\path\\to\\APL_RYDBERG_FINAL_CONVERGENCE_V3")

target=find_target(sys.argv[1] if len(sys.argv)>1 else None)
st=target/"02_FULL_STOKES_FINAL"
print("Target:",target)

resume_py = r'''from __future__ import annotations
from pathlib import Path
import json, subprocess, sys, traceback, shutil, datetime

ROOT = Path(__file__).resolve().parent
PKG = ROOT.parent
CFG = json.loads((ROOT / "design_config_gen2.json").read_text(encoding="utf-8"))
CID = CFG.get("paper_candidate", "C0002")
FREQ = float(CFG.get("paper_operating_point_GHz", 1.675))
MODE = "MID"
STATES = list(CFG.get("states", ["T1","T2","T3","T4"]))

def write_json(path, obj):
    Path(path).write_text(json.dumps(obj, indent=2, default=str) + "\n", encoding="utf-8")

def now():
    return datetime.datetime.now().isoformat(timespec="seconds")

def manifest_ok(level, state, pol):
    d = ROOT / "results" / CID / f"{MODE}_{state}_{pol}_{level}"
    mf = d / "EXPORT_MANIFEST.json"
    if not mf.exists():
        return False
    try:
        data = json.loads(mf.read_text(encoding="utf-8"))
        items = data.get("items", [])
        if not items or any(x.get("status") != "PASS" for x in items):
            return False
        for item in items:
            fn = Path(str(item.get("file", ""))).name
            if not fn or not (d / fn).exists():
                return False
        return True
    except Exception:
        return False

def level_status(level):
    jobs=[]; ok=True
    for state in STATES:
        for pol in ("X","Y"):
            good=manifest_ok(level,state,pol)
            jobs.append({"state":state,"pol":pol,"complete":good})
            ok = ok and good
    return {"level":level,"complete":ok,"jobs":jobs}

def run(cmd):
    print("\n>>>"," ".join(map(str,cmd)),flush=True)
    return subprocess.run(cmd,cwd=str(ROOT)).returncode

def ensure_level(level):
    st=level_status(level)
    if st["complete"]:
        print(f"CHECKPOINT: {level} already complete; no HFSS rerun.")
        return st
    rc=run([sys.executable,"gen2_hfss.py","candidate",CID,level,MODE])
    if rc!=0:
        raise RuntimeError(f"HFSS level {level} failed with code {rc}")
    st=level_status(level)
    if not st["complete"]:
        raise RuntimeError(f"HFSS level {level} returned but export manifests are incomplete")
    return st

def analyze_pair(low,high):
    rc=run([sys.executable,"analyze_atomic_convergence.py",low,high])
    p=ROOT/"results"/CID/"PAPER_CONVERGENCE"/f"{low}_vs_{high}.json"
    if not p.exists():
        raise RuntimeError(f"Analysis {low} vs {high} did not produce {p}")
    data=json.loads(p.read_text(encoding="utf-8"))
    data["_analysis_return_code"]=rc
    return data,p

def copy_if(src,dst_name):
    src=Path(src)
    if src.exists():
        shutil.copy2(src,PKG/dst_name)

def verdict_text(v):
    r=v.get("result") or {}
    gates=r.get("gates") or {}
    n=r.get("noise_1pct") or {}
    lines=[
        "APL RYDBERG FINAL PAPER VERDICT",
        "="*42,
        f"Status: {v.get('status')}",
        f"Candidate: {v.get('candidate')}",
        f"Operating frequency: {v.get('operating_frequency_GHz')} GHz",
        f"Final comparison: {v.get('final_comparison')}",
        f"Paper claim pass: {v.get('paper_claim_pass')}",
    ]
    if r:
        lines += [
            f"Atomic transition: {r.get('chosen_atomic_transition')}",
            f"Quantization axis theta: {r.get('quantization_axis_theta_deg')} deg",
            f"Quantization axis phi: {r.get('quantization_axis_phi_deg')} deg",
            f"Lower rank: {r.get('lower_rank')}",
            f"Higher rank: {r.get('higher_rank')}",
            f"Matrix relative error: {r.get('relative_normalized_matrix_error_pct')} %",
            f"Higher condition number: {r.get('higher_condition_number')}",
            f"Mean Poincare error @1% noise: {n.get('mean_deg')} deg",
            f"P95 Poincare error @1% noise: {n.get('p95_deg')} deg",
            "Gates: "+json.dumps(gates,sort_keys=True),
        ]
    if v.get("interpretation"):
        lines += ["","Interpretation:",v["interpretation"]]
    return "\n".join(lines)+"\n"

status={
    "release":"APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
    "started":now(),
    "candidate":CID,
    "operating_frequency_GHz":FREQ,
    "stage":"START",
    "status":"RUNNING",
}

try:
    status["stage"]="CONVA"; a=ensure_level("convA")
    status["stage"]="CONVB"; b=ensure_level("convB")
    status["stage"]="ANALYZE_AB"; ab,abp=analyze_pair("convA","convB")
    copy_if(abp,"CONVERGENCE_AB.json")

    final=ab; final_name="convA_vs_convB"; bc=None; c=None
    if not bool(ab.get("paper_claim_pass")):
        status["stage"]="CONVC"; c=ensure_level("convC")
        status["stage"]="ANALYZE_BC"; bc,bcp=analyze_pair("convB","convC")
        copy_if(bcp,"CONVERGENCE_BC.json")
        final=bc; final_name="convB_vs_convC"

    passed=bool(final.get("paper_claim_pass"))
    verdict={
        "release":"APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
        "created":now(),
        "status":"PASS" if passed else "SCIENTIFIC_FAIL",
        "candidate":CID,
        "operating_frequency_GHz":FREQ,
        "final_comparison":final_name,
        "paper_claim_pass":passed,
        "result":final,
        "comparison_ab":ab,
        "comparison_bc":bc,
        "level_status":{
            "convA":a,
            "convB":b,
            "convC":c if c is not None else level_status("convC"),
        },
        "interpretation":(
            "All predeclared full-Stokes numerical gates passed. Freeze this result for the manuscript."
            if passed else
            "The final independent mesh comparison still fails one or more predeclared scientific gates. "
            "Do not tighten criteria or keep forcing the model; report the limitation."
        ),
    }
    write_json(PKG/"FINAL_PAPER_VERDICT.json",verdict)
    (PKG/"FINAL_PAPER_VERDICT.txt").write_text(verdict_text(verdict),encoding="utf-8")
    status.update({"finished":now(),"stage":"COMPLETE","status":verdict["status"],"paper_claim_pass":passed,"final_comparison":final_name})
    write_json(PKG/"RUN_STATUS.json",status)
    latest=ROOT/"results"/CID/"PAPER_CONVERGENCE"/"LATEST.json"
    copy_if(latest,"CONVERGENCE_LATEST.json")
    raise SystemExit(0 if passed else 2)

except SystemExit:
    raise
except Exception as exc:
    tb=traceback.format_exc()
    status.update({"finished":now(),"status":"OPERATIONAL_FAIL","error":repr(exc),"traceback":tb})
    write_json(PKG/"RUN_STATUS.json",status)
    verdict={
        "release":"APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
        "created":now(),
        "status":"OPERATIONAL_FAIL",
        "candidate":CID,
        "operating_frequency_GHz":FREQ,
        "paper_claim_pass":False,
        "failed_stage":status.get("stage"),
        "error":repr(exc),
        "traceback":tb,
        "level_status":{"convA":level_status("convA"),"convB":level_status("convB"),"convC":level_status("convC")},
        "interpretation":"Operational failure. This is not a scientific FAIL; fix the run before interpreting physics.",
    }
    write_json(PKG/"FINAL_PAPER_VERDICT.json",verdict)
    (PKG/"FINAL_PAPER_VERDICT.txt").write_text(verdict_text(verdict),encoding="utf-8")
    raise SystemExit(1)
'''

record_py = r'''from pathlib import Path
import json,sys,datetime
ROOT=Path(__file__).resolve().parent
PKG=ROOT.parent
stage=sys.argv[1] if len(sys.argv)>1 else "UNKNOWN"
rc=sys.argv[2] if len(sys.argv)>2 else "unknown"
cfg={}
try: cfg=json.loads((ROOT/"design_config_gen2.json").read_text(encoding="utf-8"))
except Exception: pass
diag={}
for name in ("FEBI_HYBRID_SELF_TEST.json","FATAL_ERROR.txt","SETUP.log"):
    p=ROOT/"logs"/name
    if p.exists():
        try: diag[name]=p.read_text(encoding="utf-8",errors="ignore")[-12000:]
        except Exception: diag[name]="<unreadable>"
payload={
 "release":"APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
 "created":datetime.datetime.now().isoformat(timespec="seconds"),
 "status":"OPERATIONAL_FAIL",
 "paper_claim_pass":False,
 "failed_stage":stage,
 "return_code":rc,
 "candidate":cfg.get("paper_candidate","C0002"),
 "operating_frequency_GHz":cfg.get("paper_operating_point_GHz",1.675),
 "diagnostics":diag,
 "interpretation":"The automation failed before a valid scientific verdict was produced."
}
(PKG/"FINAL_PAPER_VERDICT.json").write_text(json.dumps(payload,indent=2)+"\n",encoding="utf-8")
(PKG/"RUN_STATUS.json").write_text(json.dumps(payload,indent=2)+"\n",encoding="utf-8")
(PKG/"FINAL_PAPER_VERDICT.txt").write_text(
 "APL RYDBERG FINAL PAPER VERDICT\n================================\n"
 "Status: OPERATIONAL_FAIL\n"
 f"Failed stage: {stage}\nReturn code: {rc}\nThis is not a scientific failure.\n",
 encoding="utf-8")
print(json.dumps(payload,indent=2))
'''

pack_py = r'''from pathlib import Path
import zipfile,json,datetime,shutil
ROOT=Path(__file__).resolve().parent
PKG=ROOT.parent
stamp=datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
def readj(p):
    try: return json.loads(Path(p).read_text(encoding="utf-8")) if Path(p).exists() else None
    except Exception: return None
verdict=readj(PKG/"FINAL_PAPER_VERDICT.json")
if verdict is None:
    verdict={"release":"APL_RYDBERG_FINAL_CONVERGENCE_V3_1","status":"OPERATIONAL_FAIL","paper_claim_pass":False,
             "interpretation":"No verdict file existed when packaging began."}
    (PKG/"FINAL_PAPER_VERDICT.json").write_text(json.dumps(verdict,indent=2)+"\n",encoding="utf-8")
if not (PKG/"RUN_STATUS.json").exists():
    (PKG/"RUN_STATUS.json").write_text(json.dumps(verdict,indent=2)+"\n",encoding="utf-8")
status=str(verdict.get("status","UNKNOWN")).replace(" ","_")
out=PKG/f"RETURN_TO_CHATGPT_APL_V3_1_{status}_{stamp}.zip"
for src_name,dst_name in [
 ("FEBI_HYBRID_SELF_TEST.json","FEBI_HYBRID_SELF_TEST.json"),
 ("FATAL_ERROR.txt","V31_FATAL_ERROR.txt"),
 ("SETUP.log","V31_SETUP.log")]:
    src=ROOT/"logs"/src_name
    if src.exists(): shutil.copy2(src,PKG/dst_name)
note=("UPLOAD THESE FILES TO CHATGPT:\n"
      f"1. {out.name}\n2. FINAL_PAPER_VERDICT.json\n3. RUN_STATUS.json\n\n"
      "The JSON sidecars guarantee the verdict is readable even if archive extraction is unavailable.\n")
(PKG/"UPLOAD_THESE_TO_CHATGPT.txt").write_text(note,encoding="utf-8")
manifest={"release":"APL_RYDBERG_FINAL_CONVERGENCE_V3_1","created":stamp,"return_zip":out.name,
          "status":verdict.get("status"),"paper_claim_pass":verdict.get("paper_claim_pass"),
          "critical_sidecars":["FINAL_PAPER_VERDICT.json","RUN_STATUS.json","FINAL_PAPER_VERDICT.txt","UPLOAD_THESE_TO_CHATGPT.txt"]}
(PKG/"RETURN_MANIFEST_V3_1.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
with zipfile.ZipFile(out,"w",zipfile.ZIP_DEFLATED) as z:
    for name in ["RETURN_MANIFEST_V3_1.json","FINAL_PAPER_VERDICT.json","RUN_STATUS.json","FINAL_PAPER_VERDICT.txt",
                 "UPLOAD_THESE_TO_CHATGPT.txt","CONVERGENCE_AB.json","CONVERGENCE_BC.json","CONVERGENCE_LATEST.json",
                 "FEBI_HYBRID_SELF_TEST.json","V31_FATAL_ERROR.txt","V31_SETUP.log"]:
        p=PKG/name
        if p.exists(): z.write(p,name)
    for p in ROOT.rglob("*"):
        if not p.is_file(): continue
        if ".venv" in p.parts or "__pycache__" in p.parts: continue
        if p.name.startswith("RETURN_TO_CHATGPT_"): continue
        if p.suffix.lower() in {".aedt",".lock"}: continue
        if any(x.endswith(".aedtresults") or x.endswith(".aedb") for x in p.parts): continue
        if p.stat().st_size>180_000_000: continue
        z.write(p,Path("02_FULL_STOKES_FINAL")/p.relative_to(ROOT))
print(out)
print(PKG/"FINAL_PAPER_VERDICT.json")
print(PKG/"RUN_STATUS.json")
'''

run_cmd = r'''@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
if not exist "02_FULL_STOKES_FINAL\gen2_hfss.py" goto :wrongfolder
if not exist "02_FULL_STOKES_FINAL\analyze_atomic_convergence.py" goto :wrongfolder
if not exist "02_FULL_STOKES_FINAL\design_config_gen2.json" goto :wrongfolder
cd /d "%~dp0\02_FULL_STOKES_FINAL"
call 00_SETUP_ENV.cmd
if errorlevel 1 (
  set FAILRC=!ERRORLEVEL!
  set FAILSTAGE=ENVIRONMENT_SETUP
  goto :recordfail
)
call .venv\Scripts\activate.bat
python -u febi_hybrid_self_test.py
if errorlevel 1 (
  set FAILRC=!ERRORLEVEL!
  set FAILSTAGE=FEBI_HYBRID_SELF_TEST
  goto :recordfail
)
python -u v31_resume_and_verdict.py
set RC=!ERRORLEVEL!
python -u pack_v31_return.py
goto :report
:recordfail
if exist ".venv\Scripts\python.exe" (
  ".venv\Scripts\python.exe" v31_record_failure.py "!FAILSTAGE!" "!FAILRC!"
  ".venv\Scripts\python.exe" pack_v31_return.py
) else (
  py -3.11 v31_record_failure.py "!FAILSTAGE!" "!FAILRC!"
  py -3.11 pack_v31_return.py
)
set RC=!FAILRC!
goto :report
:wrongfolder
echo ERROR: V3.1 installer is not in the extracted V3 package root.
pause
exit /b 9
:report
cd /d "%~dp0"
echo.
echo ============================================================
echo V3.1 FINISHED WITH RETURN CODE !RC!
echo ============================================================
if exist FINAL_PAPER_VERDICT.txt type FINAL_PAPER_VERDICT.txt
echo.
echo UPLOAD THESE THREE FILES:
echo 1. newest RETURN_TO_CHATGPT_APL_V3_1_*.zip
echo 2. FINAL_PAPER_VERDICT.json
echo 3. RUN_STATUS.json
dir /b /o-d RETURN_TO_CHATGPT_APL_V3_1_*.zip 2>nul
if not "!RC!"=="0" pause
exit /b !RC!
'''

(st/"v31_resume_and_verdict.py").write_text(resume_py,encoding="utf-8")
(st/"v31_record_failure.py").write_text(record_py,encoding="utf-8")
(st/"pack_v31_return.py").write_text(pack_py,encoding="utf-8")
(target/"RUN_THIS_FINAL_V3_1.cmd").write_text(run_cmd,encoding="utf-8")
(target/"README_V3_1.txt").write_text(
"""APL RYDBERG V3.1 RESUME + VERDICT

Run RUN_THIS_FINAL_V3_1.cmd.

The runner reuses completed convA/convB/convC field exports, runs only missing
jobs, and always emits:
- FINAL_PAPER_VERDICT.json
- RUN_STATUS.json
- FINAL_PAPER_VERDICT.txt
- RETURN_TO_CHATGPT_APL_V3_1_<STATUS>_<timestamp>.zip

Upload the ZIP plus FINAL_PAPER_VERDICT.json and RUN_STATUS.json back to ChatGPT.
""",encoding="utf-8")

print("Installed V3.1 patch into:",target)
print("Launching:",target/"RUN_THIS_FINAL_V3_1.cmd")
rc=subprocess.call([str(target/"RUN_THIS_FINAL_V3_1.cmd")],cwd=str(target),shell=True)
raise SystemExit(rc)
