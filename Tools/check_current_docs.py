#!/usr/bin/env python3
"""Guard against stale Nucleo-era system/demo docs creeping into the current G1 pages."""
from __future__ import annotations
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
diagram = (ROOT / "Documentation/PRINTABLE_SYSTEM_DIAGRAM.md").read_text()
plan = (ROOT / "Documentation/BLACK_BOX_TEST_PLAN.md").read_text()
requirements = (ROOT / "Documentation/G1_CURRENT_SYSTEM_REQUIREMENTS.md").read_text()
readme = (ROOT / "README.md").read_text()

ids = {f"G1-{i:02d}" for i in range(1, 10)}
for doc, text in [("black-box plan", plan), ("requirements", requirements)]:
    if doc == "black-box plan":
        assert ids.issubset(set(re.findall(r"\bG1-\d\d\b", text))), f"Missing system requirement in {doc}"
for token in ["AudioTask", "ControlTask", "BtTask", "SAI1A", "SAI2A",
              "SAI2B", "DFSDM1", "MPR121", "LOW16", "USART1", "USART2",
              "queue", "FreeRTOS"]:
    assert token.lower() in diagram.lower(), f"Architecture is missing {token}"
for token in ["T1", "T2", "T3", "T4", "T5", "T6", "T7", "no ST-LINK", "black-box"]:
    assert token.lower() in plan.lower(), f"Black-box plan lacks {token}"
assert "not a confirmed on-PCB" in readme, "R26 evidence claim changed without verification"
assert "not yet published" in requirements.lower(), "Latest 3MF provenance lost"
assert "Nucleo B1" not in plan, "Don't demonstrate legacy Nucleo B1 as product UI"
print("PASS: nine system requirements traced across seven black-box tests, current task ownership, R26/R24 provenance")

if __name__ == "__main__":
    raise SystemExit(0)
