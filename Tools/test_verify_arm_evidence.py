"""Mutation tests for stale and mismatched ARM evidence without an ARM toolchain."""
import hashlib
import json
import tempfile
import unittest
from datetime import datetime, timezone, timedelta
from pathlib import Path

from verify_arm_evidence import verify

REV = "a" * 40


class ArmEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        folder = self.root / "Debug"
        folder.mkdir()
        self.elf = folder / "fw.elf"
        data = bytearray(4096)
        data[0:4] = b"\x7fELF"
        data[18:20] = b"\x28\x00"
        self.elf.write_bytes(data)
        self.started = datetime.now(timezone.utc) - timedelta(seconds=10)
        self.report = self.root / "evidence.json"
        self.payload = {
            "status": "ARM_CLEAN_BUILD_PASS",
            "project": "261013-Individual_Demonstration_Connectivity",
            "configuration": "Debug",
            "firmware_commit": REV,
            "time_utc": datetime.now(timezone.utc).isoformat(),
            "executable_path": str(self.elf),
            "executable_sha256": hashlib.sha256(data).hexdigest(),
            "executable_bytes": 4096,
            "physical_tests_performed": False,
            "device_flashed": False
        }
        self.save()

    def save(self):
        self.report.write_text(json.dumps(self.payload), encoding="utf-8")

    def issues(self):
        return verify(self.report, self.root, REV, self.started)

    def test_valid_synthetic_binary_metadata(self):
        self.assertEqual(self.issues(), [])

    def test_stale_report_is_rejected(self):
        self.payload["time_utc"] = (self.started - timedelta(minutes=20)).isoformat()
        self.save()
        self.assertTrue(any("predates" in x for x in self.issues()))

    def test_wrong_revision_rejected(self):
        self.payload["firmware_commit"] = "b" * 40
        self.save()
        self.assertTrue(any("revision" in x for x in self.issues()))

    def test_corrupt_executable_rejected(self):
        self.elf.write_bytes(b"zero" * 1024)
        self.assertTrue(any("ELF" in x or "SHA-256" in x for x in self.issues()))

    def test_executable_moved_outside_output_rejected(self):
        elsewhere = self.root / "unexpected.elf"
        elsewhere.write_bytes(self.elf.read_bytes())
        self.payload["executable_path"] = str(elsewhere)
        self.save()
        self.assertTrue(any("outside" in x for x in self.issues()))

    def test_claim_of_flash_rejected(self):
        self.payload["device_flashed"] = True
        self.save()
        self.assertTrue(any("flashing" in x for x in self.issues()))

    def test_missing_report_rejected(self):
        self.report.unlink()
        self.assertTrue(any("cannot be read" in x for x in self.issues()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
