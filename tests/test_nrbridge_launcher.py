"""Full launcher NR integration, using real methods with CPU-only GTK stand-ins."""
import argparse
import ast
import contextlib
import fcntl
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

from paths import ROOT
sys.path.insert(0, str(ROOT / 'launcher'))
import nrbridge_launcher as bridge
from test_nrbridge_packaging import _make_fresh_home_fixture
import nrbridge_entry as entry


def frontend_namespace():
    tree = ast.parse((ROOT / 'launcher/bbport_launcher.py').read_text())
    chosen = {'start_game', 'start_nrbridge', 'on_nr_scene_ready', 'stop_game',
              'stop_nrbridge', 'kill_if_running', 'on_close', 'on_line', 'update_fsr411_row',
              'on_fsr411_button', 'on_fsr411_dll', 'start_fsr411_build', 'stop_fsr411_build',
              'update_launch_button', 'logs_dir'}
    cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'LauncherWindow')
    cls.bases = []
    cls.body = [n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name in chosen]
    play = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'play')
    fsr = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'build_fsr411')
    limit = next(n for n in tree.body if isinstance(n, ast.Assign) and
                 any(isinstance(t, ast.Name) and t.id == 'NR_FSR411_LIMIT' for t in n.targets))
    module = ast.fix_missing_locations(ast.Module(body=[limit, cls, play, fsr], type_ignores=[]))
    gio = mock.MagicMock()
    gio.SubprocessFlags.STDOUT_PIPE = 1
    gio.SubprocessFlags.STDERR_MERGE = 2
    gio.SubprocessFlags.STDIN_PIPE = 4
    namespace = dict(nrbridge=bridge, NRBRIDGE=True, Gio=gio,
        GLib=types.SimpleNamespace(Error=RuntimeError, timeout_add_seconds=mock.Mock()),
        Adw=mock.MagicMock(), os=mock.Mock(), signal=mock.Mock(), sys=sys, Path=Path,
        PORT_DIR=ROOT, CONFIG_FILE=Path('/tmp/fixture-settings.json'), DATA_DIR=Path('/tmp/fixture-data'),
        ini_path=lambda: Path('/tmp/fixture-data/bbport.ini'), tr=lambda value: value,
        set_language=mock.Mock(), load_settings=mock.Mock(), load_ini=mock.Mock(),
        connected_gpus=mock.Mock(return_value=[]), save_settings=mock.Mock(), LauncherApp=mock.Mock(),
        subprocess=mock.Mock(), fsr411_build_command=mock.Mock(return_value=(['fixture-converter'], {})))
    exec(compile(module, str(ROOT / 'launcher/bbport_launcher.py'), 'exec'), namespace)
    return namespace


