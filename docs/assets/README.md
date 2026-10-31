# Original supplied G1 assembly (3MF)

`g1-assembly.3mf` is the **original supplied, ZIP-based 3MF CAD model** (its `3D/3dmodel.model` is checked by the site and publication validators). Previous revisions stored these exact bytes under `g1-assembly.glb` despite the file not being a GLB, which broke the viewer and CI. The format has been corrected without changing or fabricating geometry.

The site loads the authentic 3MF directly through Three.js `ThreeMFLoader`; the local upload control also accepts `.3mf` or a real `.glb`. Loading the full assembly may require desktop-grade GPU/memory. If you convert it to GLB later, use `Tools/convert_3mf_to_glb.py` and validate the first four bytes are `glTF` before changing the viewer URL.

**Publication and licensing remain a manual gate.** This project is already publicly visible, but file inclusion is not a warranty that all contributors, CAD authors or the institution authorised reuse. Do not infer medical/acoustic safety certification. See `Documentation/PUBLICATION_GATE.md`.
