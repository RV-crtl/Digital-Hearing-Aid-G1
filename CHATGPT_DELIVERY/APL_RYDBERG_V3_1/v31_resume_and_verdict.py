from __future__ import annotations
from pathlib import Path
import json, subprocess, sys, traceback, shutil, datetime

ROOT = Path(__file__).resolve().parent
PKG = ROOT.parent
CFG = json.loads((ROOT / "design_config_gen2.json").read_text(encoding="utf-8"))
CID = CFG.get("paper_candidate", "C0002")
FREQ = float(CFG.get("paper_operating_point_GHz", 1.675))
MODE = "MID"
STATES = list(CFG.get("states", ["T1", "T2", "T3", "T4"]))

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
    jobs = []
    ok = True
    for state in STATES:
        for pol in ("X", "Y"):
            good = manifest_ok(level, state, pol)
            jobs.append({"state": state, "pol": pol, "complete": good})
            ok = ok and good
    return {"level": level, "complete": ok, "jobs": jobs}

def run(cmd):
    print("\n>>>", " ".join(map(str, cmd)), flush=True)
    return subprocess.run(cmd, cwd=str(ROOT)).returncode

def ensure_level(level):
    st = level_status(level)
    if st["complete"]:
        print(f"CHECKPOINT: {level} already complete; no HFSS rerun.")
        return st
    rc = run([sys.executable, "gen2_hfss.py", "candidate", CID, level, MODE])
    if rc != 0:
        raise RuntimeError(f"HFSS level {level} failed with code {rc}")
    st = level_status(level)
    if not st["complete"]:
        raise RuntimeError(f"HFSS level {level} returned but export manifests are incomplete")
    return st

def analyze_pair(low, high):
    rc = run([sys.executable, "analyze_atomic_convergence.py", low, high])
    p = ROOT / "results" / CID / "PAPER_CONVERGENCE" / f"{low}_vs_{high}.json"
    if not p.exists():
        raise RuntimeError(f"Analysis {low} vs {high} did not produce {p}")
    data = json.loads(p.read_text(encoding="utf-8"))
    data["_analysis_return_code"] = rc
    return data, p

def copy_if(src, dst_name):
    src = Path(src)
    if src.exists():
        shutil.copy2(src, PKG / dst_name)

def verdict_text(v):
    r = v.get("result") or {}
    n = r.get("noise_1pct") or {}
    lines = [
        "APL RYDBERG FINAL PAPER VERDICT",
        "=" * 42,
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
            "Gates: " + json.dumps(r.get("gates") or {}, sort_keys=True),
        ]
    if v.get("interpretation"):
        lines += ["", "Interpretation:", v["interpretation"]]
    return "\n".join(lines) + "\n"

status = {
    "release": "APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
    "started": now(),
    "candidate": CID,
    "operating_frequency_GHz": FREQ,
    "stage": "START",
    "status": "RUNNING",
}

try:
    status["stage"] = "CONVA"
    a = ensure_level("convA")
    status["stage"] = "CONVB"
    b = ensure_level("convB")

    status["stage"] = "ANALYZE_AB"
    ab, ab_path = analyze_pair("convA", "convB")
    copy_if(ab_path, "CONVERGENCE_AB.json")

    final = ab
    final_name = "convA_vs_convB"
    bc = None
    c = None

    if not bool(ab.get("paper_claim_pass")):
        status["stage"] = "CONVC"
        c = ensure_level("convC")
        status["stage"] = "ANALYZE_BC"
        bc, bc_path = analyze_pair("convB", "convC")
        copy_if(bc_path, "CONVERGENCE_BC.json")
        final = bc
        final_name = "convB_vs_convC"

    passed = bool(final.get("paper_claim_pass"))
    verdict = {
        "release": "APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
        "created": now(),
        "status": "PASS" if passed else "SCIENTIFIC_FAIL",
        "candidate": CID,
        "operating_frequency_GHz": FREQ,
        "final_comparison": final_name,
        "paper_claim_pass": passed,
        "result": final,
        "comparison_ab": ab,
        "comparison_bc": bc,
        "level_status": {
            "convA": a,
            "convB": b,
            "convC": c if c is not None else level_status("convC"),
        },
        "interpretation": (
            "All predeclared full-Stokes numerical gates passed. Freeze this result for the manuscript."
            if passed else
            "The final independent mesh comparison still fails one or more predeclared scientific gates. "
            "Do not tighten criteria or keep forcing the model; report the limitation."
        ),
    }

    write_json(PKG / "FINAL_PAPER_VERDICT.json", verdict)
    (PKG / "FINAL_PAPER_VERDICT.txt").write_text(verdict_text(verdict), encoding="utf-8")
    status.update({
        "finished": now(),
        "stage": "COMPLETE",
        "status": verdict["status"],
        "paper_claim_pass": passed,
        "final_comparison": final_name,
    })
    write_json(PKG / "RUN_STATUS.json", status)

    latest = ROOT / "results" / CID / "PAPER_CONVERGENCE" / "LATEST.json"
    copy_if(latest, "CONVERGENCE_LATEST.json")
    raise SystemExit(0 if passed else 2)

except SystemExit:
    raise
except Exception as exc:
    tb = traceback.format_exc()
    status.update({
        "finished": now(),
        "status": "OPERATIONAL_FAIL",
        "error": repr(exc),
        "traceback": tb,
    })
    write_json(PKG / "RUN_STATUS.json", status)
    verdict = {
        "release": "APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
        "created": now(),
        "status": "OPERATIONAL_FAIL",
        "candidate": CID,
        "operating_frequency_GHz": FREQ,
        "paper_claim_pass": False,
        "failed_stage": status.get("stage"),
        "error": repr(exc),
        "traceback": tb,
        "level_status": {
            "convA": level_status("convA"),
            "convB": level_status("convB"),
            "convC": level_status("convC"),
        },
        "interpretation": "Operational failure. This is not a scientific FAIL; fix the run before interpreting physics.",
    }
    write_json(PKG / "FINAL_PAPER_VERDICT.json", verdict)
    (PKG / "FINAL_PAPER_VERDICT.txt").write_text(verdict_text(verdict), encoding="utf-8")
    raise SystemExit(1)
