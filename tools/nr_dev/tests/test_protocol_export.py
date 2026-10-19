"""Native-local protocol export checks, including rejection before build writes."""
import importlib.util
import json
from pathlib import Path
import unittest

import test_build_inputs as fixtures

SOURCE = Path(__file__).parents[1] / 'check_protocol_export.py'
spec = importlib.util.spec_from_file_location('check_protocol_export', SOURCE)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ProtocolExport(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.BuildInputs('test_default_dependency_root_and_isolated_output')
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.directory = self.fixture.wt / checker.DIRECTORY
        self.provenance = self.directory / checker.PROVENANCE

    def change(self, edit):
        meta = json.loads(self.provenance.read_text())
        edit(meta)
        self.provenance.write_text(json.dumps(meta, indent=2) + '\n')

    def test_local_copy_and_build_provenance(self):
        proof = checker.checked(self.fixture.wt)
        self.assertEqual(proof['source']['commit'], 'cea5160aab380272dfe52ab2db31691e77531c18')
        self.assertEqual(proof['header_sha256'], '67fa656294fa38c6aca66f011a64211ae9fe79e44aad2f9a93fbe5cfcfcc732b')
        self.fixture.run_build()
        inputs = json.loads((self.fixture.output / 'build-inputs.json').read_text())
        self.assertEqual(inputs['protocol'], proof)
        self.assertIn(str(self.provenance), inputs['inputs'])
        self.assertIn(str(self.directory / checker.HEADER), inputs['inputs'])

    def test_header_mismatch_before_build_writes(self):
        with (self.directory / checker.HEADER).open('ab') as stream:
            stream.write(b'\n// drift\n')
        result = self.fixture.run_build(False)
        self.assertIn('header SHA mismatch', result.stderr)

    def test_missing_provenance_before_build_writes(self):
        self.provenance.unlink()
        result = self.fixture.run_build(False)
        self.assertIn('missing regular local protocol input', result.stderr)

    def test_missing_header_before_build_writes(self):
        (self.directory / checker.HEADER).unlink()
        self.fixture.run_build(False)

    def test_manifest_hash_mismatch_before_build_writes(self):
        self.change(lambda meta: meta.update(export_manifest_sha256='0' * 64))
        result = self.fixture.run_build(False)
        self.assertIn('export manifest SHA mismatch', result.stderr)

    def test_wrong_schema_and_typed_version_refused(self):
        original = self.provenance.read_bytes()
        for edit in (lambda m: m.update(schema='unrecognized'),
                     lambda m: m['export_manifest'].update(wire_version=True),
                     lambda m: m['export_manifest'].update(interface_revision=4),
                     lambda m: m['source'].update(commit='short')):
            with self.subTest(edit=edit):
                self.provenance.write_bytes(original)
                self.change(edit)
                with self.assertRaises(ValueError):
                    checker.checked(self.fixture.wt)

    def test_duplicate_or_symlink_provenance_refused(self):
        original = self.provenance.read_bytes()
        self.provenance.write_text('{"schema":"a","schema":"b"}')
        with self.assertRaisesRegex(ValueError, 'duplicate key'):
            checker.checked(self.fixture.wt)
        self.provenance.unlink()
        target = self.directory / 'other.json'
        target.write_bytes(original)
        self.provenance.symlink_to(target)
        self.fixture.run_build(False)


if __name__ == '__main__':
    unittest.main()
