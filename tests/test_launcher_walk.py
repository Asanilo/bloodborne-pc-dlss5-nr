from paths import ROOT
import unittest


class LauncherWalk(unittest.TestCase):
    def test_slow_walk_is_exposed_without_overriding_saved_bindings(self):
        import ast
        tree = ast.parse((ROOT / 'launcher/bbport_launcher.py').read_text())
        controls = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                        and any(isinstance(t, ast.Name) and t.id == 'CONTROLS' for t in n.targets))
        rows = {row[0]: row for row in controls}
        self.assertEqual(rows['walk'][2:], ('Left Alt', None))
        self.assertEqual(rows['l3'][2], 'Left Ctrl')
        # The existing INI layer preserves an explicit binding, including an intentional unbind.
        namespace = {}
        module = ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef)
                                  and n.name == 'load_ini'], type_ignores=[])
        from tempfile import TemporaryDirectory
        from pathlib import Path
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / 'bbport.ini'
            namespace['ini_path'] = lambda: path
            namespace['INI_DEFAULTS'] = {}
            defaults = ast.Module(body=[n for n in tree.body if isinstance(n, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id in ('EFFECTS', 'INI_DEFAULTS')
                        for t in n.targets)], type_ignores=[])
            exec(compile(defaults, '<launcher defaults>', 'exec'), namespace)
            exec(compile(module, '<launcher ini>', 'exec'), namespace)
            for binding in ('V', ''):
                path.write_text('key.walk=' + binding + '\n')
                values, _ = namespace['load_ini']()
                self.assertEqual(values['key.walk'], binding)
