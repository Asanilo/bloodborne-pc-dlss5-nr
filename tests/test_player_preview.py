import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from paths import ROOT

spec=importlib.util.spec_from_file_location('player_preview',ROOT/'launcher/play_nrbridge.py')
player=importlib.util.module_from_spec(spec);spec.loader.exec_module(player)


class PlayerPreview(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        for n in ('home','build','data','user'): (self.root/n).mkdir()
        (self.root/'settings.json').write_text(json.dumps({'user_dir':str(self.root/'user')}))
        (self.root/'base.ini').write_text('upscaler=dlss\npreset=1\noutput_res=1920x1080\n')
        (self.root/'inputs.json').write_text('{}')
        self.profile=self.root/'profile.json'
        self.profile.write_text(json.dumps(dict(schema='bbport-player-preview-v1',
            home=str(self.root/'home'),native_build=str(self.root/'build'),
            local_inputs=str(self.root/'inputs.json'),base_settings=str(self.root/'settings.json'),
            base_ini=str(self.root/'base.ini'),player_data_dir=str(self.root/'data'),reuse_nr_prefix='retired')))
        p=patch.object(player.bridge.fg_config,'availability',return_value='checked');p.start();self.addCleanup(p.stop)

    def prepare(self,*args):
        return player.prepare(player.arguments(['--profile',str(self.profile),*args]))[0]

    def test_default_old_nr_quality_standby_x2(self):
        c=self.prepare();self.assertIn('--fg-start-off',c);self.assertNotIn('--sr-only',c)
        s=json.loads(Path(c[c.index('--launcher-settings')+1]).read_text())
        self.assertEqual(s['memory_model'],'old');self.assertTrue(s['nrbridge_native_mouse'])
        self.assertEqual(c[c.index('--dlssg-multiplier')+1],'2')

    def test_offline_camera_defaults_on_with_explicit_older_runtime_fallback(self):
        for options, expected in [((), True), (('--cinematic-camera','off'), False)]:
            c=self.prepare(*options)
            settings=json.loads(Path(c[c.index('--launcher-settings')+1]).read_text())
            self.assertIs(settings['cinematic_camera'],expected)

    def test_new_mfg6_and_sr_only_preserve_selection(self):
        c=self.prepare('--memory-model','new','--nr','off','--fg','dlssg','--multiplier','6','--sr','balanced','--output','2560x1440')
        self.assertIn('--sr-only',c);self.assertNotIn('--reuse-nr-prefix',c);self.assertNotIn('--fg-start-off',c)
        self.assertEqual(c[c.index('--nr-sr-preset')+1],'balanced')
        self.assertEqual(c[c.index('--dlssg-multiplier')+1],'6')

    def test_native_nr_and_fsr_baseline(self):
        c=self.prepare('--sr','native');self.assertNotIn('--sr-only',c)
        self.assertEqual(c[c.index('--fg-backend')+1],'off')
        c=self.prepare('--fg','fsr31');self.assertEqual(c[c.index('--fg-backend')+1],'fsr31')
        self.assertIn('--nr-sr-preset',c)

    def test_fsr_dynamic_profile_and_multiplier_rejection(self):
        for backend in ('fsr31','fsr316'):
            c=self.prepare('--fg',backend,'--nr','off','--sr','balanced','--output','2560x1440')
            self.assertEqual(c[c.index('--nr-sr-preset')+1],'balanced')
            self.assertEqual(c[c.index('--nr-output')+1],'2560x1440')
            self.assertIn('--sr-only',c)
            with self.assertRaisesRegex(ValueError,'x2 only'):
                self.prepare('--fg',backend,'--multiplier','3')

    def test_native_scene_fg_keeps_nr_selection_and_never_requests_sr(self):
        for backend in ('dlssg','fsr31'):
            for nr in ('on','off'):
                c=self.prepare('--fg',backend,'--nr',nr,'--sr','native')
                self.assertEqual(c[c.index('--nr-sr-preset')+1],'native')
                self.assertEqual('--sr-only' in c,nr=='off')
                ini=Path(c[c.index('--launcher-ini')+1]).read_text()
                self.assertIn('upscaler=off',ini)

    def test_bad_session_cannot_escape_private_recipes(self):
        with self.assertRaisesRegex(ValueError,'session'):
            self.prepare('--session','../../outside')
        self.assertFalse((self.root/'outside').exists())

    def test_saved_backend_and_multiplier_restore_but_explicit_cli_wins(self):
        data=self.root/'data'
        record=dict(schema='bbport-fg-preferences-v1',owner=str(self.profile),backend='dlssg',multiplier=5,enabled=True)
        player.fg_preferences.path_for(self.profile,data).write_text(json.dumps(record))
        c=self.prepare('--nr','off')
        self.assertEqual(c[c.index('--dlssg-multiplier')+1],'5')
        self.assertIn('--sr-only',c)
        self.assertNotIn('--fg-start-off',c)
        c=self.prepare('--fg','dlssg','--multiplier','2')
        self.assertEqual(c[c.index('--dlssg-multiplier')+1],'2')
        record.update(backend='off',multiplier=2,enabled=False)
        player.fg_preferences.path_for(self.profile,data).write_text(json.dumps(record))
        c=self.prepare()
        self.assertEqual(c[c.index('--fg-backend')+1],'off')
        self.assertNotIn('--fg-start-off',c)

    def test_saved_disable_retains_dlss_backend_without_enabling_fg(self):
        record=dict(schema='bbport-fg-preferences-v1',owner=str(self.profile),backend='dlssg',multiplier=3,enabled=False)
        player.fg_preferences.path_for(self.profile,self.root/'data').write_text(json.dumps(record))
        c=self.prepare();self.assertIn('--fg-start-off',c)
        self.assertEqual(c[c.index('--dlssg-multiplier')+1],'3')

    def test_explicit_fg_diagnostic_forwarding(self):
        c=self.prepare('--fg-profile')
        self.assertIn('--fg-profile',c)
        self.assertNotIn('--gpu-validation',c)

    def test_sr_only_coordinator_drops_inherited_nr_prefix_dependency(self):
        import runner
        args=runner.parse_args(['start','cpu-no-nr','--daily','--local-inputs','/fixture-inputs','--sr-only','--reuse-nr-prefix','unavailable-nr-prefix'])
        self.assertIsNone(args.reuse_nr_prefix)
        args=runner.parse_args(['start','cpu-with-nr','--daily','--local-inputs','/fixture-inputs','--reuse-nr-prefix','retired-nr-prefix'])
        self.assertEqual(args.reuse_nr_prefix,'retired-nr-prefix')
