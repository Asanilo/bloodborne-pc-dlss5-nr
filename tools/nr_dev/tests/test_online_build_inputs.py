import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('online_inputs', Path(__file__).parents[1] / 'online_build_inputs.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class PreparedOnlineInputs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'usr/lib').mkdir(parents=True)
        (self.root / 'usr/lib/libminiupnpc.so.21').write_bytes(b'fixture')
        (self.root / 'usr/lib/libminiupnpc.so').symlink_to('libminiupnpc.so.21')
        (self.root / 'bbport-online-inputs.json').write_text(json.dumps({'files': {
            'usr/lib/libminiupnpc.so.21': {'sha256': hashlib.sha256(b'fixture').hexdigest()},
            'usr/lib/libminiupnpc.so': {'link': 'libminiupnpc.so.21'}}}))

    def test_inventory_and_bounded_soname_link(self):
        self.assertEqual(len(m.verify(self.root)['files']), 2)

    def test_modified_or_extra_file_rejected(self):
        (self.root / 'usr/lib/extra').write_bytes(b'extra')
        self.assertRaisesRegex(ValueError, 'drift', m.verify, self.root)
        (self.root / 'usr/lib/extra').unlink()
        (self.root / 'usr/lib/libminiupnpc.so.21').write_bytes(b'changed')
        self.assertRaisesRegex(ValueError, 'drift', m.verify, self.root)

    def test_symlink_escape_rejected(self):
        link = self.root / 'usr/lib/libminiupnpc.so'
        link.unlink()
        link.symlink_to('/etc/hosts')
        self.assertRaisesRegex(ValueError, 'escape', m.verify, self.root)

if __name__ == '__main__': unittest.main()
