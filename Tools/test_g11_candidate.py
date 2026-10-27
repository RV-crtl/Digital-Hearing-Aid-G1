"""Check that G11 candidate archives contain exactly allowed files and honest claims."""
import sys
import tempfile
import unittest
import zipfile
import json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from g11_make_candidate import package
class CandidateTests(unittest.TestCase):
    def test_source_package_has_manifest_and_no_physical_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"candidate.zip"
            count,sha=package(path)
            self.assertGreater(count,50)
            self.assertEqual(len(sha),40)
            with zipfile.ZipFile(path) as bundle:
                names=bundle.namelist()
                self.assertEqual(len(names),len(set(names)))
                self.assertIn("G1_SOURCE_CANDIDATE_MANIFEST.json",names)
                payload=json.loads(bundle.read("G1_SOURCE_CANDIDATE_MANIFEST.json"))
                self.assertEqual(payload["git_sha"],sha)
                self.assertFalse(payload["hardware_release_approved"])
                self.assertFalse(payload["custom_pcb_validated_by_this_tool"])
                self.assertFalse(payload["acoustic_safety_validated"])
                self.assertFalse(any(s.startswith("Documentation/Reference/") for s in names))
                self.assertFalse(any(s.endswith(".pdf") for s in names))
if __name__=="__main__":
    unittest.main(verbosity=2)
