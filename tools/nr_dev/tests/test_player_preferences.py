import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('prefs',Path(__file__).parents[1]/'player_preferences.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)


class PlayerPreferencesTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.run=self.root/'run';(self.run/'profile').mkdir(parents=True)
        self.ini=self.root/'bbport.ini';self.text='# player\npreset=1\nkey.cross=Left Ctrl+F\nmouse_sensitivity=2.75\n'
        self.ini.write_text(self.text)
        (self.run/'player-settings.json').write_text(json.dumps(dict(ini_path=str(self.ini),ini_text=self.text,ini_sha256=hashlib.sha256(self.ini.read_bytes()).hexdigest())))
        (self.run/'profile/bbport.ini').write_text('preset=3\nkey.cross=Space\nmouse_sensitivity=1\noutput_res=2560x1440\n')
        self.result=dict(status='PASS_NORMAL_TEARDOWN',returncode=0,native_lastuse_proof=True,residuals=[])

    def test_merges_graphics_only_without_touching_player_controls(self):
        self.assertEqual(p.commit(self.run,self.result)['status'],'saved')
        self.assertIn('preset=3',self.ini.read_text());self.assertIn('output_res=2560x1440',self.ini.read_text())
        self.assertIn('key.cross=Left Ctrl+F',self.ini.read_text());self.assertIn('mouse_sensitivity=2.75',self.ini.read_text())

    def test_does_not_overwrite_external_settings_or_unretired(self):
        self.ini.write_text(self.text+'sharpen=0\n')
        self.assertEqual(p.commit(self.run,self.result)['status'],'retained_external_changes')
        self.assertEqual(p.commit(self.run,dict(self.result,status='HOLD'))['status'],'retained_unretired')
        self.assertNotIn('preset=3',self.ini.read_text())

    def test_pruning_preserves_prefix_and_failed_and_two_recent_sessions(self):
        for i in range(5):
            run=self.root/str(i);(run/'profile/user/savedata').mkdir(parents=True)
            (run/'player-settings.json').write_text('{}')
            result=dict(self.result,player_save_sync={'status':'synced'})
            if i==1:result['status']='HOLD'
            if i==0:(run/'profile/wine-prefix').mkdir()
            (run/'result.json').write_text(json.dumps(result))
        plan=p.prune_save_copies(self.root,self.root/'4')
        self.assertEqual(plan['redundant_save_copies'],[str(self.root/'2/profile/user/savedata')])
        self.assertTrue((self.root/'2/profile/user/savedata').is_dir())
        p.prune_save_copies(self.root,self.root/'4',apply=True)
        self.assertFalse((self.root/'2/profile/user/savedata').exists())
        self.assertTrue((self.root/'0/profile/wine-prefix').is_dir())
        self.assertTrue((self.root/'1/profile/user/savedata').is_dir())
