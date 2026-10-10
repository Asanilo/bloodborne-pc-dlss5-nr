from paths import ROOT
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(ROOT / "scripts"))
import save_manager as saves


class SaveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.user = Path(self.temp.name)
        self.file = self.user / "savedata/1/CUSA03173/SAVEDATA/file"
        self.file.parent.mkdir(parents=True)
        self.file.write_bytes(b"original")

    def test_rotation_preserves_restore_safety_copy(self):
        saved = saves.backup(self.user, "manual")
        self.file.write_bytes(b"new")
        self.assertEqual(saves.request_restore(self.user, saved.name), "restored")
        self.assertEqual(self.file.read_bytes(), b"original")
        for _ in range(12):
            saves.backup(self.user)
        snapshots = saves.snapshots(self.user)
        self.assertEqual(sum("-launch-" in p.name for p in snapshots), 10)
        safety = next(p for p in snapshots if "-before-restore-" in p.name)
        self.assertEqual((safety / self.file.relative_to(self.user)).read_bytes(), b"new")

    def test_corruption_and_symlink_do_not_touch_live_data(self):
        saved = saves.backup(self.user)
        copy = saved / self.file.relative_to(self.user)
        copy.write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            saves.request_restore(self.user, saved.name)
        self.assertEqual(self.file.read_bytes(), b"original")
        copy.unlink()
        copy.symlink_to(self.file)
        with self.assertRaises(ValueError):
            saves.request_restore(self.user, saved.name)
        with self.assertRaises(ValueError):
            saves.request_restore(self.user, "../../outside")

    def test_recovery_of_interrupted_directory_swap(self):
        os.replace(self.user / "savedata", self.user / ".savedata-previous")
        saves.recover(self.user)
        self.assertEqual(self.file.read_bytes(), b"original")

    def test_running_game_queues_restore_and_excludes_second_launch(self):
        saved = saves.backup(self.user)
        ready, finish = self.user / "ready", self.user / "finish"
        code = ("from pathlib import Path; import time,sys; "
                f"Path({str(ready)!r}).touch(); "
                f"finish=Path({str(finish)!r})\n"
                "while not finish.exists(): time.sleep(0.01)\n"
                "sys.exit(7)")
        command = [sys.executable, str(ROOT / "scripts/save_manager.py"), "--user", str(self.user),
                   "run", "--", sys.executable, "-c", code]
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while not ready.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(ready.exists())
            self.file.write_bytes(b"in-game")
            self.assertIn("queued", saves.request_restore(self.user, saved.name))
            self.assertEqual(self.file.read_bytes(), b"in-game")
            duplicate = subprocess.run(command, capture_output=True, text=True, timeout=5)
            self.assertEqual(duplicate.returncode, 1)
            self.assertIn("in use", duplicate.stderr)
            finish.touch()
            out, err = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 7, out + err)
            self.assertEqual(self.file.read_bytes(), b"original")
        finally:
            if child.poll() is None:
                child.kill()
            child.communicate()


if __name__ == "__main__":
    unittest.main()
