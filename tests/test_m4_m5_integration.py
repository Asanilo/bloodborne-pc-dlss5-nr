"""Shared launcher -> public entry -> Runner admission, CPU-only and no process launch."""
import contextlib
import io
from pathlib import Path
import shlex
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'launcher'))
sys.path.insert(0,str(ROOT/'tools/nr_dev'))
import nrbridge_launcher as bridge
import nrbridge_entry as entry
import play

class IntegratedSelection(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.home=self.root/'runtime';self.home.mkdir()
        self.native=self.root/'native';self.native.mkdir()
        self.settings=self.root/'settings.json';self.settings.write_text('{}')
        self.ini=self.root/'bbport.ini';self.ini.write_text('')
        self.config=self.root/'inputs.json';self.config.write_text('{}')

    def forward(self,settings,ini):
        with patch.object(bridge.fg_config,'availability',return_value='fixture'):
            c=bridge.player_command(settings,ini,home_path=self.home,settings_path=self.settings,
                ini_path=self.ini,data_dir=self.root,session='cpu-integrated')
        output=io.StringIO()
        with contextlib.redirect_stdout(output),patch.object(entry.subprocess,'call',side_effect=AssertionError('no process allowed')):
            self.assertEqual(entry.main(c[2:]+['--native-build',str(self.native),
                '--local-inputs',str(self.config),'--print-command']),0)
        return play.parse_args(shlex.split(output.getvalue())[2:])

    def test_all_mfg_multipliers_survive_entry_and_keep_nr(self):
        for multiplier in range(2,7):
            with self.subTest(multiplier=multiplier):
                args=self.forward(dict(fg_backend='dlssg',fg_dlss_multiplier=multiplier,
                    fg_dlss_pipeline='consumer-queue-v1',fg_dlss_model='/model/user-prepared.dll',
                    fg_dlss_runtime='/runtime/prepared',fg_dlss_prefix_session='retired-prefix'),dict(bridge.NR_INI))
                self.assertEqual(args.dlssg_multiplier,multiplier)
                self.assertEqual(args.experimental_fg,'dlssg');self.assertTrue(args.fg_native)
                self.assertFalse(args.sr_only);self.assertIsNone(args.nr_sr_preset)

    def test_nr_sizes_and_native_only_survive_same_entry(self):
        for name,preset in [('native','3'),('dlaa','0'),('quality','1'),('balanced','2'),('performance','3')]:
            with self.subTest(mode=name):
                args=self.forward(dict(fg_backend='off',nrbridge_dimensions=True),
                    dict(bridge.NR_INI,upscaler='off' if name=='native' else 'dlss',preset=preset,output_res='2560x1440'))
                self.assertEqual((args.nr_sr_preset,args.nr_output,args.nr_execution),(name,'2560x1440','1280x720'))
                self.assertFalse(args.sr_only);self.assertIsNone(args.experimental_fg)
                self.assertEqual(args.dlssg_multiplier,2)

    def test_unvalidated_mixed_profiles_are_rejected_at_runner_boundary(self):
        for backend in ('fsr31','dlssg'):
            with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                play.parse_args(['--daily','--nr-sr-preset','quality','--nr-output','2560x1440',
                    '--fg-backend',backend,'--dlssg-model','/model/user-prepared.dll'] if backend=='dlssg' else
                    ['--daily','--nr-sr-preset','quality','--nr-output','2560x1440','--fg-backend',backend])

    def test_default_stays_x2_and_smooth_motion_stays_quarantined(self):
        args=self.forward({},dict(bridge.NR_INI))
        self.assertEqual(args.dlssg_multiplier,2);self.assertEqual(args.fg_backend,'off')
        self.assertIsNone(args.nr_sr_preset)
        with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
            play.parse_args(['--daily','--fg-backend','smooth-motion'])

if __name__=='__main__':unittest.main()
