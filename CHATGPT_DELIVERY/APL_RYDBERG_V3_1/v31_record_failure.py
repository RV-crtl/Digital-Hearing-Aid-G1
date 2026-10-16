from pathlib import Path
import json, sys, datetime

ROOT = Path(__file__).resolve().parent
PKG = ROOT.parent
stage = sys.argv[1] if len(sys.argv) > 1 else "UNKNOWN"
rc = sys.argv[2] if len(sys.argv) > 2 else "unknown"

def now():
    return datetime.datetime.now().isoformat(timespec="seconds")

cfg = {}
try:
    cfg = json.loads((ROOT / "design_config_gen2.json").read_text(encoding="utf-8"))
except Exception:
    pass

diag = {}
for name in ("FEBI_HYBRID_SELF_TEST.json", "FATAL_ERROR.txt", "SETUP.log"):
    p = ROOT / "logs" / name
    if p.exists():
        try:
            diag[name] = p.read_text(encoding="utf-8", errors="ignore")[-12000:]
        except Exception:
            diag[name] = "<unreadable>"

payload = {
    "release": "APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
    "created": now(),
    "status": "OPERATIONAL_FAIL",
    "paper_claim_pass": False,
    "failed_stage": stage,
    "return_code": rc,
    "candidate": cfg.get("paper_candidate", "C0002"),
    "operating_frequency_GHz": cfg.get("paper_operating_point_GHz", 1.675),
    "diagnostics": diag,
    "interpretation": "The automation failed before a valid scientific verdict was produced."
}

(PKG / "FINAL_PAPER_VERDICT.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
(PKG / "RUN_STATUS.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
(PKG / "FINAL_PAPER_VERDICT.txt").write_text(
    "APL RYDBERG FINAL PAPER VERDICT\n"
    "================================\n"
    "Status: OPERATIONAL_FAIL\n"
    f"Failed stage: {stage}\n"
    f"Return code: {rc}\n"
    "This is not a scientific failure.\n",
    encoding="utf-8",
)
print(json.dumps(payload, indent=2))
