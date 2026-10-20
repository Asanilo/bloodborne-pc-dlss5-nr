"""Private account/environment selection with synthetic credentials; no network/session."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('daily_online_test', HERE / 'daily_online.py')
online = importlib.util.module_from_spec(spec); spec.loader.exec_module(online)


class DailyOnline(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name); self.profile = self.root / 'profile'; self.profile.mkdir()
        self.build = self.root / 'build'; (self.build / 'gpu').mkdir(parents=True)
        self.module = self.build / 'gpu/libbbnet20.so'; self.module.write_bytes(b'CPU module fixture')
        self.account = self.root / 'settings.json'
        self.settings = {'thd_online_id':'Fixture', 'thd_token':'SYNTHETIC_THD_SECRET',
                         'online_npid':'Fixture', 'online_password':'SYNTHETIC_SHAD_SECRET',
                         'online_upnp':False, 'unrelated_private_setting':'NEVER_COPY'}
        self.account.write_text(json.dumps(self.settings))

    def args(self, pool='thd', **extra):
        return argparse.Namespace(**({'daily':True, 'online_pool':pool,
                                     'online_account_config':str(self.account), 'bbsc':False} | extra))

    def test_thd_environment_and_private_copy_never_publish_secrets(self):
        original = self.account.read_bytes()
        env, public, module = online.prepare(self.args(), self.build, self.profile)
        self.assertEqual(module, self.module)
        self.assertEqual(env['BB_NET_MODULE'], str(self.module))
        self.assertEqual(env['BB_THD_TOKEN'], self.settings['thd_token'])
        self.assertNotIn('BB_SHADNET_PASSWORD', env)
        self.assertNotIn('BB_BBSC', env)
        account = self.profile / 'online-account.json'
        self.assertEqual(account.stat().st_mode & 0o777, 0o600)
        copied = json.loads(account.read_text())
        self.assertEqual(set(copied), {'thd_online_id', 'thd_token'})
        manifest = {'environment':online.public_environment(env), 'online':public}
        self.assertNotIn('SYNTHETIC_THD_SECRET', json.dumps(manifest))
        self.assertNotIn('SYNTHETIC_SHAD_SECRET', json.dumps(manifest))
        self.assertEqual(self.account.read_bytes(), original)

    def test_shadnet_bbsc_only_explicit_and_environment_redacted(self):
        env, _, _ = online.prepare(self.args('shadnet',bbsc=True), self.build, self.profile)
        self.assertEqual(env['BB_BBSC'], '1')
        self.assertEqual(env['BB_SHADNET_PASSWORD'], self.settings['online_password'])
        self.assertNotIn('BB_THD_TOKEN', env)
        self.assertNotIn('BB_SHADNET_PASSWORD', online.public_environment(env))
        self.assertNotIn('BB_NET_BACKEND', env)
        self.assertEqual(env['BB_UPNP'], '0')

    def test_offline_never_reads_account_or_selects_module(self):
        self.account.unlink()
        env, record, module = online.prepare(self.args('offline', online_account_config=None), self.build, self.profile)
        self.assertEqual(env, {}); self.assertEqual(record, {'pool':'offline'}); self.assertIsNone(module)
        self.assertFalse((self.profile / 'online-account.json').exists())

    def test_invalid_selection_and_unknown_module_refused(self):
        for args in (self.args('offline'), self.args(online_account_config=None), self.args(bbsc=True),
                     self.args('shadnet',bbsc=1), self.args(daily=False)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                online.validate_args(args)
        self.module.unlink()
        self.assertRaisesRegex(ValueError, 'online module', online.prepare, self.args(), self.build, self.profile)

    def test_bad_or_unapproved_account_values_do_not_copy_private_state(self):
        linked = self.root / 'linked.json'; linked.symlink_to(self.account)
        self.assertRaises(ValueError, online.read_account, str(linked))
        for change in ({'thd_token':False}, {'online_upnp':1}, {'thd_server':'https://user:password@example.com'}):
            self.account.write_text(json.dumps(self.settings | change))
            self.assertRaises(ValueError, online.prepare, self.args(), self.build, self.profile)
        self.assertFalse((self.profile / 'online-account.json').exists())


if __name__ == '__main__':
    unittest.main()
