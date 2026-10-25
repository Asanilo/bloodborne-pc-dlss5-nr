import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import fg_preferences as prefs


class PreferencesTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.run=self.root/'run';self.run.mkdir()
        (self.run/'profile').mkdir();self.data=self.root/'data';self.data.mkdir()
        self.owner=self.root/'owner.json';self.owner.write_text('{"nr":true,"sr":"quality"}')
        self.snapshot=dict(data_dir=str(self.data),fg_preferences_owner=str(self.owner),
            fg_preferences_owner_sha256=prefs.digest(self.owner))
        (self.run/'player-settings.json').write_text(json.dumps(self.snapshot))
        self.ini=self.run/'profile/bbport.ini'
        self.ini.write_text('frame_generation_backend=dlssg\nframe_generation_multiplier=6\nframe_generation=1\n')
        self.result=dict(status='PASS_NORMAL_TEARDOWN',returncode=0,native_lastuse_proof=True,residuals=[])

    def test_normal_retirement_persists_only_fg_and_separates_owners(self):
        before=self.owner.read_bytes()
        self.assertEqual(prefs.commit(self.run,self.result)['status'],'saved')
        self.assertEqual(prefs.read(self.owner,self.data),dict(fg_backend='dlssg',fg_dlss_multiplier=6,fg_enabled=True))
        self.assertEqual(self.owner.read_bytes(),before)
        self.assertEqual(prefs.read(self.root/'other-owner.json',self.data),{})

    def test_unproved_or_abnormal_retirement_never_commits(self):
        for change in (dict(status='HOLD'),dict(returncode=1),dict(native_lastuse_proof=False),dict(residuals=[123])):
            self.assertEqual(prefs.commit(self.run,dict(self.result,**change))['status'],'not_applied_unretired')
        self.assertEqual(list(self.data.iterdir()),[])

    def test_external_settings_edit_wins_over_game_request(self):
        self.owner.write_text('{"nr":false}')
        self.assertEqual(prefs.commit(self.run,self.result)['status'],'retained_owner_changed')
        self.assertEqual(list(self.data.iterdir()),[])

    def test_symlink_destination_is_rejected_without_modifying_target(self):
        target=self.root/'keep';target.write_text('keep')
        prefs.path_for(self.owner,self.data).symlink_to(target)
        with self.assertRaises(ValueError):prefs.commit(self.run,self.result)
        self.assertEqual(target.read_text(),'keep')

    def test_invalid_or_driver_backend_is_not_silently_selected(self):
        for backend,count in [('unknown',2),('smooth-motion',2),('fsr31',6),('dlssg',7)]:
            self.ini.write_text(f'frame_generation_backend={backend}\nframe_generation_multiplier={count}\nframe_generation=1\n')
            with self.assertRaises(ValueError):prefs.commit(self.run,self.result)
        self.assertEqual(list(self.data.iterdir()),[])

    def test_off_is_explicit_and_duplicate_requests_are_rejected(self):
        self.ini.write_text('frame_generation_backend=off\nframe_generation_multiplier=2\nframe_generation=1\n')
        prefs.commit(self.run,self.result)
        self.assertFalse(prefs.read(self.owner,self.data)['fg_enabled'])
        self.ini.write_text(self.ini.read_text()+'frame_generation_multiplier=3\n')
        with self.assertRaises(ValueError):prefs.commit(self.run,self.result)

    def test_corrupt_stored_multiplier_does_not_fall_back(self):
        prefs.commit(self.run,self.result)
        path=prefs.path_for(self.owner,self.data);obj=json.loads(path.read_text());obj['multiplier']=True
        path.write_text(json.dumps(obj))
        with self.assertRaises(ValueError):prefs.read(self.owner,self.data)

if __name__=='__main__':unittest.main()
