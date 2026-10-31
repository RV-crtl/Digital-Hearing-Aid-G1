#!/usr/bin/env python3
"""Build a traceable *source candidate*, not a hardware-certified firmware release.

Includes selected firmware, board assets, site source and verification harness.
Excludes historical references, course assessment submissions, recorded audio
and any generated executable; checks rights before distributing publicly.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PREFIXES=("Core/","Drivers/","Validation/","Tools/","docs/","Documentation/","Hardware/")
SINGLES={".project",".cproject","261013-Individual_Demonstration_Connectivity.ioc","README.md","CHANGELOG.md","PREPARE_PROJECT.ps1","PREPARE_PROJECT.bat","G1_BUILD_ARM.ps1","G1_RUN_ALL.ps1","CHECK_PROJECT.py","CHECK_GROUP.py"}
SKIP=("Validation/results/","Documentation/Reference/","Hardware/Original_KiCad/","docs/plots/","docs/assets/g1-assembly.3mf")
SKIP_SUFFIX=(".pyc",".elf",".hex",".bin",".zip",".pdf")
MAX_SIZE=8*1024*1024

def manifest_records(files):
    entries=[]
    for path in files:
        data=(ROOT/path).read_bytes()
        entries.append({"path":path,"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
    return entries

def collect_files():
    names=subprocess.check_output(["git","ls-files","-z"],cwd=ROOT).split(b"\x00")
    filtered=[]
    for part in names:
        if not part:continue
        name=part.decode("utf-8")
        if name not in SINGLES and not name.startswith(PREFIXES):continue
        if name.startswith(SKIP) or name.endswith(SKIP_SUFFIX):continue
        obj=ROOT/name
        if not obj.is_file() or obj.stat().st_size>MAX_SIZE:continue
        filtered.append(name)
    return sorted(set(filtered))

def package(destination):
    files=collect_files()
    revision=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    record={
       "kind":"G1_SOURCE_CANDIDATE_ONLY",
       "git_sha":revision,
       "utc":datetime.now(timezone.utc).isoformat(),
       "device_flashing_performed":False,
       "custom_pcb_validated_by_this_tool":False,
       "acoustic_safety_validated":False,
       "external_library_clearance_required":True,
       "hardware_release_approved":False,
       "files":manifest_records(files)
    }
    destination.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(destination,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=7) as z:
        for rel in files:z.write(ROOT/rel,arcname=rel)
        z.writestr("G1_SOURCE_CANDIDATE_MANIFEST.json",json.dumps(record,indent=2)+"\n")
    with zipfile.ZipFile(destination) as z:
        for entry in record["files"]:
            assert hashlib.sha256(z.read(entry["path"])).hexdigest()==entry["sha256"]
    return len(files),revision

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"Validation/results/G1_SOURCE_CANDIDATE.zip")
    args=parser.parse_args()
    try:
        count,sha=package(args.output)
    except (OSError,subprocess.CalledProcessError,zipfile.BadZipFile) as error:
        print(f"FAIL: source candidate packaging: {type(error).__name__}",file=sys.stderr)
        sys.exit(2)
    print(f"PASS: {count} tracked files; source candidate revision {sha}")
    print("NOT a tested custom-PCB firmware image or permission to distribute uncleared content.")
