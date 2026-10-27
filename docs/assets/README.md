# Real G1 CAD assembly asset

Upload the converted 3D assembly **here** using **Add file → Upload files** while browsing this `docs/assets/` folder.

The filename must be exactly **`g1-assembly.glb`**. GitHub Pages will then display the original 3MF-derived physical model (housing, PCB, copper, components, contacts) in its real-time WebGL viewer. On the next manually triggered Pages deploy the file will be included automatically.

The interactive site works without it and accurately labels the illustrative 3D fallback as conceptual. You can also use the site's **Open local CAD (.glb)** button for a completely local private preview.

Only commit the asset if your team owns the model and has agreed to redistribute it publicly. Never upload project secrets, assessment submissions, recordings, or device test data into the Pages root.

Conversion source: [`Tools/convert_3mf_to_glb.py`](../../Tools/convert_3mf_to_glb.py).

**Instant private preview:** the website also accepts the original `.3mf` directly via **Open your 3D CAD**. It parses the local file in-browser; it does not transmit the model to GitHub. Large 3MFs may consume substantial memory; a converted GLB is faster for public visitors.
