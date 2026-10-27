#!/usr/bin/env python3
"""Convert a 3MF assembly with 3MF color groups into a low-draw-call GLB.

This is an offline developer utility, not run on the browser or CI. It does
not modify the source 3MF. Requires: pip install trimesh numpy lxml.
Usage: python Tools/convert_3mf_to_glb.py assembly.3mf docs/assets/g1-assembly.glb
"""
from __future__ import annotations
import argparse
from collections import defaultdict
from pathlib import Path
from zipfile import ZipFile

import numpy as np
import trimesh
from lxml import etree


def convert(source: Path, output: Path) -> dict:
    with ZipFile(source) as archive:
        root = etree.fromstring(archive.read("3D/3dmodel.model"))
    ns = {"a": root.nsmap[None], "m": root.nsmap["m"]}
    colors = [tag.get("color") for tag in root.xpath("//m:colorgroup/m:color", namespaces=ns)]
    colors = [tuple(int(color[i:i + 2], 16) for i in (1, 3, 5, 7)) for color in colors]
    materials = {tag.get("name"): tag.get("pindex") for tag in root.xpath("//a:object", namespaces=ns)}
    source_scene = trimesh.load(source, force="scene", process=False)
    grouped: dict[str, list[trimesh.Trimesh]] = defaultdict(list)
    for name, mesh in source_scene.geometry.items():
        index = materials.get(name)
        if name.startswith("Body_"):
            group, color = "Shell", (55, 70, 84, 255)
        elif index == "3":
            group, color = "Copper", (222, 177, 75, 255)
        elif index in ("1", "2"):
            group, color = "PCB", colors[int(index)]
        elif index in ("4", "5", "6"):
            group, color = "Contacts", colors[int(index)]
        elif index == "0":
            group, color = "Components", (203, 217, 218, 255)
        else:
            group, color = "Electronics", (95, 118, 140, 255)
        # Groups retain the original 3MF triangle positions and materials.
        mesh.visual.face_colors = color
        grouped[group].append(mesh)
    result = trimesh.Scene()
    triangles = 0
    for name, parts in grouped.items():
        joined = trimesh.util.concatenate(parts)
        result.add_geometry(joined, geom_name=name, node_name=name)
        triangles += len(joined.faces)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result.export(file_type="glb"))
    size = np.diff(result.bounds, axis=0)[0].round(3).tolist()
    return {"source_parts": len(source_scene.geometry), "exported_layers": list(grouped),
            "triangles": triangles, "bounds_mm": size,
            "file_mb": round(output.stat().st_size / 1048576, 2)}


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("source", type=Path)
    cli.add_argument("output", type=Path)
    args = cli.parse_args()
    print(convert(args.source, args.output))
