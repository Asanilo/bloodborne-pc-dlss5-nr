import ast
import inspect
import os
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).parents[1]
def load(name):
    spec = importlib.util.spec_from_file_location('mouse_test_' + name, ROOT / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

scope = load('native_mouse_scope')
r = load('runner')
c = load('context_recovery')

class CandidateSelection(unittest.TestCase):
    def args(self, **changes):
        return SimpleNamespace(**(dict(native_mouse=True, pm4_cold_trace=True,
                                      reboot_diagnostic='/scope', context_recovery=None) | changes))

    def test_default_does_not_inherit_host_environment(self):
        self.assertFalse(scope.requested(SimpleNamespace()))
        scope.check_record({'schema': 'nr-isolated-context-v1'}, False)
        scope.check_environment({}, {})
        for value in ('1', '0', '', True):
            with self.assertRaises(ValueError):
                scope.check_environment({}, {'BB_NATIVE_MOUSE': value})

    def test_actual_runner_environment_filters_host_and_requires_explicit_selection(self):
        tree = ast.parse(inspect.getsource(r.launch_supervised))
        assignments = {node.targets[0].id: node for node in ast.walk(tree)
                       if isinstance(node, ast.Assign) and len(node.targets) == 1
                       and isinstance(node.targets[0], ast.Name)
                       and node.targets[0].id in ('base_keys', 'env')}
        selection = next(node for node in ast.walk(tree) if isinstance(node, ast.If)
                         and ast.unparse(node.test) == 'native_mouse_scope.requested(args)')
        for selected in (False, True):
            namespace = dict(os=os, native_mouse_scope=scope,
                             args=self.args(native_mouse=selected), nr={})
            with patch.dict(os.environ, {'BB_NATIVE_MOUSE': '1', 'BB_ONLINE': '1',
                                         'BB_BBSC': '1', 'BB_RE_CAMERA': '1'}):
                code = ast.fix_missing_locations(ast.Module(body=[assignments['base_keys'],
                           assignments['env'], selection], type_ignores=[]))
                exec(compile(code, str(ROOT / 'runner.py'), 'exec'), namespace)
            self.assertFalse(any(k in namespace['env'] for k in
                                 ('BB_NATIVE_MOUSE', 'BB_ONLINE', 'BB_BBSC', 'BB_RE_CAMERA')))
            self.assertEqual(namespace['nr'].get('BB_NATIVE_MOUSE'), '1' if selected else None)

    def test_exact_one_scope_cold_and_strict_boolean(self):
        self.assertTrue(scope.requested(self.args()))
        for changes in ({'native_mouse': 1}, {'native_mouse': '1'},
                        {'pm4_cold_trace': 1}, {'pm4_cold_trace': False},
                        {'reboot_diagnostic': None}, {'context_recovery': '/other'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                scope.requested(self.args(**changes))

    def test_v2_explicit_binding_only(self):
        for schema in ('nr-reboot-sr-diagnostic-v2', 'nr-isolated-context-v2'):
            scope.check_record({'schema': schema, 'native_mouse': True}, True)
        for record in ({}, {'native_mouse': '1'}, {'native_mouse': 1},
                       {'native_mouse': False}, {'native_mouse': True, 'schema': 'nr-isolated-context-v1'}):
            with self.assertRaises(ValueError): scope.check_record(record, True)
        with self.assertRaises(ValueError): scope.check_record({'native_mouse': True}, False)

    def test_manifest_patches_and_selection_rechecked(self):
        record = {'schema': 'nr-isolated-context-v2', 'native_mouse': True}
        environment = {'BB_NATIVE_MOUSE': '1'}
        scope.check_environment(record, environment)
        for variable in ('BB_ONLINE', 'BB_BBSC', 'BB_NET_MODULE', 'BB_RE_CAMERA', 'BB_RE_ENTRY'):
            with self.assertRaises(ValueError):
                scope.check_environment(record, environment | {variable: '1'})
        for value in (None, '0', '', '01'):
            with self.assertRaises(ValueError):
                scope.check_environment(record, {'BB_NATIVE_MOUSE': value})

    def test_recovery_control_rejects_drift_before_trace_validation(self):
        with self.assertRaisesRegex(ValueError, 'manifest selection drift'):
            c._trace_control({'schema': 'nr-isolated-context-v2', 'native_mouse': True}, {})

    def test_reboot_selection_rejected_before_build_or_old_hold_checks(self):
        with self.assertRaisesRegex(ValueError, 'scope request drift'):
            r.validate_reboot_scope({'schema': 'nr-reboot-sr-diagnostic-v2'}, self.args(), '', {}, {}, {})

    def test_invalid_entry_never_starts_launch_setup(self):
        with patch.object(r, 'checked_run') as checked:
            with self.assertRaisesRegex(ValueError, 'exactly one isolated scope'):
                r.launch_supervised(self.args(reboot_diagnostic=None), None)
            checked.assert_not_called()

if __name__ == '__main__': unittest.main()
