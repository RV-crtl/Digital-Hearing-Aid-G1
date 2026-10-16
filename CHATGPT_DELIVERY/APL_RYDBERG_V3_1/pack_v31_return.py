from pathlib import Path
import zipfile, json, datetime, shutil

ROOT = Path(__file__).resolve().parent
PKG = ROOT.parent
stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")

def readj(path):
    path = Path(path)
    try:
        return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None
    except Exception:
        return None

verdict = readj(PKG / "FINAL_PAPER_VERDICT.json")
if verdict is None:
    verdict = {
        "release": "APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
        "created": datetime.datetime.now().isoformat(timespec="seconds"),
        "status": "OPERATIONAL_FAIL",
        "paper_claim_pass": False,
        "interpretation": "No verdict file existed when packaging began."
    }
    (PKG / "FINAL_PAPER_VERDICT.json").write_text(
        json.dumps(verdict, indent=2) + "\n", encoding="utf-8"
    )

if not (PKG / "RUN_STATUS.json").exists():
    (PKG / "RUN_STATUS.json").write_text(
        json.dumps(verdict, indent=2) + "\n", encoding="utf-8"
    )

status = str(verdict.get("status", "UNKNOWN")).replace(" ", "_")
out = PKG / f"RETURN_TO_CHATGPT_APL_V3_1_{status}_{stamp}.zip"

for src_name, dst_name in [
    ("FEBI_HYBRID_SELF_TEST.json", "FEBI_HYBRID_SELF_TEST.json"),
    ("FATAL_ERROR.txt", "V31_FATAL_ERROR.txt"),
    ("SETUP.log", "V31_SETUP.log"),
]:
    src = ROOT / "logs" / src_name
    if src.exists():
        shutil.copy2(src, PKG / dst_name)

upload_note = (
    "UPLOAD THESE FILES TO CHATGPT:\n"
    f"1. {out.name}\n"
    "2. FINAL_PAPER_VERDICT.json\n"
    "3. RUN_STATUS.json\n\n"
    "The two JSON sidecars deliberately duplicate the scientific verdict so "
    "ChatGPT can always read the outcome even if ZIP extraction is unavailable.\n"
)
(PKG / "UPLOAD_THESE_TO_CHATGPT.txt").write_text(upload_note, encoding="utf-8")

manifest = {
    "release": "APL_RYDBERG_FINAL_CONVERGENCE_V3_1",
    "created": stamp,
    "return_zip": out.name,
    "status": verdict.get("status"),
    "paper_claim_pass": verdict.get("paper_claim_pass"),
    "critical_sidecars": [
        "FINAL_PAPER_VERDICT.json",
        "RUN_STATUS.json",
        "FINAL_PAPER_VERDICT.txt",
        "UPLOAD_THESE_TO_CHATGPT.txt",
    ],
}
(PKG / "RETURN_MANIFEST_V3_1.json").write_text(
    json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
)

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    # Critical files are intentionally at ZIP root.
    for name in [
        "RETURN_MANIFEST_V3_1.json",
        "FINAL_PAPER_VERDICT.json",
        "RUN_STATUS.json",
        "FINAL_PAPER_VERDICT.txt",
        "UPLOAD_THESE_TO_CHATGPT.txt",
        "CONVERGENCE_AB.json",
        "CONVERGENCE_BC.json",
        "CONVERGENCE_LATEST.json",
        "FEBI_HYBRID_SELF_TEST.json",
        "V31_FATAL_ERROR.txt",
        "V31_SETUP.log",
    ]:
        p = PKG / name
        if p.exists():
            z.write(p, name)

    for p in ROOT.rglob("*"):
        if not p.is_file():
            continue
        if ".venv" in p.parts or "__pycache__" in p.parts:
            continue
        if p.name.startswith("RETURN_TO_CHATGPT_"):
            continue
        if p.suffix.lower() in {".aedt", ".lock"}:
            continue
        if any(x.endswith(".aedtresults") or x.endswith(".aedb") for x in p.parts):
            continue
        if p.stat().st_size > 180_000_000:
            continue
        z.write(p, Path("02_FULL_STOKES_FINAL") / p.relative_to(ROOT))

print(out)
print(PKG / "FINAL_PAPER_VERDICT.json")
print(PKG / "RUN_STATUS.json")
