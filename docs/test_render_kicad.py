"""KiCad 10 plot classification and embedded SVG image regressions."""
from pathlib import Path
import base64
import tempfile
import unittest
from render_kicad import name_for, normalize_embedded_images


class RealKiCadPlots(unittest.TestCase):
    def test_leaf_names_ignore_parent_subsystem_names(self):
        root="260510-Digital_hearing_aid-Control, Connectivity, and Power"
        examples={
            "Audio Input":"Audio",
            "Connectivity":"Connectivity",
            "Power":"Power",
            "STM32L476RG":"STM32",
            "Output":"Output",
            "User Interface":"User Interface",
            "Analog Frontend":"Analog Frontend",
            "Amplifier":"Amplifier",
            "Telecoil Buffer Circuits":"Telecoil Buffer Circuits",
            "Active Bandpass Filter":"Active Bandpass Filter",
        }
        for suffix, expected in examples.items():
            with self.subTest(suffix=suffix):
                self.assertEqual(name_for(Path(root+"-"+suffix+".svg")),expected)
        self.assertEqual(name_for(Path("260510-Digital_hearing_aid.svg")),
                         "260510-Digital hearing aid")

    def test_png_base64_whitespace_is_fixed_without_changing_image(self):
        binary=b"\\x89PNG\\r\\n\\x1a\\n"+b"example source art"
        encoded=base64.b64encode(binary).decode("ascii").rstrip("=")
        spaced=encoded[:10]+"  "+encoded[10:19]+"\\n"+encoded[19:]
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"test.svg"
            path.write_text('<svg><image href="data:image/png;base64,'+spaced+'"/></svg>',
                            encoding="utf-8")
            normalize_embedded_images(path)
            text=path.read_text(encoding="utf-8")
            part=text.split("base64,",1)[1].split('"',1)[0]
            self.assertNotIn(" ",part)
            self.assertNotIn("\\n",part)
            self.assertEqual(base64.b64decode(part),binary)


if __name__=="__main__":
    unittest.main(verbosity=2)
