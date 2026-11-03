# CAD provenance and real 3D viewing

**Latest supplied assembly** — `260510-Digital_hearing_aid1.step(1).3mf` (uploaded 03 November 2026), 6,466,898 bytes, ZIP-based 3MF, millimetre units, **1,713 mesh objects / 460,021 triangles**. The single file includes PCB-related bodies, enclosure/structural pieces and physically separated large fixture bodies. Do not assume the file is just a PCB; the design was authored as one multi-body assembly.

The latest upload is **not byte-identical** to the repository's earlier `docs/assets/g1-assembly.3mf` (6,067,119 bytes). The repository's published default viewer currently uses that earlier 3MF, with an optimized GLB derived from it during Pages deployment. The new 3MF can be loaded locally through the website's **Open your 3D CAD** control (the browser reads it locally; it is not uploaded).

The newer source body names are predominantly `Imported_*`, with separate `Body_1717`, `Body_1721`, `Body_1720`, `Body_1720_(2)` and `Mesh_body_01` structures. Coordinates suggest clustered PCB/electronics near X 0–96 mm, adjacent housing bodies around the same region, and separated larger structures at greater X distances. These are *geometric observations*, not manually validated semantic labels; the exact function of each mesh must be checked against CAD and the assembled prototype.

## Inspect the full assembly

1. Open the [G1 visual site](https://rv-crtl.github.io/Digital-Hearing-Aid-G1/#device).
2. Click **Open your 3D CAD** and select the latest uploaded 3MF.
3. Rotate, zoom, explode and toggle housing/wireframe. A combined view can include multiple physically separated test fixtures; use the fit-to-view control if you lose the assembly.
4. Use the **KiCad schematic gallery** to inspect real source-linked sheets from `Hardware/Original_KiCad/`. The page shows SVGs exported by KiCad CLI, not manually redrawn circuitry.
5. To permanently host the newer complete assembly, commit the actual file after rights review and regenerate the Pages GLB from it; **do not silently substitute the earlier version**.

The original KiCad PCB and schematic have not changed. CAD meshes and rendered KiCad views are engineering visualization, not geometric clearance, ERC, DRC or acoustic-safety proof.

## Optimization and part naming

For public builds, prefer rendering grouped GLB geometry with limited draw calls, preserving original 3MF as provenance. Converting a 3MF with many imported bodies may collapse per-body names into render groups; use the source 3MF as the authoritative body hierarchy. The [converter](../Tools/convert_3mf_to_glb.py) generates a viewing artefact only and must not overwrite the original.
