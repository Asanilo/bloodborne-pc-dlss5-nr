from paths import ROOT
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(ROOT / "scripts"))
from frame_report import summarize
from install_native import install, activate_release
from manager_entries import export


class PcToolsTests(unittest.TestCase):
    def test_frame_percentiles_ignore_warmup_and_invalid_frames(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "frames.csv"
            path.write_text("t_s,frame_ms,image_mb,buffer_mb\n0,900,20,10\n5,10,20,10\n"
                            "6,20,30,10\n7,60,40,15\n8,nan,40,15\n9,0,40,15\n10,broken,,\n")
            result = summarize(path)
            self.assertEqual(result["frames"], 3)
            self.assertEqual(result["median_ms"], 20)
            self.assertEqual(result["p99_ms"], 60)
            self.assertAlmostEqual(result["render_fps"], 1000 / 30)
            self.assertEqual(result["tracked_image_mb_peak"], 40)
            self.assertEqual(result["over_50ms"], 1)

    def test_manager_entries_reference_same_native_wrapper(self):
        with tempfile.TemporaryDirectory(prefix="bb game ") as folder:
            root = Path(folder)
            (root / "launch.sh").touch()
            desktop, lutris = export(root, root / "managers")
            self.assertIn(f'Exec="{root}/launch.sh" --play', desktop.read_text())
            config = json.loads(lutris.read_text())
            self.assertEqual(config["runner"], "linux")
            self.assertEqual(config["script"]["game"]["exe"], str(root / "launch.sh"))
            self.assertEqual(config["script"]["game"]["args"], "--play")

    def test_runtime_updates_preserve_config_saves_and_local_wrapper_fixes(self):
        with tempfile.TemporaryDirectory() as folder:
            root, source = Path(folder) / "game", Path(folder) / "source"
            root.mkdir()
            for file in ("out/bb-probe", "out/bb-gpu-capabilities", "out/gpu/libbbgpu.so", "run.sh", "LICENSE"):
                path = source / file
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("test binary")
            for name in ("scripts", "launcher", "patches", "tools/fsr4cap"):
                (source / name).mkdir(parents=True)
            wrapper = '#!/bin/bash\nexport LOCAL_FIX=1\nexec "$bb_root/runtime/AppRun" "$@"\n'
            (root / "launch.sh").write_text(wrapper)
            (root / "bbport.ini").write_text("personal config")
            (root / "data/saves/savedata").mkdir(parents=True)
            (root / "data/saves/savedata/file").write_text("personal save")
            with patch("install_native.subprocess.run", return_value=subprocess.CompletedProcess([], 0, "", "")):
                first = install(source, root)
                # A DLSS-enabled update is prepared without switching the live runtime.
                for file in ("out/libbbport_dlss.so", "out/libnvidia-ngx-dlss.so.test", "out/NVIDIA-DLSS-LICENSE.txt"):
                    (source / file).write_text(file)
                second = install(source, root, activate=False)
                self.assertEqual((root / "runtime/current").resolve(), first)
                self.assertTrue((second / "bin/libbbport_dlss.so").is_file())
                self.assertTrue((second / "bin/libnvidia-ngx-dlss.so.test").is_file())
                self.assertTrue((second / "NVIDIA-DLSS-LICENSE.txt").is_file())
                activate_release(root, second)
                activate_release(root, second)
                self.assertEqual((root / "runtime/previous").resolve(), first)
                (source / "out/NVIDIA-DLSS-LICENSE.txt").unlink()
                with self.assertRaisesRegex(ValueError, "Incomplete DLSS"):
                    install(source, root)
                self.assertEqual((root / "runtime/current").resolve(), second)
            self.assertNotEqual(first, second)
            self.assertEqual((root / "runtime/previous").resolve(), first)
            self.assertEqual((root / "runtime/current").resolve(), second)
            self.assertEqual(next(root.glob("launch-before-native-*.sh")).read_text(), wrapper)
            self.assertIn("export LOCAL_FIX=1", (root / "launch.sh").read_text())
            self.assertEqual((root / "bbport.ini").read_text(), "personal config")
            self.assertEqual((root / "data/saves/savedata/file").read_text(), "personal save")


if __name__ == "__main__":
    unittest.main()
