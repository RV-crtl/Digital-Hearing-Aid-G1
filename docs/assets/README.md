# Original supplied G1 assembly (3MF)

`g1-assembly.3mf` is the **original supplied, ZIP-based 3MF CAD model** (its `3D/3dmodel.model` is checked by the site and publication validators). Previous revisions stored these exact bytes under `g1-assembly.glb` despite the file not being a GLB, which broke the viewer and CI. The format has been corrected without changing or fabricating geometry.

The manually triggered Pages workflow generates an optimized real `.glb` from this 3MF at deployment. The viewer uses the GLB when present and falls back to the authentic 3MF through Three.js `ThreeMFLoader`; local upload also accepts either format. Loading the full assembly may require desktop-grade GPU/memory. If you convert it to GLB later, use `Tools/convert_3mf_to_glb.py` and validate the first four bytes are `glTF` before changing the viewer URL.

**Publication and licensing remain a manual gate.** This project is already publicly visible, but file inclusion is not a warranty that all contributors, CAD authors or the institution authorised reuse. Do not infer medical/acoustic safety certification. See `Documentation/PUBLICATION_GATE.md`.

## Install a newly owner-approved full-body 3MF

The November 2026 all-body project is **not** the earlier tracked model. The browser's local-file button can inspect the new file without uploading it. To update the public default, from a clean local clone run:

```sh
python Tools/import_latest_3mf.py --inspect "path/to/260510-Digital_hearing_aid1.step(1).3mf"
python Tools/import_latest_3mf.py --install --authorised "path/to/260510-Digital_hearing_aid1.step(1).3mf"
python docs/validate_site.py
```

Then review the resulting `docs/assets/g1-assembly.3mf` and `cad-version.json`, commit intentionally, and run the manual Pages publication workflow. The importer validates ZIP/3MF structure and counts without installing CAD dependencies, preserves original KiCad, requires an explicit permission flag for publishing, and does **not** push the model automatically.
