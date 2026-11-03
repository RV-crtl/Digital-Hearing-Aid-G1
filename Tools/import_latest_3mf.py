#!/usr/bin/env python3
"""Safely import an owner-supplied 3MF into the tracked G1 CAD source.

The command is local-only; it does NOT push to GitHub or publish the design.
Use --inspect first, then --install with explicit owner authorization.
The previous design remains retrievable from Git history; never modify KiCad.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from zipfile import ZipFile, BadZipFile

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "docs/assets/g1-assembly.3mf"
METADATA = ROOT / "docs/assets/cad-version.json"
MODEL = "3D/3dmodel.model"

def inspect(source: Path) -> dict:
    if not source.is_file():
        raise ValueError("File not found: " + str(source))
    size = source.stat().st_size
    if size > 80*1024*1024 or size < 4096:
        raise ValueError("3MF size must be between 4 KiB and 80 MiB")
    digest = hashlib.sha256()
    with source.open("rb") as stream:
        for part in iter(lambda: stream.read(1024*1024), b""):
            digest.update(part)
    try:
        with ZipFile(source) as archive:
            if archive.testzip() is not None:
                raise ValueError("3MF checksum error")
            if MODEL not in archive.namelist() or "[Content_Types].xml" not in archive.namelist():
                raise ValueError("Not a valid 3MF model archive")
            objects, triangles, vertices = 0, 0, 0
            names = []
            with archive.open(MODEL) as model:
                for _, element in ET.iterparse(model, events=("end",)):
                    if element.tag.rsplit("}", 1)[-1] == "object":
                        objects += 1
                        names.append(element.get("name") or "(unnamed)")
                        mesh = next((x for x in element if x.tag.endswith("}mesh")), None)
                        if mesh is not None:
                            for c in mesh:
                                tag = c.tag.rsplit("}", 1)[-1]
                                if tag == "triangles": triangles += len(c)
                                if tag == "vertices": vertices += len(c)
                        element.clear()
    except (BadZipFile, ET.ParseError, OSError) as exc:
        raise ValueError("Malformed 3MF file: " + str(exc)) from exc
    if objects == 0 or triangles == 0:
        raise ValueError("The 3MF contains no renderable objects")
    return {
        "format": "3MF", "unit": "millimeter",
        "file_size_bytes": size, "sha256": digest.hexdigest(),
        "mesh_objects": objects, "triangles": triangles, "vertices": vertices,
        "object_name_examples": names[:12],
        "owner_review_required": True, "visualization_only": True
    }

def main() -> int:
    arg = argparse.ArgumentParser(description=__doc__)
    arg.add_argument("source", type=Path)
    mode = arg.add_mutually_exclusive_group(required=True)
    mode.add_argument("--inspect", action="store_true", help="Validate and report without modifying the repository")
    mode.add_argument("--install", action="store_true", help="Replace the tracked source (requires consent to publish)")
    arg.add_argument("--authorised", action="store_true", help="Explicitly acknowledge CAD release permission")
    args = arg.parse_args()
    try:
        data = inspect(args.source)
    except ValueError as exc:
        print("FAIL:", exc, file=sys.stderr)
        return 2
    print(json.dumps(data, indent=2))
    if args.inspect:
        return 0
    if not args.authorised:
        print("FAIL: --install requires --authorised and an owner-approved public-release decision.",file=sys.stderr)
        return 3
    if args.source.resolve() != TARGET.resolve():
        TARGET.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(args.source, TARGET)
    META = {**data, "source_revision": "owner-imported", "note": "Original full-body 3MF; KiCad source unchanged"}
    METADATA.write_text(json.dumps(META,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    print("CAD source installed locally. Review diff, then commit/push deliberately.")
    print("The manual GitHub Pages workflow derives optimized GLB from the tracked 3MF.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
