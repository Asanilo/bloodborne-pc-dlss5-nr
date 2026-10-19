"""CPU fixtures execute the real wrapper, without compiling or invoking a GPU."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

WRAPPER = Path(__file__).parents[1] / 'build.sh'
SPECS = {'fsr-vulkan': 'gpu/third_party/fsr-vulkan',
         'imgui': 'gpu/third_party/imgui', 'LibAtrac9': 'third_party/LibAtrac9'}


class BuildInputs(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.wt = self.root / 'worktree'
        self.deps = self.root / 'inputs/source-deps'
        self.envroot = self.root / 'sdk environment'
        self.output = self.root / 'isolated output'
        wrapper = self.wt / 'tools/nr_dev/build.sh'
        wrapper.parent.mkdir(parents=True)
        shutil.copyfile(WRAPPER, wrapper)
        for name, rel in SPECS.items():
            (self.wt / rel).mkdir(parents=True)
            (self.deps / name).mkdir(parents=True)
        (self.deps / 'fsr-vulkan/CMakeLists.txt').write_text('patched\n')
        (self.deps / 'imgui/imgui.h').write_text('locked\n')
        (self.deps / 'LibAtrac9/C/src').mkdir(parents=True)
        (self.deps / 'LibAtrac9/C/src/decoder.c').write_text('locked\n')
        patches = self.wt / 'gpu/patches/fsr-vulkan'
        patches.mkdir(parents=True)
        (patches / 'test.patch').write_text('diff --git a/CMakeLists.txt b/CMakeLists.txt\n--- a/CMakeLists.txt\n+++ b/CMakeLists.txt\n@@ -1 +1 @@\n-original\n+patched\n')
        for name in SPECS:
            self.lock(name)
        for rel in ('root/usr/include', 'dlss-sdk'):
            (self.envroot / rel).mkdir(parents=True)
        (self.envroot / 'bbport-env.sh').write_text('''export DLSS_SDK_ROOT="$(dirname "${BASH_SOURCE[0]}")/dlss-sdk"
export VULKAN_INCLUDE="$(dirname "${BASH_SOURCE[0]}")/root/usr/include"
export CMAKE_PREFIX_PATH="$(dirname "${BASH_SOURCE[0]}")/root/usr"
export CPATH="$VULKAN_INCLUDE"
''')
        # Fake only the lower-level compiler recipe; the wrapper and validation
        # execute unchanged, with a real temporary Git repository.
        (self.wt / 'build.sh').write_text('''set -eu
printf compiled > "$BB_BUILD_DIR/compile-started"
mkdir -p "$BB_BUILD_DIR/gpu"
for p in bb-probe gpu/libbbgpu.so libbbport_dlss.so libnvidia-ngx-dlss.so.310.9.1; do
 printf fixture > "$BB_BUILD_DIR/$p"
done
''')
        self.git('init', '-q')
        self.git('add', '.')
        self.git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture')
        self.environment = dict(os.environ, NR_DEV_ROOT=str(self.root),
                                NR_BUILD_ENV_ROOT=str(self.envroot), BB_BUILD_DIR=str(self.output))
        self.environment.pop('NR_SOURCE_DEPS_ROOT', None)

    def git(self, *args):
        subprocess.run(['git', '-C', str(self.wt), *args], check=True, capture_output=True)

    def lock(self, name):
        directory = self.deps / name
        (self.deps / (name + '.content.sha256')).write_text(''.join(
            hashlib.sha256(p.read_bytes()).hexdigest() + '  ./' + str(p.relative_to(directory)) + '\n'
            for p in sorted(directory.rglob('*')) if p.is_file()))

    def run_build(self, success=True):
        before = {str(p): p.read_bytes() for p in self.deps.rglob('*') if p.is_file()}
        result = subprocess.run(['bash', str(self.wt / 'tools/nr_dev/build.sh')],
                                env=self.environment, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertEqual(before, {str(p): p.read_bytes() for p in self.deps.rglob('*') if p.is_file()})
        if not success:
            self.assertFalse(self.output.exists(), result.stdout + result.stderr)
            self.assertTrue(all(not (self.wt / rel).is_symlink() for rel in SPECS.values()))
        return result

    def test_default_dependency_root_and_isolated_output(self):
        self.run_build()
        record = json.loads((self.output / 'build-inputs.json').read_text())
        self.assertEqual(record['source_deps_root'], str(self.deps))
        self.assertEqual(record['environment']['DLSS_SDK_ROOT'], str(self.envroot / 'dlss-sdk'))
        self.assertEqual(record['environment']['CPATH'], str(self.envroot / 'root/usr/include'))
        self.assertFalse((self.root / 'build/native').exists())
        self.assertTrue((self.output / 'compile-started').is_file())
        for name, rel in SPECS.items():
            self.assertEqual((self.wt / rel).resolve(), self.deps / name)
        self.run_build()  # Matching preexisting links remain usable.

    def test_explicit_independent_dependency_root(self):
        other = self.root / 'separate sources'
        self.deps.rename(other)
        self.deps = other
        self.environment['NR_SOURCE_DEPS_ROOT'] = str(other)
        self.run_build()
        self.assertEqual(json.loads((self.output / 'build-inputs.json').read_text())['source_deps_root'], str(other))

    def test_missing_dependency(self):
        shutil.rmtree(self.deps / 'imgui')
        self.run_build(False)

    def test_missing_manifest(self):
        (self.deps / 'imgui.content.sha256').unlink()
        self.run_build(False)

    def test_changed_dependency(self):
        (self.deps / 'imgui/imgui.h').write_text('drift\n')
        self.run_build(False)

    def test_unlisted_dependency_file(self):
        (self.deps / 'imgui/extra').write_text('extra')
        self.run_build(False)

    def test_unsafe_manifest(self):
        (self.deps / 'imgui.content.sha256').write_text('0' * 64 + '  ./../outside\n')
        self.run_build(False)

    def test_dependency_symlink(self):
        (self.deps / 'imgui/alias').symlink_to('imgui.h')
        self.run_build(False)

    def test_populated_checkout_preserved(self):
        checkout = self.wt / SPECS['imgui'] / 'imgui.h'
        checkout.write_text('existing checkout')
        self.run_build(False)
        self.assertEqual(checkout.read_text(), 'existing checkout')

    def test_unapplied_patch_rejected_without_repair(self):
        (self.deps / 'fsr-vulkan/CMakeLists.txt').write_text('original\n')
        self.lock('fsr-vulkan')
        self.run_build(False)

    def test_stale_absolute_environment_exports(self):
        with (self.envroot / 'bbport-env.sh').open('a') as stream:
            stream.write('export DLSS_SDK_ROOT=/old/absolute/sdk\n')
        self.run_build(False)

    def test_missing_environment(self):
        (self.envroot / 'bbport-env.sh').unlink()
        self.run_build(False)

    def test_output_overlaps_input(self):
        self.environment['BB_BUILD_DIR'] = str(self.deps / 'objects')
        self.run_build(False)

    def test_relative_paths_rejected(self):
        self.environment['NR_BUILD_ENV_ROOT'] = 'relative'
        self.run_build(False)


if __name__ == '__main__':
    unittest.main()
