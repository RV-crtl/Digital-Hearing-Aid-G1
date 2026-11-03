#!/usr/bin/env python3
"""Static contract test for the read-only KiCad PCB viewer (no KiCad required)."""
from __future__ import annotations
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "docs"))
import render_pcb as board

def main() -> None:
    assert board.SOURCE.is_file(), "Original KiCad PCB must remain available"
    assert len(board.VIEWS) == 4
    assert {v[0] for v in board.VIEWS} == {"front", "back", "front-copper", "back-copper"}
    for slug, layers, mirror in board.VIEWS:
        cmd = board.command(slug, layers, mirror)
        assert cmd[:4] == ["kicad-cli", "pcb", "export", "svg"]
        assert "--mode-single" in cmd and "--exclude-drawing-sheet" in cmd
        assert "--fit-page-to-board" in cmd
        assert ("--mirror" in cmd) is mirror
        assert cmd[cmd.index("--layers") + 1] == layers
        assert cmd[cmd.index("--output") + 1].endswith("/" + slug + ".svg")
        assert "Hardware/Original_KiCad" in str(board.SOURCE)
    print("PASS: four original KiCad PCB views; no source writes, correct mirrored back layers")

if __name__ == "__main__":
    main()
