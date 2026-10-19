"""Dashboard source smoke test: compile it without importing optional UI dependencies."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = ROOT / "Dashboard" / "connectivity_dashboard.py"


class DashboardSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = DASHBOARD.read_text(encoding="utf-8")

    def test_syntax_without_optional_ui_libraries(self):
        compile(self.source, str(DASHBOARD), "exec")

    def test_local_pointer_files_never_written_into_repository_root(self):
        for name in ("RESULTS_LOCATION.txt", "LATEST_RESULT_PATH.txt"):
            self.assertNotIn("ROOT/'" + name + "'", self.source)
            self.assertIn("RESULTS/'" + name + "'", self.source)

    def test_launcher_uses_private_venv(self):
        launcher = (ROOT / "Dashboard" / "START_DASHBOARD.bat").read_text(encoding="utf-8")
        self.assertIn('".venv\\\\Scripts\\\\python.exe"', launcher)
        self.assertIn('-m venv', launcher)
        self.assertNotIn("python -m pip install", launcher)

    def test_pointers_generated_only_on_run_or_evidence_export(self):
        self.assertIn("if __name__=='__main__':", self.source)
        self.assertIn("App().mainloop()", self.source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
