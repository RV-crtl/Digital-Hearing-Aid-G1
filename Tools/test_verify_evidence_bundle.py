"""Mutation tests for hashed G1 ZIP evidence packages."""
import hashlib
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from verify_evidence_bundle import verify, REPO, MANIFEST


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "bundle.zip"
        self.data = b"test source evidence"
        self.manifest = {
            "repository": REPO,
            "portable_C_tests_executed": False,
            "arm_compiled_by_this_script": False,
            "physical_hardware_verified_by_this_script": False,
            "artifacts": [{"path": "Assessment/input.csv", "sha256": hashlib.sha256(self.data).hexdigest()}],
        }
        self.write()

    def write(self, data=None, extra=None):
        with zipfile.ZipFile(self.path, "w") as archive:
            archive.writestr(MANIFEST, json.dumps(self.manifest))
            archive.writestr("Assessment/input.csv", data if data is not None else self.data)
            if extra:
                archive.writestr(extra, b"unexpected")

    def issues(self):
        return verify(self.path)

    def test_valid_zip(self):
        self.assertEqual(self.issues(), [])

    def test_tampered_csv_fails(self):
        self.write(b"tampered")
        self.assertTrue(any("SHA-256" in msg for msg in self.issues()))

    def test_extra_file_fails(self):
        self.write(extra="surprise.txt")
        self.assertTrue(any("Unmanifested" in msg for msg in self.issues()))

    def test_unsafe_path_fails(self):
        self.write(extra="../outside.txt")
        self.assertTrue(any("Unsafe" in msg for msg in self.issues()))

    def test_false_arm_evidence_fails(self):
        self.manifest["arm_compiled_by_this_script"] = True
        self.write()
        self.assertTrue(any("ARM build claim" in msg for msg in self.issues()))

    def test_fabricated_physical_pass_fails(self):
        self.manifest["physical_hardware_verified_by_this_script"] = True
        self.write()
        self.assertTrue(any("physical" in msg for msg in self.issues()))

    def test_missing_manifest_fails(self):
        with zipfile.ZipFile(self.path, "w") as z:
            z.writestr("data.txt", "x")
        self.assertTrue(any("Manifest" in msg for msg in self.issues()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
