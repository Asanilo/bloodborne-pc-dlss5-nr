import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('tested_player_settings', Path(__file__).resolve().parents[1] / 'player_settings.py')
p = importlib.util.module_from_spec(spec); spec.loader.exec_module(p)


class PlayerSettingsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'game'; self.game.mkdir(); (self.game / 'eboot.bin').write_bytes(b'game')
        self.data = self.root / 'data'; self.data.mkdir()
        self.settings = self.root / 'settings.json'; self.ini = self.root / 'bbport.ini'
        self.obj = {'game_dir': str(self.game), 'user_dir': str(self.root / 'saves'),
                    'ui_language': 'zh_CN', 'language': '11', 'fps_mode': '60', 'fps_limit': 60,
                    'mods_enabled': True, 'fullscreen': True, 'gamepad': 'controller-guid',
                    'thd_token': 'SENSITIVE_TOKEN_MUST_NOT_BE_COPIED', 'online_password': 'SECRET_PASSWORD'}
        self.text = '# controls stay intact\nupscaler=dlss\npreset=1\noutput_res=1920x1080\nobject_motion=1\naspect_ratio=16:9\nmouse_sensitivity=2.75\nkey.cross=Left Ctrl+F\npad.cross=A\n'
        self.write()
        self.args = argparse.Namespace(daily=True, launcher_settings=str(self.settings), launcher_ini=str(self.ini),
                                       player_data_dir=str(self.data), sr_only=False, continue_session=None)

    def write(self):
        self.settings.write_text(json.dumps(self.obj)); self.ini.write_text(self.text)

    def test_controls_language_device_mods_and_graphics_are_forwarded(self):
        r = p.load_request(self.args); e = r['environment']
        self.assertEqual(r['ini_text'], self.text)
        self.assertEqual((e['BB_LANGUAGE'], e['BB_MENU_LANGUAGE']), ('11', 'zh_CN'))
        self.assertEqual((e['BB_FPS'], e['BB_FPS_LIMIT'], e['BB_FULLSCREEN']), ('60', '60', '1'))
        self.assertEqual(e['BB_MODS_ENABLED'], '1'); self.assertEqual(e['BB_GAMEPAD'], 'controller-guid')
        self.assertEqual(e['BB_MODS_CONFIG'], str(self.data / 'mods.json'))
        self.assertEqual((e['BB_UPSCALER'], e['BB_PC_MODEL']), ('dlss', '0'))
        self.assertEqual(self.settings.read_text(), json.dumps(self.obj))

    def test_explicit_dimensions_preserve_matching_player_profile(self):
        self.args.nr_sr_preset='performance';self.args.nr_output='2560x1440';self.args.nr_execution='1280x720'
        self.text=self.text.replace('preset=1','preset=3').replace('1920x1080','2560x1440');self.write()
        request=p.load_request(self.args)
        self.assertIn('key.cross=Left Ctrl+F',request['ini_text'])
        self.assertEqual(request['environment']['BB_UPSCALE_PRESET'],'3')
        self.args.nr_sr_preset='quality'
        with self.assertRaisesRegex(ValueError,'matching SR profile'):p.load_request(self.args)

    def test_native_only_keeps_hidden_sr_preset_and_player_binding(self):
        self.args.nr_sr_preset='native';self.args.nr_output='1920x1080';self.args.nr_execution='1280x720'
        self.text=self.text.replace('upscaler=dlss','upscaler=off').replace('preset=1','preset=3');self.write()
        request=p.load_request(self.args)
        self.assertEqual(request['environment']['BB_UPSCALER'],'off');self.assertEqual(request['environment']['BB_DLSS'],'1')
        self.assertEqual(request['environment']['BB_UPSCALE_PRESET'],'3')
        self.assertIn('key.cross=Left Ctrl+F',request['ini_text'])

    def test_snapshot_omits_accounts_and_detects_selected_option_drift(self):
        r = p.load_request(self.args); run = self.root / 'run'; run.mkdir()
        binding = p.write_snapshot(run, r)
        snapshot = (run / 'player-settings.json').read_text()
        self.assertNotIn('SENSITIVE_TOKEN', snapshot); self.assertNotIn('SECRET_PASSWORD', snapshot)
        self.assertNotIn('thd_token', snapshot)
        self.assertEqual(p.verify_snapshot(run, binding, r['environment']), r)
        e = dict(r['environment'], BB_LANGUAGE='1')
        with self.assertRaisesRegex(ValueError, 'selected option drift'):
            p.verify_snapshot(run, binding, e)
        (run / 'player-settings.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'identity drift'):
            p.verify_snapshot(run, binding, e)

    def test_nr_incompatible_upscaler_rejects_without_overwriting_user_choice(self):
        self.text = self.text.replace('upscaler=dlss', 'upscaler=fsr3'); self.write()
        before = self.ini.read_bytes()
        with self.assertRaisesRegex(ValueError, 'NR requires'):
            p.load_request(self.args)
        self.assertEqual(self.ini.read_bytes(), before)
        self.args.sr_only = True; r = p.load_request(self.args)
        self.assertFalse(r['nr_enabled']); self.assertEqual(r['environment']['BB_UPSCALER'], 'fsr3')
        self.assertEqual(r['environment']['BB_DLSS'], '0')

    def test_nr_new_memory_is_explicit_and_keeps_current_sync(self):
        self.obj['memory_model'] = 'new'; self.write()
        r = p.load_request(self.args)
        self.assertTrue(r['nr_enabled'])
        self.assertEqual(r['environment']['BB_PC_MODEL'], '1')
        self.args.sr_only = True
        self.assertEqual(p.load_request(self.args)['environment']['BB_PC_MODEL'], '1')
        self.args.sr_only = False; self.obj['memory_model'] = 'old'; self.obj['as_0_3'] = True; self.write()
        with self.assertRaisesRegex(ValueError, 'synchronization'):
            p.load_request(self.args)

    def test_unknown_memory_model_cannot_silently_select_old(self):
        self.obj['memory_model'] = 'typo'; self.write()
        with self.assertRaisesRegex(ValueError, 'unsupported memory model'):
            p.load_request(self.args)
        self.args.sr_only = True
        with self.assertRaisesRegex(ValueError, 'unsupported memory model'):
            p.load_request(self.args)

    def test_extra_env_cannot_override_owner_or_hook_admission(self):
        for extra in ['BB_NR_DAILY=0', 'BB_NATIVE_MOUSE=1', 'LD_LIBRARY_PATH=/tmp', 'BB_PC_MODEL=1']:
            with self.subTest(extra=extra):
                self.obj['extra_env'] = extra; self.write()
                with self.assertRaisesRegex(ValueError, 'extra variable'):
                    p.load_request(self.args)

    def test_symlinks_and_partial_cli_rejected(self):
        link = self.root / 'linked.json'; link.symlink_to(self.settings); self.args.launcher_settings = str(link)
        with self.assertRaisesRegex(ValueError, 'canonical'):
            p.load_request(self.args)
        self.args.launcher_settings = str(self.settings); self.args.launcher_ini = None
        with self.assertRaisesRegex(ValueError, 'all three'):
            p.load_request(self.args)

    def test_player_save_source_is_not_diagnostic_continuation(self):
        self.args.continue_session = 'private-test'
        with self.assertRaisesRegex(ValueError, 'selected save folder'):
            p.load_request(self.args)

    def test_source_save_default_and_diagnostic_compatibility(self):
        self.obj['user_dir'] = ''; self.write()
        self.assertEqual(p.load_request(self.args)['user_dir'], str(self.data / 'user'))
        self.assertIsNone(p.load_request(argparse.Namespace(daily=True)))

    def test_stdout_statistics_use_bounded_log_without_csv(self):
        self.obj['frame_stats'] = True; self.obj['mangohud'] = True; self.write()
        e = p.load_request(self.args)['environment']
        self.assertEqual(e['BB_FRAME_STATS'], '1'); self.assertNotIn('BB_FRAME_LOG', e)
        self.assertEqual((e['MANGOHUD'], e['DISABLE_MANGOHUD']), ('1', '0'))

    def test_explicit_validation_records_effective_selection_and_cannot_weaken(self):
        self.args.fg_validation = True
        r = p.load_request(self.args)
        self.assertEqual(r['environment']['BB_VK_VALIDATION'], '1')
        run = self.root / 'run'; run.mkdir()
        binding = p.write_snapshot(run, r)
        with self.assertRaisesRegex(ValueError, 'selected option drift'):
            p.verify_snapshot(run, binding, dict(r['environment'], BB_VK_VALIDATION='0'))

    def test_gpu_validation_records_effective_selection(self):
        self.args.gpu_validation = True
        self.assertEqual(p.load_request(self.args)['environment']['BB_VK_VALIDATION'], '1')

    def test_legacy_diagnostic_can_add_core_and_sync_validation(self):
        r = p.load_request(self.args); run = self.root / 'run'; run.mkdir()
        binding = p.write_snapshot(run, r)
        e = dict(r['environment'], BB_VK_VALIDATION='1', BB_VK_VALIDATION_SYNC='1')
        p.verify_snapshot(run, binding, e)
        e.pop('BB_VK_VALIDATION_SYNC')
        with self.assertRaisesRegex(ValueError, 'selected option drift'):
            p.verify_snapshot(run, binding, e)

    def test_booleans_and_duplicate_json_not_coerced(self):
        self.obj['fullscreen'] = 'false'; self.write()
        with self.assertRaisesRegex(ValueError, 'boolean'):
            p.load_request(self.args)
        self.settings.write_text('{"game_dir":"x","game_dir":"y"}')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            p.load_request(self.args)


if __name__ == '__main__':
    unittest.main()