class AdapterTests(unittest.TestCase):
    def test_daily_has_all_saved_paths_and_explicit_modes(self):
        command = bridge.player_command({'online': True, 'online_pool': 'huntersdream',
            'nrbridge_enabled': False, 'nrbridge_native_mouse': False}, {},
            session='fixture', home_path='/tmp/nr-home', settings_path='/tmp/config/settings.json',
            ini_path='/tmp/data/bbport.ini', data_dir='/tmp/data')
        self.assertIn('--daily', command)
        self.assertIn('--no-terminal', command)
        self.assertIn('--sr-only', command)
        self.assertIn('--joystick-mouse', command)
        for flag, value in [('--launcher-settings', '/tmp/config/settings.json'),
                            ('--launcher-ini', '/tmp/data/bbport.ini'),
                            ('--player-data-dir', '/tmp/data'), ('--online-pool', 'thd'),
                            ('--online-account-config', '/tmp/config/settings.json')]:
            self.assertEqual(command[command.index(flag) + 1], value)
        self.assertNotIn('--bbsc', command)
        self.assertFalse(any('run.sh' in x for x in command))

    def test_fg_is_opt_in_and_independent_of_nr(self):
        for nr in (False, True):
            settings = {'nrbridge_enabled': nr}
            original = dict(settings)
            plain = bridge.player_command(settings, dict(bridge.NR_INI))
            self.assertNotIn('--experimental-fg', plain)
            self.assertEqual(settings, original)
            selected = bridge.player_command(dict(settings, nrbridge_fsr_fg=True), dict(bridge.NR_INI))
            self.assertEqual(selected[selected.index('--fg-backend')+1], 'fsr31')
            self.assertEqual('--sr-only' in selected, not nr)

    def test_unified_backends_and_both_dlss_pipelines_keep_nr(self):
        for nr in (False,True):
            for backend in bridge.fg_config.BACKENDS:
                for pipeline in bridge.fg_config.PIPELINES:
                    settings=dict(nrbridge_enabled=nr,fg_backend=backend,fg_dlss_pipeline=pipeline)
                    with mock.patch.object(bridge.fg_config,'availability',return_value='fixture'):
                        c=bridge.player_command(settings,dict(bridge.NR_INI))
                    self.assertEqual(c[c.index('--fg-backend')+1],backend)
                    self.assertEqual('--sr-only' in c,not nr)
                    if backend=='dlssg':self.assertEqual(c[c.index('--dlssg-pipeline')+1],pipeline)
        with self.assertRaisesRegex(ValueError,'NVPRESENT'):
            bridge.player_command({'extra_env':'NVPRESENT_ENABLE_SMOOTH_MOTION=1'},dict(bridge.NR_INI))

    def test_nr_constraints_reject_without_replacing_player_choices(self):
        ini = dict(bridge.NR_INI, preset='3')
        before = dict(ini)
        with self.assertRaisesRegex(ValueError, 'preset=3'):
            bridge.player_command({}, ini)
        self.assertEqual(ini, before)
        for key, value in [('memory_model', 'invalid'), ('as_0_3', True)]:
            with self.assertRaises(ValueError):
                bridge.player_command({key: value}, dict(bridge.NR_INI))
        bridge.player_command({'memory_model': 'auto'}, dict(bridge.NR_INI, aspect_ratio='auto'))
        bridge.player_command({'memory_model': 'new'}, dict(bridge.NR_INI))

    def test_m5_dimensions_are_opt_in_match_saved_profile_and_reject_fg(self):
        ini=dict(bridge.NR_INI,preset='3',output_res='1920x1200',aspect_ratio='16:10')
        settings=dict(nrbridge_dimensions=True,nrbridge_execution='1280x720',fg_backend='off')
        command=bridge.player_command(settings,ini)
        for flag,value in [('--nr-sr-preset','performance'),('--nr-output','1920x1200'),('--nr-execution','1280x720')]:
            self.assertEqual(command[command.index(flag)+1],value)
        self.assertNotIn('--sr-only',command)
        with self.assertRaisesRegex(ValueError,'FSR/CPU'):bridge.player_command(dict(settings,fg_backend='fsr31'),ini)
        with self.assertRaises(ValueError):bridge.player_command(dict(settings,nrbridge_dimensions=False),ini)
        with self.assertRaises(ValueError):bridge.player_command(settings,dict(ini,preset='-1'))
        legacy=bridge.player_command({},dict(bridge.NR_INI))
        self.assertNotIn('--nr-sr-preset',legacy)

    def test_m9_dlss_gpu_profiles_keep_nr_independent(self):
        for enabled in (False,True):
            settings=dict(nrbridge_dimensions=True,nrbridge_execution='1280x720',nrbridge_enabled=enabled,fg_backend='dlssg',fg_dlss_pipeline='consumer-queue-v1',fg_dlss_multiplier=4)
            ini=dict(bridge.NR_INI,preset='1',output_res='2560x1440',aspect_ratio='16:9')
            with mock.patch.object(bridge.fg_config,'availability',return_value='GPU available'):
                command=bridge.player_command(settings,ini)
            self.assertEqual(command[command.index('--nr-output')+1],'2560x1440')
            self.assertEqual(command[command.index('--dlssg-multiplier')+1],'4')
            self.assertEqual('--sr-only' in command,not enabled)
        with mock.patch.object(bridge.fg_config,'availability',return_value='GPU available'):
            with self.assertRaises(ValueError):
                bridge.player_command(dict(settings,fg_dlss_transport='mapped-v1',fg_dlss_multiplier=2),ini)
            with self.assertRaisesRegex(ValueError,'source handoff'):
                bridge.player_command(settings,dict(ini,upscaler='off'))

    def test_m5_native_only_uses_off_without_disabling_nr(self):
        c=bridge.player_command(dict(nrbridge_dimensions=True,fg_backend='off'),dict(bridge.NR_INI,upscaler='off',preset='3'))
        self.assertEqual(c[c.index('--nr-sr-preset')+1],'native');self.assertNotIn('--sr-only',c)

    def test_nr_off_keeps_upstream_options_but_still_managed(self):
        command = bridge.player_command({'nrbridge_enabled': False, 'memory_model': 'new'},
                                        {'upscaler': 'taa', 'preset': '0'})
        self.assertIn('--sr-only', command)
        self.assertTrue(command[1].endswith('nrbridge_entry.py'))

    def test_gpu_and_reserved_env_fail_explicitly(self):
        with self.assertRaisesRegex(ValueError, 'GPU'):
            bridge.player_command({'gpu': 'missing GPU'}, dict(bridge.NR_INI))
        with self.assertRaisesRegex(ValueError, 'BB_GPU_ID'):
            bridge.player_command({'extra_env': 'BB_GPU_ID=9'}, dict(bridge.NR_INI))

    def test_saved_online_mapping_and_no_credential_in_argv(self):
        settings = {'online': True, 'online_pool': 'seamless', 'online_bbsc': True,
                    'online_password': 'never-print-this', 'thd_token': 'private-token'}
        command = bridge.player_command(settings, dict(bridge.NR_INI))
        self.assertEqual(command[command.index('--online-pool') + 1], 'shadnet')
        self.assertIn('--bbsc', command)
        self.assertNotIn('never-print-this', ' '.join(command))
        self.assertNotIn('private-token', ' '.join(command))
        self.assertEqual(bridge.online_mapping(dict(settings, online=False)), ('offline', None, False))

    def test_logs_follow_current_then_last_run_without_creating_unused_session(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runs = root / 'runs'
            current = runs / 'current-session'
            last = runs / 'previous-session'
            current.mkdir(parents=True)
            last.mkdir()
            (root / 'state.json').write_text(json.dumps({'last_session': last.name}))
            self.assertEqual(bridge.logs_directory(current.name, root), current)
            self.assertEqual(bridge.logs_directory(home_path=root), last)
            self.assertEqual(bridge.logs_directory('not-created-yet', root), last)
            self.assertFalse((runs / 'not-created-yet').exists())
            (root / 'state.json').write_text('{broken')
            self.assertEqual(bridge.logs_directory(home_path=root), runs)

    def test_logs_reject_state_path_traversal_and_symlinked_sessions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'runs').mkdir()
            (root / 'outside').mkdir()
            (root / 'runs/linked-session').symlink_to(root / 'outside', target_is_directory=True)
            for value in ('../outside', 'linked-session'):
                (root / 'state.json').write_text(json.dumps({'last_session': value}))
                self.assertEqual(bridge.logs_directory(home_path=root), root / 'runs')

    def test_manifest_activates_and_paths_keep_original_environment(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict(os.environ, {'NRBRIDGE_APP': directory}, clear=True):
            self.assertFalse(bridge.active())
            (Path(directory) / 'nrbridge-bundle.json').write_text('{}')
            self.assertTrue(bridge.active())
            env = bridge.launch_environment(settings_path='/tmp/config/bbport-launcher/settings.json',
                ini_path='/tmp/data/bbport.ini', data_dir='/tmp/data', home_path='/tmp/nr-home')
            self.assertEqual(env['XDG_CONFIG_HOME'], '/tmp/config')
            self.assertEqual(env['BB_DATA_DIR'], '/tmp/data')
            self.assertEqual(env['BB_CONFIG'], '/tmp/data/bbport.ini')


class FrontendLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.ns = frontend_namespace()
        self.window = self.ns['LauncherWindow']()
        self.window.process = mock.Mock()
        self.window.nr_session = 'exact-fixture-session'
        self.window.nr_stopping = False
        self.window.nr_setup_working = False
        self.window.nr_stop_request = None
        self.window.nr_result = None
        self.window.nr_waiting_scene = False
        for method in ('store', 'alert', 'append_log', 'read_line', 'update_launch_button'):
            setattr(self.window, method, mock.Mock())

    def test_logs_button_method_routes_managed_and_upstream_locations(self):
        with mock.patch.object(bridge, 'logs_directory', return_value=Path('/tmp/managed/run')) as managed:
            self.assertEqual(self.window.logs_dir(), Path('/tmp/managed/run'))
            managed.assert_called_once_with('exact-fixture-session')
            self.ns['NRBRIDGE'] = False
            self.assertEqual(self.window.logs_dir(), self.ns['DATA_DIR'] / 'logs')
            managed.assert_called_once()

    def test_preview_blocks_all_fsr_conversion_callbacks_even_after_hold(self):
        self.window.fsr411_build = mock.Mock()
        self.window.choose_dll = mock.Mock()
        for result in (None, {'status': 'HOLD_CLOSE_TIMEOUT'}):
            self.window.nr_result = result
            self.window.on_fsr411_button(None)
            self.window.on_fsr411_dll('/tmp/fixture.dll')
            self.window.start_fsr411_build('/tmp/fixture.dll', None)
            self.window.stop_fsr411_build()
        self.window.choose_dll.assert_not_called()
        self.ns['fsr411_build_command'].assert_not_called()
        self.ns['Gio'].SubprocessLauncher.new.assert_not_called()
        self.ns['os'].killpg.assert_not_called()
        self.window.fsr411_build.force_exit.assert_not_called()
        self.assertEqual(self.window.alert.call_count, 8)

    def test_preview_conversion_button_stays_disabled_after_launch_updates(self):
        self.window.fsr411_build = None
        self.window.fsr411_button = mock.Mock()
        self.window.fsr411_row = mock.Mock()
        self.window.launch_button = mock.Mock()
        self.window.nr_arm_button = mock.Mock()
        self.window.nr_apply_button = mock.Mock()
        self.window.process = None
        self.window.update_fsr411_row()
        self.window.fsr411_button.set_sensitive.assert_called_with(False)
        # update_launch_button is replaced by a mock in setUp; run the actual method.
        self.ns['LauncherWindow'].update_launch_button(self.window)
        self.window.fsr411_button.set_sensitive.assert_called_with(False)
        self.assertIn('separate GPU workflow', self.window.fsr411_row.set_subtitle.call_args.args[0])

    def test_preview_fsr_cli_refuses_without_spawning_and_source_behavior_remains(self):
        with contextlib.redirect_stderr(io.StringIO()) as error:
            self.assertEqual(self.ns['build_fsr411'](['/tmp/fixture.dll']), 2)
        self.assertIn('unavailable in NRBridge Preview', error.getvalue())
        self.ns['fsr411_build_command'].assert_not_called()
        self.ns['subprocess'].call.assert_not_called()
        self.ns['NRBRIDGE'] = False
        self.ns['subprocess'].call.return_value = 19
        self.assertEqual(self.ns['build_fsr411'](['/tmp/fixture.dll']), 19)
        self.ns['fsr411_build_command'].assert_called_once_with('/tmp/fixture.dll')
        self.ns['subprocess'].call.assert_called_once()

    def test_nr_start_routes_to_managed_entry_and_keeps_stdin(self):
        self.window.settings = {}
        self.window.ini = dict(bridge.NR_INI)
        self.window.log_view = mock.Mock()
        self.window.stack = mock.Mock()
        self.window.on_exit = mock.Mock()
        self.window.start_nrbridge()
        launcher = self.ns['Gio'].SubprocessLauncher.new.return_value
        command = launcher.spawnv.call_args.args[0]
        self.assertTrue(command[1].endswith('nrbridge_entry.py'))
        self.assertIn('--launcher-settings', command)
        self.assertEqual(self.ns['Gio'].SubprocessLauncher.new.call_args.args[0] & 4, 4)
        self.assertFalse(any('run.sh' in x for x in command))
        self.window.process.get_stdin_pipe.assert_not_called()

    def test_stop_is_exact_runner_request_without_signals_or_kill_timer(self):
        process = self.window.process
        self.window.stop_game()
        launcher = self.ns['Gio'].SubprocessLauncher.new.return_value
        command = launcher.spawnv.call_args.args[0]
        self.assertEqual(command[-2:], ['stop', 'exact-fixture-session'])
        self.ns['os'].killpg.assert_not_called()
        process.force_exit.assert_not_called()
        self.ns['GLib'].timeout_add_seconds.assert_not_called()
        self.window.kill_if_running(1234)
        self.ns['os'].killpg.assert_not_called()

    def test_close_refuses_live_child_hold_and_outstanding_stop(self):
        self.assertTrue(self.window.on_close(None))
        self.window.store.assert_not_called()
        self.window.process = None
        self.window.nr_result = {'status': 'HOLD_CLOSE_TIMEOUT'}
        self.assertTrue(self.window.on_close(None))
        self.window.nr_result = {'status': 'PASS_NORMAL_TEARDOWN'}
        self.window.nr_stop_request = object()
        self.assertTrue(self.window.on_close(None))
        self.window.nr_stop_request = None
        self.window.fsr411_build = None
        self.assertFalse(self.window.on_close(None))

    def test_scene_gate_requires_prompt_and_user_confirmation(self):
        self.window.on_nr_scene_ready(None)
        self.ns['Adw'].AlertDialog.assert_not_called()
        self.window.nr_waiting_scene = True
        self.window.on_nr_scene_ready(None)
        dialog = self.ns['Adw'].AlertDialog.return_value
        callback = dialog.connect.call_args.args[1]
        self.window.process.get_stdin_pipe.assert_not_called()
        callback(dialog, 'cancel')
        self.window.process.get_stdin_pipe.assert_not_called()
        callback(dialog, 'ready')
        self.window.process.get_stdin_pipe.return_value.write_all.assert_called_once_with(b'\n', None)
        self.assertFalse(self.window.nr_waiting_scene)

    def test_scene_readiness_tracks_ingame_and_launcher_confirmation(self):
        stream = mock.Mock()
        for line, expected in [
                ('NR_SCENE_READY_WAIT: 进入场景后按 F8，在游戏内确认开启 NR。', True),
                ('NR_SCENE_READY_ACCEPTED: 场景确认已接收，正在启动 NR。', False),
                ('进入实际游戏场景后按 Enter 开启 NR', True)]:
            stream.read_line_finish_utf8.return_value = (line, len(line))
            self.window.on_line(stream, None)
            self.assertEqual(self.window.nr_waiting_scene, expected)
        self.assertEqual(self.window.read_line.call_count, 3)

    def test_play_uses_same_managed_command(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            (game / 'eboot.bin').write_text('fixture')
            self.ns['load_settings'].return_value = {'game_dir': str(game), 'nrbridge_enabled': False}
            self.ns['load_ini'].return_value = ({'upscaler': 'taa'}, [])
            # No display: CLI keeps terminal observation through the same entry.
            self.ns['os'].environ = {}
            self.ns['play']()
            command = self.ns['os'].execvpe.call_args.args[1]
            self.assertTrue(command[1].endswith('nrbridge_entry.py'))
            self.assertIn('--sr-only', command)
            self.assertIn('--launcher-ini', command)
            self.assertFalse(any('run.sh' in x for x in command))


class EntryAndReadbackTests(unittest.TestCase):
    def test_desktop_execs_full_launcher_with_original_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            args = argparse.Namespace(home=home, launcher_settings=str(home / 'config/bbport-launcher/settings.json'),
                launcher_ini=str(home / 'data/bbport.ini'), player_data_dir=str(home / 'data'))
            with mock.patch.object(entry.os, 'execve') as execute, mock.patch.object(entry.os, 'chdir'):
                entry.desktop_launcher(args)
            _python, command, env = execute.call_args.args
            self.assertTrue(command[1].endswith('bbport_launcher.py'))
            self.assertEqual(env['BB_CONFIG'], args.launcher_ini)
            self.assertEqual(env['BB_DATA_DIR'], args.player_data_dir)
            self.assertEqual(env['BB_LAUNCHER_SETTINGS'], args.launcher_settings)

    def test_appimage_fsr_conversion_refused_before_any_process_or_dispatch(self):
        with mock.patch.object(entry, 'desktop_launcher') as desktop, \
             mock.patch.object(entry, 'daily') as daily, \
             mock.patch.object(entry.subprocess, 'call') as spawn, \
             contextlib.redirect_stderr(io.StringIO()) as error:
            with self.assertRaises(SystemExit) as raised:
                entry.main(['--build-fsr411', '/tmp/fixture.dll'])
        self.assertEqual(raised.exception.code, 2)
        self.assertIn('NRBridge Preview', error.getvalue())
        self.assertIn('暂不支持', error.getvalue())
        desktop.assert_not_called()
        daily.assert_not_called()
        spawn.assert_not_called()

    def test_appimage_play_flag_dispatches_to_same_full_launcher(self):
        with mock.patch.object(entry, 'desktop_launcher', return_value=17) as desktop:
            self.assertEqual(entry.main(['--play', '--home', '/tmp/fixture-home']), 17)
        self.assertTrue(desktop.call_args.args[0].play)

    def test_dependency_readback_update_preserves_selected_seed_and_existing_save(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = _make_fresh_home_fixture(root)
            home = root / 'private-home'
            attrs = dict(APP=fixture['app'], BUNDLE=fixture['bundle'], TOOLS=fixture['app'] / 'tools/nr_dev',
                BUNDLE_MANIFEST=fixture['app'] / 'nrbridge-bundle.json', _BUNDLE_CACHE=None,
                DEPENDENCY_LOCK=root / 'fixture-gpu.lock', dependency_guard=mock.Mock(return_value=mock.Mock()))
            with mock.patch.multiple(entry, **attrs), mock.patch.dict(os.environ, {'NRBRIDGE_HOME': str(home)}), contextlib.redirect_stdout(io.StringIO()):
                values = dict(game_dir=str(fixture['game']), ge_files=str(fixture['ge']), model=str(fixture['model']),
                    ngx_dir=str(fixture['ngx']), system_ngx=str(fixture['system_ngx']), sysroot_lib=str(fixture['sysroot']))
                bridge.save_dependencies(values, home)
                runtime = bridge.read_runtime(home)
                custom_seed = home / 'seed/selected-player-seed'
                (custom_seed / 'savedata').mkdir(parents=True)
                (custom_seed / 'savedata/character.bin').write_bytes(b'player-character')
                manifest = {'files': [{'relative': 'character.bin', 'sha256': entry.sha256(custom_seed / 'savedata/character.bin')}]}
                (custom_seed / 'manifest.json').write_text(json.dumps(manifest))
                runtime['seed'] = str(custom_seed)
                bridge.runtime_config(home).write_text(json.dumps(runtime))
                before = {p.relative_to(custom_seed): p.read_bytes() for p in custom_seed.rglob('*') if p.is_file()}
                values = bridge.dependency_values(bridge.read_runtime(home))
                self.assertEqual(values['model'], str(fixture['model']))
                bridge.save_dependencies(values, home)
                self.assertEqual(bridge.read_runtime(home)['seed'], str(custom_seed))
                self.assertEqual(before, {p.relative_to(custom_seed): p.read_bytes() for p in custom_seed.rglob('*') if p.is_file()})
                self.assertFalse((custom_seed / 'savedata/.bbport-preview-seed').exists())

    def test_dependency_writes_refuse_busy_gpu_or_global_hold_before_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            lockpath = Path(directory) / 'fixture-gpu.lock'
            home = Path(directory) / 'not-created'
            with mock.patch.object(entry, 'DEPENDENCY_LOCK', lockpath), \
                 mock.patch.object(entry, 'dependency_guard') as guard:
                with lockpath.open('a') as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    with self.assertRaisesRegex(ValueError, 'managed GPU session'):
                        entry.write_config(home, {}, None, force=True)
                guard.assert_not_called()
                guard.return_value.admit.side_effect = ValueError('current-boot global session pending/HOLD')
                with self.assertRaisesRegex(ValueError, 'pending/HOLD'):
                    entry.write_config(home, {}, None, force=True)
                self.assertFalse(home.exists())
                guard.return_value.begin.assert_not_called()
                guard.return_value.retire.assert_not_called()

    def test_entry_forwards_paths_without_automatic_test_continuation(self):
        home = Path('/tmp/fixture-home')
        args = argparse.Namespace(home=home, session='fixture', launcher_settings='/tmp/config/settings.json',
            launcher_ini='/tmp/data/bbport.ini', player_data_dir='/tmp/data', sr_only=True, experimental_fg='fsr31-hudless',
            continue_session=None, new_game=False, online_pool='offline', offline=False,
            online_account_config=None, bbsc=False, joystick_mouse=False, no_terminal=True, print_command=True)
        with mock.patch.object(Path, 'is_file', return_value=True), mock.patch.object(entry, 'ensure_runs'), \
             mock.patch.object(entry, 'verify_private_library_dir'), mock.patch.object(entry, 'choose_continuation') as continuation, \
             mock.patch.object(entry, 'bundle_info', return_value={'daily_ready': True, 'native': {'source': 'a' * 40}}), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(entry.daily(args), 0)
        continuation.assert_not_called()
        self.assertIn('--launcher-settings /tmp/config/settings.json', output.getvalue())
        self.assertIn('--launcher-ini /tmp/data/bbport.ini', output.getvalue())
        self.assertIn('--player-data-dir /tmp/data', output.getvalue())
        self.assertIn('--sr-only', output.getvalue())
        self.assertIn('--experimental-fg fsr31-hudless --fg-native', output.getvalue())

    def test_entry_profile_flag_reaches_existing_daily_profiler(self):
        with mock.patch.object(entry, 'daily', return_value=0) as daily:
            self.assertEqual(entry.main(['--daily', '--fg-profile']), 0)
        args = daily.call_args.args[0]
        args.native_build = '/tmp/fixture-build'
        args.local_inputs = '/tmp/fixture-inputs.json'
        args.session = 'fixture-profile'
        args.no_terminal = args.print_command = True
        args.new_game = True
        with mock.patch.object(Path, 'is_file', return_value=True), \
             mock.patch.object(entry, 'ensure_runs'), \
             mock.patch.object(entry, 'choose_continuation', return_value=None), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(entry.daily(args), 0)
        import shlex
        from tools.nr_dev import play
        line = next(line for line in output.getvalue().splitlines() if '--fg-profile' in line)
        command = shlex.split(line)
        # Exercise the canonical play parser too; entry parsing alone can hide
        # an option that never reaches the actual runner.
        forwarded = play.parse_args(command[2:])
        self.assertTrue(forwarded.fg_profile)
        self.assertIsNone(forwarded.fg_backend)


if __name__ == '__main__':
    unittest.main()
