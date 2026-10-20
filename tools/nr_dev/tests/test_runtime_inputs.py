"""CPU-only local runtime closure admission; never starts Wine or a GPU."""
import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

BASE = Path(__file__).parents[1]
spec = importlib.util.spec_from_file_location('runtime_inputs_test', BASE / 'runtime_inputs.py')
i = importlib.util.module_from_spec(spec)
spec.loader.exec_module(i)
spec = importlib.util.spec_from_file_location('runtime_runner_test', BASE / 'runner.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class Inputs(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'localinputs').mkdir()
        self.path = self.root / 'localinputs/runtime.json'
        self.pins = self.root / 'localinputs/runtime-pins-v1.json'
        self.files = []
        def file(name, data=b'fixture'):
            p = self.root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
            self.files.append(p)
            return str(p)
        self.file = file
        asset = file('private-assets/eboot.bin')
        savedata = file('private-seed/savedata/a/save.dat')
        seed_manifest = file('private-seed/manifest.json', json.dumps({
            'files': [{'relative': 'a/save.dat', 'sha256': i.sha(savedata)}]}).encode())
        self.helper = file('build/helper-runtime-model/new/wine_runtime_model.exe')
        self.manifest_path = Path(self.helper).parent / 'build.json'
        self.manifest = {'schema': 'nrbridge-helper-build-v1', 'status': 'BUILD_ONLY_PASS',
                         'artifact': self.helper, 'artifact_sha256': i.sha(self.helper),
                         'protocol': i.HELPER_PROTOCOL | {
                             'exports': {x: 'a' * 64 for x in ('native_runtime_model.h', 'pre_sr_wire.h', 'pre_sr_spec.h', 'effective-spec.json')},
                             'export_manifest_sha256': 'b' * 64}}
        file(str(self.manifest_path.relative_to(self.root)), json.dumps(self.manifest).encode())
        model = file('runtime/model.dll')
        forwarder = file('runtime/forwarder.dll')
        system_ngx = file('runtime/libnvidia-ngx.so.VERSION')
        file('ge-files/lib/wine/x86_64-unix/wine')
        file('ge-files/lib/wine/x86_64-unix/wine-preloader')
        file('ge-files/share/wine/wine.inf')
        file('sysroot-lib/libSDL.so')
        file('ngx-dir/_nvngx.dll')
        file('ngx-dir/nvngx.dll')
        self.config = {'schema': i.SCHEMA,
                       'assets': {'path': str(Path(asset).parent), 'access': 'read-only'},
                       'seed': str(Path(seed_manifest).parent),
                       'ge_files': str(self.root / 'ge-files'),
                       'sysroot_lib': str(self.root / 'sysroot-lib'),
                       'helper': {'path': self.helper, 'manifest_sha256': i.sha(self.manifest_path)},
                       'forwarder': forwarder, 'model': model, 'system_ngx': system_ngx, 'ngx_dir': str(self.root / 'ngx-dir'),
                       'pins': {'path': str(self.pins), 'sha256': ''}}
        self.repin()

    def write(self):
        self.path.write_text(json.dumps(self.config))

    def repin(self):
        pins = [{'path': str(p), 'real_path': str(p.resolve()), 'bytes': p.stat().st_size,
                 'sha256': i.sha(p)} for p in self.files]
        self.pins.write_text(json.dumps({'schema': i.PINS_SCHEMA, 'pins': pins}))
        self.config['pins']['sha256'] = i.sha(self.pins)
        self.write()

    def load(self):
        return i.load(self.path, self.root)

    def test_valid_explicit_closure_without_historical_inputs(self):
        binding = self.load()
        self.assertEqual(i.recheck(binding, self.root), binding)
        helper = i.helper_config(binding)
        self.assertEqual(set(helper), {'schema', 'model', 'forwarder', 'ngx_dir', 'ge_files'})
        self.assertEqual(helper['schema'], 'nrbridge-helper-runtime-v1')
        self.assertFalse((self.root / 'inputs/runtime-pins.json').exists())
        self.assertFalse((self.root / 'runs').exists())

    def test_missing_or_modified_actual_runtime_files_rejected(self):
        for key in ('model', 'forwarder', 'system_ngx'):
            p = Path(self.config[key]);original = p.read_bytes()
            p.write_bytes(b'changed')
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'runtime pin drift'):
                self.load()
            p.write_bytes(original)
        Path(self.config['model']).unlink()
        self.assertRaises(ValueError, self.load)

    def test_unpinned_loader_file_rejected(self):
        self.file('ge-files/lib/new.dll')
        self.assertRaisesRegex(ValueError, 'unpinned', self.load)

    def test_omitted_required_artifact_rejected(self):
        self.files.remove(Path(self.config['model']));self.repin()
        self.assertRaisesRegex(ValueError, 'not pinned', self.load)

    def test_pin_manifest_drift_duplicate_and_bad_schema_rejected(self):
        original = self.pins.read_text()
        self.pins.write_text(original + ' ')
        self.assertRaisesRegex(ValueError, 'manifest SHA', self.load)
        for mutate in (lambda p: p['pins'].append(p['pins'][0]), lambda p: p.update(schema='old')):
            p = json.loads(original);mutate(p)
            self.pins.write_text(json.dumps(p));self.config['pins']['sha256'] = i.sha(self.pins);self.write()
            self.assertRaises(ValueError, self.load)

    def test_symlink_target_identity_drift_rejected_even_same_bytes(self):
        model = Path(self.config['model'])
        target = Path(self.file('runtime/other-model.dll', model.read_bytes()))
        model.unlink();model.symlink_to(target)
        self.assertRaisesRegex(ValueError, 'canonical', self.load)
        # Loader file symlinks are permitted only with the recorded real target.
        loader = self.root / 'sysroot-lib/libSDL.so'
        other = Path(self.file('outside/loader.so', loader.read_bytes()))
        loader.unlink();loader.symlink_to(other)
        model.unlink();model.write_bytes(b'fixture');self.repin()
        self.load()
        other2 = Path(self.file('outside/loader2.so', other.read_bytes()))
        loader.unlink();loader.symlink_to(other2)
        self.assertRaisesRegex(ValueError, 'identity drift', self.load)

    def test_wrong_helper_version_and_manifest_rejected(self):
        for field, value in [('wire_version', 2), ('interface_revision', True), ('interface_revision', 5),
                             ('runtime_model_schema', 'future'), ('exports', {'unknown': 'a' * 64})]:
            manifest = copy.deepcopy(self.manifest);manifest['protocol'][field] = value
            self.manifest_path.write_text(json.dumps(manifest))
            self.config['helper']['manifest_sha256'] = i.sha(self.manifest_path);self.repin()
            with self.subTest(field=field), self.assertRaises(ValueError):self.load()
        self.manifest_path.write_text(json.dumps(self.manifest));self.repin()
        self.assertRaisesRegex(ValueError, 'Helper manifest SHA', self.load)

    def test_daily_capability_requires_real_native_marker_and_revision_four(self):
        build = self.root / 'build/native'; (build / 'gpu').mkdir(parents=True)
        library = build / 'gpu/libbbgpu.so'; library.write_bytes(b'old Native fixture')
        binding = self.load()  # Existing revision 3 remains supported for diagnostics.
        self.assertRaisesRegex(ValueError, 'ring64', i.daily_capability, build, binding)
        library.write_bytes(b'NRBRIDGE_NATIVE_DAILY_RING64_V2')
        self.assertRaisesRegex(ValueError, 'revision 4', i.daily_capability, build, binding)
        self.manifest['protocol']['interface_revision'] = 4
        self.manifest_path.write_text(json.dumps(self.manifest))
        self.config['helper']['manifest_sha256'] = i.sha(self.manifest_path); self.repin()
        binding = self.load()
        i.daily_capability(build, binding)
        self.manifest_path.write_text('{}')
        self.assertRaisesRegex(ValueError, 'SHA drift', i.daily_capability, build, binding)

    def test_malformed_helper_manifest_types_fail_closed(self):
        for manifest in ([], self.manifest | {'protocol': []}):
            self.manifest_path.write_text(json.dumps(manifest))
            self.config['helper']['manifest_sha256'] = i.sha(self.manifest_path);self.repin()
            self.assertRaisesRegex(ValueError, 'Helper.*manifest', self.load)

    def test_wrong_config_version_extra_duplicate_and_relative_paths_rejected(self):
        original = copy.deepcopy(self.config)
        for mutation in ({'schema': 'future'}, {'extra': 1}, {'model': 'relative.dll'},
                         {'assets': original['assets'] | {'access': 'write'}}):
            self.config = original | mutation;self.write()
            self.assertRaises(ValueError, self.load)
        self.config = original;self.write()
        text = self.path.read_text();self.path.write_text(text[:-1] + ',"schema":"' + i.SCHEMA + '"}')
        self.assertRaisesRegex(ValueError, 'duplicate JSON', self.load)

    def test_private_seed_inventory_drift_and_escape_rejected(self):
        self.file('private-seed/savedata/unlisted.dat')
        self.assertRaisesRegex(ValueError, 'seed inventory mismatch', self.load)
        manifest = self.root / 'private-seed/manifest.json'
        manifest.write_text(json.dumps({'files': [{'relative': '../escape', 'sha256': 'a' * 64}]}))
        self.assertRaisesRegex(ValueError, 'seed inventory path', self.load)

    def test_local_config_and_run_confinement(self):
        outside = self.root / 'other.json';outside.write_text(self.path.read_text())
        self.assertRaisesRegex(ValueError, 'ROOT/localinputs', i.load, outside, self.root)
        run = self.root / 'runs/input';run.mkdir(parents=True)
        self.config['sysroot_lib'] = str(run);self.write()
        self.assertRaisesRegex(ValueError, 'overlaps runs', self.load)
        with patch.object(r, 'ROOT', self.root):
            self.assertRaises(ValueError, r.checked_run, '../escape')
            (self.root / 'runs/link').symlink_to(self.root)
            self.assertRaises(ValueError, r.checked_run, 'link')

    def test_binding_drift_rejected_and_old_native_marker_rejected(self):
        binding = self.load()
        self.config['model'] = self.config['forwarder'];self.write()
        self.assertRaisesRegex(ValueError, 'binding drift', i.recheck, binding, self.root)
        build = self.root / 'native-build';(build / 'gpu').mkdir(parents=True)
        lib = build / 'gpu/libbbgpu.so';lib.write_bytes(b'old baseline')
        self.assertRaisesRegex(ValueError, 'does not support', i.native_capability, build)
        lib.write_bytes(b'NRBRIDGE_NATIVE_HELPER_RUNTIME_CONFIG_V1')
        i.native_capability(build)

    def test_old_native_rejected_before_gpu_lock_or_profile_creation(self):
        build = self.root / 'native-build';(build / 'gpu').mkdir(parents=True)
        (build / 'gpu/libbbgpu.so').write_bytes(b'old validated binary')
        args = SimpleNamespace(session='fresh', native_build=None, local_inputs=str(self.path),
                               pm4_producer_context=False)
        with patch.object(r, 'ROOT', self.root), patch.object(r, 'checked_native_build',
                 return_value=(build, [], {})), patch('builtins.open', side_effect=AssertionError('GPU lock opened')):
            self.assertRaisesRegex(ValueError, 'does not support', r.launch_supervised, args, None)
        self.assertFalse((self.root / 'runs/fresh').exists())

    def test_control_refuses_changed_generated_helper_config_before_arming(self):
        binding = self.load()
        run = self.root / 'runs/fresh';run.mkdir(parents=True)
        config = run / 'helper-runtime.json';config.write_text(json.dumps(i.helper_config(binding)))
        manifest = {'runtime_inputs': binding, 'helper_runtime_config': {
            'path': str(config), 'sha256': i.sha(config)}}
        (run / 'manifest.json').write_text(json.dumps(manifest))
        config.write_text(config.read_text() + ' ')
        with patch.object(r, 'ROOT', self.root), patch.object(r, 'current_game', return_value={}):
            self.assertRaisesRegex(ValueError, 'Helper runtime config drift', r.control,
                                   SimpleNamespace(command='arm', session='fresh'))
        self.assertFalse((run / 'native-scene-arm').exists())

    def test_legacy_inventory_still_required_and_unchanged(self):
        inputs = self.root / 'inputs';inputs.mkdir()
        old = inputs / 'runtime-pins.json'
        old.write_text(json.dumps({'pins': [{'path': self.config['model'], 'sha256': i.sha(self.config['model'])}]}))
        original = old.read_bytes()
        with patch.object(r, 'ROOT', self.root):
            r.verify_runtime_inputs()
            Path(self.config['model']).write_bytes(b'drift')
            self.assertRaisesRegex(ValueError, 'runtime pin drift', r.verify_runtime_inputs)
        self.assertEqual(old.read_bytes(), original)

    def test_original_hold_blocks_explicit_config_and_is_never_modified(self):
        result = self.root / 'runs/held/result.json';result.parent.mkdir(parents=True)
        result.write_text('{"status":"HOLD"}')
        original = result.read_bytes()
        with patch.object(r, 'ROOT', self.root), patch.object(r, 'holders', return_value=[]):
            self.assertRaisesRegex(ValueError, 'unresolved new-context HOLD', r.preflight,
                                   None, None, self.load())
        self.assertEqual(result.read_bytes(), original)

    def test_bad_config_rejected_before_run_creation_or_gpu_lock(self):
        self.config['schema'] = 'future';self.write()
        args = SimpleNamespace(session='fresh', native_build=None, local_inputs=str(self.path),
                               pm4_producer_context=False)
        with patch.object(r, 'ROOT', self.root), patch.object(r, 'checked_native_build',
                 return_value=(self.root, [], {})), patch('builtins.open', side_effect=AssertionError('GPU lock opened')):
            # Config loading itself uses Path.open, so patch the lock-facing call only.
            with self.assertRaisesRegex(ValueError, 'unsupported local config'):
                r.launch_supervised(args, None)
        self.assertFalse((self.root / 'runs/fresh').exists())


    def test_private_sealer_does_not_overwrite_and_records_actual_closure(self):
        import sys
        with patch.dict(sys.modules, {'runtime_inputs': i}):
            spec = importlib.util.spec_from_file_location('private_sealer', BASE / 'prepare_runtime_inputs.py')
            prepare = importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)
        output = self.root / 'localinputs/sealed.json'
        binding = prepare.seal(self.path, output, self.root)
        self.assertEqual(i.load(output, self.root), binding)
        original = output.read_bytes()
        self.assertRaisesRegex(ValueError, 'overwrite', prepare.seal, self.path, output, self.root)
        self.assertEqual(output.read_bytes(), original)
        self.config['helper']['manifest_sha256'] = '0' * 64;self.write()
        invalid_output = self.root / 'localinputs/invalid.json'
        self.assertRaisesRegex(ValueError, 'Helper manifest SHA', prepare.seal,
                               self.path, invalid_output, self.root)
        self.assertFalse(invalid_output.exists())
        self.assertFalse(invalid_output.with_name('invalid-pins.json').exists())

    def test_context_recovery_uses_exact_local_binding_without_legacy_pins(self):
        spec = importlib.util.spec_from_file_location('local_context_recovery', BASE / 'context_recovery.py')
        c = importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
        binding = self.load()
        record = {'native_build': {'path': str(self.root), 'provenance': {}, 'artifacts': {}},
                  'helper': {'path': self.helper, 'sha256': i.sha(self.helper),
                             'build_manifest': binding['helper_build']}, 'runtime_inputs': binding}
        with patch.object(r, 'ROOT', self.root), patch.object(r, 'checked_native_build',
                    return_value=(self.root, [], {})):
            c._identities(r, record)
            changed = copy.deepcopy(record)
            changed['runtime_inputs']['sha256'] = '0' * 64
            self.assertRaisesRegex(ValueError, 'binding drift', c._identities, r, changed)
        self.assertFalse((self.root / 'inputs/runtime-pins.json').exists())


if __name__ == '__main__':
    unittest.main()
