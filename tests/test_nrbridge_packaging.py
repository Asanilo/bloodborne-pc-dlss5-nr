#!/usr/bin/env python3
"""Static/CPU checks for the private NRBridge AppImage packaging.

No GPU, Wine, account, game asset or private configuration is touched here.
"""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "packaging/nrbridge"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


build = load_module("nrbridge_build", PACK / "build.py")
closure = load_module("nrbridge_closure", PACK / "closure.py")
entry = load_module("nrbridge_entry", ROOT / "launcher/nrbridge_entry.py")


class PortableBuildDefaultsTest(unittest.TestCase):
    def test_relocated_checkout_and_selected_developer_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / 'checkout/packaging/nrbridge'
            package.mkdir(parents=True)
            for name in ('build.py', 'closure.py'):
                shutil.copyfile(PACK / name, package / name)
            environment = dict(os.environ, HOME=str(root / 'home'),
                               NRBRIDGE_HOME=str(root / 'private'),
                               BB_ONLINE_DEPS_ROOT=str(root / 'online'))
            environment.pop('NR_DEV_ROOT', None)
            code = ("import build,json; print(json.dumps([str(build.DEFAULT_NATIVE), "
                    "str(build.DEFAULT_SYSROOT_LIB),str(build.DEFAULT_ONLINE)]))")
            result = subprocess.run([sys.executable, '-c', code], cwd=package,
                                    env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            native, sysroot, online = json.loads(result.stdout)
            self.assertTrue(Path(native).is_relative_to(root / 'private/build'))
            self.assertEqual(Path(sysroot), root / 'checkout/out/deps/root/usr/lib')
            self.assertEqual(Path(online), root / 'online/root/usr/lib')
            environment['NR_DEV_ROOT'] = str(root / 'explicit developer')
            overridden = subprocess.run([sys.executable, '-c', code], cwd=package,
                                        env=environment, capture_output=True, text=True)
            self.assertEqual(overridden.returncode, 0, overridden.stderr)
            self.assertTrue(Path(json.loads(overridden.stdout)[0]).is_relative_to(root / 'explicit developer/build'))
            environment.pop('NR_DEV_ROOT')
            environment.pop('NRBRIDGE_HOME')
            fallback = subprocess.run([sys.executable, '-c', code], cwd=package,
                                      env=environment, capture_output=True, text=True)
            self.assertEqual(fallback.returncode, 0, fallback.stderr)
            self.assertTrue(Path(json.loads(fallback.stdout)[0]).is_relative_to(root / 'home/Code/nr-home/build'))
            environment.pop('BB_ONLINE_DEPS_ROOT')
            missing = subprocess.run([sys.executable, str(package / 'build.py')],
                                     env=environment, capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)
            self.assertIn('--online-deps or BB_ONLINE_DEPS_ROOT', missing.stderr)

    def test_static_verifier_rejects_relocated_development_runpath(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'AppDir'
            app.mkdir()
            (app / 'program').write_bytes(b'\x7fELFfixture')
            dynamic = '(RUNPATH) Library runpath: [/opt/relocated-native/gpu]'
            with mock.patch.object(build, 'run', return_value=dynamic):
                with self.assertRaisesRegex(SystemExit, 'development RUNPATH remains'):
                    build.static_verify(app, (Path('/opt/relocated-native'),))
            with mock.patch.object(build, 'run', return_value='(RUNPATH) [/home/example/build]'):
                with self.assertRaisesRegex(SystemExit, 'development RUNPATH remains'):
                    build.static_verify(app)


class ArtifactManifestTest(unittest.TestCase):
    def test_absolute_gpu_path_is_preserved_in_tree_form(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "artifacts.sha256"
            manifest.write_text(
                f"{'a' * 64}  /tmp/build/bb-probe\n"
                f"{'b' * 64}  /tmp/build/gpu/libbbgpu.so\n"
                f"{'c' * 64}  /tmp/build/libbbport_dlss.so\n"
            )
            parsed = build.parse_artifacts(manifest)
            self.assertEqual(
                set(parsed),
                {"bb-probe", "gpu/libbbgpu.so", "libbbport_dlss.so"},
            )

    def test_invalid_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "artifacts.sha256"
            manifest.write_text("not a digest  file\n")
            with self.assertRaises(SystemExit):
                build.parse_artifacts(manifest)


class ClosurePolicyTest(unittest.TestCase):
    def test_glibc_and_driver_libraries_stay_on_host(self):
        for name in ("libc.so.6", "libm.so.6", "ld-linux-x86-64.so.2",
                     "libnvidia-glcore.so.615.78.08", "libcuda.so.1",
                     "libvulkan.so.1", "libEGL.so.1", "libGLX.so.0"):
            self.assertTrue(closure._is_denied(name), name)

    def test_ordinary_payload_libraries_are_bundled(self):
        for name in ("libgtk-4.so.1", "libadwaita-1.so.0",
                     "libprotobuf.so.36.1.0", "libavformat.so.63",
                     "libminiupnpc.so.21", "libstdc++.so.6"):
            self.assertFalse(closure._is_denied(name), name)

    def test_project_owned_libraries_are_never_generic_payload(self):
        for name in ("libbbgpu.so", "libbbnet.so", "libbbnet20.so",
                     "libbbport_dlss.so", "libnvidia-ngx-dlss.so.310.9.1"):
            self.assertTrue(closure._is_project_owned(name), name)
            self.assertTrue(entry._is_project_library(name), name)
        for name in ("libgtk-4.so.1", "libprotobuf.so.36.1.0",
                     "libnvidia-glcore.so.615.78.08"):
            self.assertFalse(closure._is_project_owned(name), name)


@unittest.skipUnless(shutil.which("gcc"), "gcc is required for the real ELF test")
class RealElfClosureTest(unittest.TestCase):
    def _gcc(self, *arguments):
        result = subprocess.run(
            ["gcc", *map(str, arguments)], capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_project_libraries_are_scanned_but_not_copied(self):
        # Real shared objects and the host ldd: no mocked dependency output.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            gpu = source / "gpu"
            gpu.mkdir(parents=True)
            (source / "dep.c").write_text("int dep(void) { return 7; }\n")
            (gpu / "gpu.c").write_text(
                "extern int dep(void); int gpu(void) { return dep(); }\n"
            )
            (gpu / "net.c").write_text(
                "extern int gpu(void); int net(void) { return gpu(); }\n"
            )
            (source / "main.c").write_text(
                "extern int net(void); extern int dep(void);\n"
                "int main(void) { return net() + dep() > 0 ? 0 : 1; }\n"
            )
            self._gcc("-shared", "-fPIC", "-Wl,-soname,libdep.so",
                      "-o", source / "libdep.so", source / "dep.c")
            self._gcc("-shared", "-fPIC", "-Wl,-soname,libbbgpu.so",
                      "-o", gpu / "libbbgpu.so", gpu / "gpu.c",
                      "-L", source, "-Wl,--no-as-needed", "-ldep")
            self._gcc("-shared", "-fPIC", "-Wl,-soname,libbbnet20.so",
                      "-o", gpu / "libbbnet20.so", gpu / "net.c",
                      "-L", gpu, "-Wl,--no-as-needed", "-lbbgpu",
                      "-L", source, "-ldep")
            self._gcc("-o", source / "main", source / "main.c",
                      "-L", gpu, "-L", source,
                      "-Wl,-rpath-link," + str(gpu),
                      "-Wl,-rpath-link," + str(source),
                      "-Wl,--no-as-needed", "-lbbnet20", "-ldep")
            dest = root / "lib"
            dest.mkdir()
            copied = closure.collect(
                [source / "main"], dest, [gpu, source]
            )
            # The owner libraries are not duplicated...
            self.assertFalse((dest / "libbbgpu.so").exists())
            self.assertFalse((dest / "libbbnet20.so").exists())
            self.assertNotIn("libbbgpu.so", copied)
            self.assertNotIn("libbbnet20.so", copied)
            # ...but their ordinary dependency is still collected because the
            # excluded owner is scanned transitively.
            self.assertTrue((dest / "libdep.so").is_file())
            self.assertIn("libdep.so", copied)


def _write_pe(path: Path) -> None:
    data = bytearray(0x100)
    data[:2] = b"MZ"
    data[0x3C:0x40] = (0x80).to_bytes(4, "little")
    data[0x80:0x84] = b"PE\0\0"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _make_fresh_home_fixture(root: Path) -> dict[str, Path]:
    """A structurally valid package/home fixture for the real setup path."""
    app = root / "app"
    bundle = app / "nrbridge"
    provenance = app / "build-provenance"
    gpu = app / "bin/gpu"
    gpu.mkdir(parents=True)
    bundle.mkdir(parents=True)
    provenance.mkdir(parents=True)
    staged = {
        "bb-probe": b"probe",
        "gpu/libbbgpu.so": b"gpu",
        "libbbport_dlss.so": b"bridge",
        "libnvidia-ngx-dlss.so.1": b"ngx",
    }
    for name, payload in staged.items():
        path = app / "bin" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    (app / "bin/gpu/libbbnet.so").write_bytes(b"net")
    (app / "bin/gpu/libbbnet20.so").write_bytes(b"net20")
    (provenance / "source-head.txt").write_text("a" * 40 + "\n")
    (provenance / "source-diff.patch").write_text("")
    (provenance / "artifacts.sha256").write_text(
        "\n".join(f"{build.sha256(app / 'bin' / name)}  {name}" for name in staged)
        + "\n"
    )
    helper = bundle / "wine_runtime_model.exe"
    helper.write_bytes(b"MZ" + b"\0" * 64)
    exports = {}
    for name in ("native_runtime_model.h", "pre_sr_wire.h",
                 "pre_sr_spec.h", "effective-spec.json"):
        exports[name] = "d" * 64
    helper_manifest = {
        "schema": "nrbridge-helper-build-v1",
        "status": "BUILD_ONLY_PASS",
        "source_head": "c" * 40,
        "artifact": str(helper),
        "artifact_sha256": build.sha256(helper),
        "protocol": {
            "wire_version": 1,
            "interface_revision": 4,
            "runtime_model_schema": "nr-runtime-model-v1",
            "job_model_schema": "nr-runtime-job-model-v1",
            "exports": exports,
            "export_manifest_sha256": "e" * 64,
        },
    }
    (bundle / "helper-build.json").write_text(json.dumps(helper_manifest))
    forwarder = bundle / "forwarder/nvngx.dll_dlssnr.dll"
    forwarder.parent.mkdir(parents=True)
    forwarder.write_bytes(b"MZ" + b"\1" * 64)
    (app / "nrbridge-bundle.json").write_text(json.dumps({
        "schema": "bbport-nrbridge-bundle-v1",
        "build_status": "BUILD_ONLY",
        "daily_ready": True,
        "native": {"source": "a" * 40, "daily_portable": True},
        "helper": {
            "build_id": "b" * 16,
            "artifact_sha256": build.sha256(helper),
            "protocol": {"wire_version": 1, "interface_revision": 4},
        },
        "forwarder": {
            "build_id": "c" * 16,
            "artifact_sha256": build.sha256(forwarder),
        },
    }))
    tools = app / "tools/nr_dev"
    tools.mkdir(parents=True)
    for source in (ROOT / "tools/nr_dev").glob("*.py"):
        shutil.copy2(source, tools / source.name)

    game = root / "game"
    game.mkdir()
    (game / "eboot.bin").write_bytes(b"eboot")
    ge = root / "ge"
    wine = ge / "lib/wine/x86_64-unix"
    wine.mkdir(parents=True)
    (wine / "wine").write_bytes(b"wine")
    (wine / "wine-preloader").write_bytes(b"preloader")
    ngx = root / "ngx"
    ngx.mkdir()
    (ngx / "nvngx.dll").write_bytes(b"nvngx")
    (ngx / "_nvngx.dll").write_bytes(b"_nvngx")
    model = root / "model.dll"
    _write_pe(model)
    system_ngx = root / "libnvidia-ngx.so.999"
    system_ngx.write_bytes(b"system-ngx")
    sysroot = root / "sysroot"
    sysroot.mkdir()
    (sysroot / "libfmt.so.12").write_bytes(b"host-fmt")
    return {
        "app": app, "bundle": bundle, "game": game, "ge": ge,
        "ngx": ngx, "model": model, "system_ngx": system_ngx,
        "sysroot": sysroot,
    }


class FreshHomeTest(unittest.TestCase):
    def test_runs_rejects_non_directory_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory) / "home"
            home.mkdir()
            (home / "runs").write_text("not a directory")
            with self.assertRaises(SystemExit):
                entry.ensure_runs(home)
            (home / "runs").unlink()
            target = Path(directory) / "target"
            target.mkdir()
            (home / "runs").symlink_to(target)
            with self.assertRaises(SystemExit):
                entry.ensure_runs(home)

    def test_fresh_home_setup_creates_runs_for_the_real_runner_mkdir(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = _make_fresh_home_fixture(root)
            home = root / "data/nrbridge-home"
            old = (entry.APP, entry.BUNDLE, entry.TOOLS,
                   entry.BUNDLE_MANIFEST, entry._BUNDLE_CACHE)
            old_home = os.environ.get("NRBRIDGE_HOME")
            entry.APP = fixture["app"]
            entry.BUNDLE = fixture["bundle"]
            entry.TOOLS = fixture["app"] / "tools/nr_dev"
            entry.BUNDLE_MANIFEST = fixture["app"] / "nrbridge-bundle.json"
            entry._BUNDLE_CACHE = None
            try:
                args = type("Args", (), {})()
                args.home = home
                args.force = False
                args.game_dir = str(fixture["game"])
                args.ge_files = str(fixture["ge"])
                args.model = str(fixture["model"])
                args.ngx_dir = str(fixture["ngx"])
                args.system_ngx = str(fixture["system_ngx"])
                args.sysroot_lib = str(fixture["sysroot"])
                with mock.patch.object(entry, "DEPENDENCY_LOCK", root / "fixture-gpu.lock"), \
                     mock.patch.object(entry, "dependency_guard", return_value=mock.Mock()):
                    entry.setup(args)
                self.assertTrue((home / "runs").is_dir())
                self.assertFalse((home / "runs").is_symlink())
                self.assertEqual(
                    entry.project_libraries(home / "lib"), []
                )
                # Reproduce the exact runner operation that used to fail on a
                # fresh home because home/runs did not exist.
                environment = dict(os.environ)
                environment["NRBRIDGE_HOME"] = str(home)
                result = subprocess.run(
                    [sys.executable, "-c",
                     "import runner; run = runner.checked_run('fresh-cpu'); "
                     "run.mkdir(mode=0o700); print(run)"],
                    cwd=str(ROOT / "tools/nr_dev"), env=environment,
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue((home / "runs/fresh-cpu").is_dir())
            finally:
                (entry.APP, entry.BUNDLE, entry.TOOLS,
                 entry.BUNDLE_MANIFEST, entry._BUNDLE_CACHE) = old
                if old_home is None:
                    os.environ.pop("NRBRIDGE_HOME", None)
                else:
                    os.environ["NRBRIDGE_HOME"] = old_home


class LibraryMigrationTest(unittest.TestCase):
    def test_stage_libs_refuses_project_source_and_stale_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            home = root / "home"
            home.mkdir()
            clean = root / "clean"
            clean.mkdir()
            (clean / "libfmt.so.12").write_bytes(b"fmt")
            entry.stage_libs(home, clean, replace=False)
            self.assertTrue((home / "lib/libfmt.so.12").is_file())
            (home / "lib/libbbgpu.so").write_bytes(b"stale")
            with self.assertRaises(SystemExit):
                entry.stage_libs(home, clean, replace=False)
            leaked = root / "leaked"
            leaked.mkdir()
            (leaked / "libbbgpu.so").write_bytes(b"project")
            with self.assertRaises(SystemExit):
                entry.stage_libs(home, leaked, replace=False)



class EntryValidationTest(unittest.TestCase):
    def test_relative_and_empty_paths_are_rejected(self):
        for value in ("", "relative/path", "./x", "a:b"):
            with self.assertRaises(SystemExit):
                entry.canonical(value)

    def test_symlink_is_not_canonical(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "target"
            target.write_text("x")
            link = Path(directory) / "link"
            link.symlink_to(target)
            with self.assertRaises(SystemExit):
                entry.canonical(str(link))

    def test_pe_header_check(self):
        with tempfile.TemporaryDirectory() as directory:
            good = Path(directory) / "good.dll"
            data = bytearray(0x100)
            data[:2] = b"MZ"
            data[0x3C:0x40] = (0x80).to_bytes(4, "little")
            data[0x80:0x84] = b"PE\0\0"
            good.write_bytes(data)
            self.assertTrue(entry.pe_dll(good))
            bad = Path(directory) / "bad.dll"
            bad.write_bytes(b"MZ" + b"\0" * 200)
            self.assertFalse(entry.pe_dll(bad))


class DynamicIdentityTest(unittest.TestCase):
    def test_native_identity_comes_from_manifest_not_a_constant(self):
        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory) / "baselines" / ("a" * 40)
            (build_dir / "gpu").mkdir(parents=True)
            (build_dir / "bb-probe").write_text("probe")
            (build_dir / "gpu/libbbgpu.so").write_text("gpu")
            (build_dir / "gpu/libbbnet.so").write_text("net")
            (build_dir / "gpu/libbbnet20.so").write_text("net20")
            (build_dir / "libbbport_dlss.so").write_text("bridge")
            (build_dir / "libnvidia-ngx-dlss.so.1").write_text("ngx")
            (build_dir / "source-head.txt").write_text("a" * 40)
            (build_dir / "source-diff.patch").write_text("")
            lines = []
            for name in ("bb-probe", "gpu/libbbgpu.so", "libbbport_dlss.so",
                         "libnvidia-ngx-dlss.so.1"):
                lines.append(f"{build.sha256(build_dir / name)}  {name}")
            (build_dir / "artifacts.sha256").write_text("\n".join(lines) + "\n")
            record = build.verify_native(build_dir)
            self.assertEqual(record["source"], "a" * 40)
            self.assertEqual(set(record["artifacts"]), {
                "bb-probe", "gpu/libbbgpu.so", "libbbport_dlss.so",
                "libnvidia-ngx-dlss.so.1"})
            self.assertFalse(record["daily_portable"])
            (build_dir / "gpu/libbbgpu.so").write_text("NRBRIDGE_NATIVE_DAILY_RING64_V2")
            (build_dir / "artifacts.sha256").write_text(
                "\n".join(
                    f"{build.sha256(build_dir / name)}  {name}"
                    for name in ("bb-probe", "gpu/libbbgpu.so",
                                 "libbbport_dlss.so", "libnvidia-ngx-dlss.so.1")
                ) + "\n"
            )
            self.assertTrue(build.verify_native(build_dir)["daily_portable"])

    def test_helper_accepts_wire1_revision4(self):
        with tempfile.TemporaryDirectory() as directory:
            dest = Path(directory) / "helper-runtime-model" / ("b" * 16)
            dest.mkdir(parents=True)
            exe = dest / "wine_runtime_model.exe"
            exe.write_bytes(b"MZ" + b"\0" * 100)
            protocol_dir = dest / "protocol"
            protocol_dir.mkdir()
            exports = {}
            for name in ("native_runtime_model.h", "pre_sr_wire.h",
                         "pre_sr_spec.h", "effective-spec.json"):
                path = protocol_dir / name
                path.write_text(name)
                exports[name] = build.sha256(path)
            manifest = {
                "schema": "nrbridge-helper-build-v1",
                "status": "BUILD_ONLY_PASS",
                "source_head": "c" * 40,
                "artifact": str(exe),
                "artifact_sha256": build.sha256(exe),
                "protocol": {"wire_version": 1, "interface_revision": 4,
                             "exports": exports,
                             "export_manifest_sha256": "d" * 64},
            }
            (dest / "build.json").write_text(__import__("json").dumps(manifest))
            record = build.verify_helper(exe)
            self.assertEqual(record["protocol"]["interface_revision"], 4)
            self.assertEqual(record["protocol"]["wire_version"], 1)

    def test_helper_rejects_unsupported_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            dest = Path(directory) / "helper-runtime-model" / ("b" * 16)
            dest.mkdir(parents=True)
            exe = dest / "wine_runtime_model.exe"
            exe.write_bytes(b"MZ" + b"\0" * 100)
            protocol_dir = dest / "protocol"
            protocol_dir.mkdir()
            exports = {}
            for name in ("native_runtime_model.h", "pre_sr_wire.h",
                         "pre_sr_spec.h", "effective-spec.json"):
                path = protocol_dir / name
                path.write_text(name)
                exports[name] = build.sha256(path)
            for wire, revision in ((2, 4), (1, 5), (1, 2)):
                manifest = {
                    "schema": "nrbridge-helper-build-v1",
                    "status": "BUILD_ONLY_PASS",
                    "source_head": "c" * 40,
                    "artifact": str(exe),
                    "artifact_sha256": build.sha256(exe),
                    "protocol": {"wire_version": wire, "interface_revision": revision,
                                 "exports": exports,
                                 "export_manifest_sha256": "d" * 64},
                }
                (dest / "build.json").write_text(__import__("json").dumps(manifest))
                with self.assertRaises(SystemExit):
                    build.verify_helper(exe)

    def test_protocol_matching_requires_exact_supported_fields(self):
        native = {"protocol": {"wire_version": 1, "interface_revision": 4,
                                "export_manifest_sha256": "a" * 64}}
        helper = {"protocol": {"wire_version": 1, "interface_revision": 4,
                                "export_manifest_sha256": "a" * 64}}
        build.protocol_matches(native, helper)
        helper["protocol"]["interface_revision"] = 3
        with self.assertRaises(SystemExit):
            build.protocol_matches(native, helper)
        helper["protocol"] = {"wire_version": 1, "interface_revision": 4}
        with self.assertRaises(SystemExit):
            build.protocol_matches(native, helper)

    def test_bundle_info_uses_packaged_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nrbridge-bundle.json"
            path.write_text(__import__("json").dumps({
                "schema": "bbport-nrbridge-bundle-v1",
                "native": {"source": "e" * 40},
                "helper": {"build_id": "f" * 16},
                "forwarder": {"build_id": "1" * 16},
            }))
            old = entry.BUNDLE_MANIFEST, entry._BUNDLE_CACHE
            entry.BUNDLE_MANIFEST = path
            entry._BUNDLE_CACHE = None
            try:
                self.assertEqual(entry.bundle_info()["native"]["source"], "e" * 40)
            finally:
                entry.BUNDLE_MANIFEST, entry._BUNDLE_CACHE = old


class ContinuationStateTest(unittest.TestCase):
    def _home(self, directory, status):
        home = Path(directory)
        run = home / "runs/previous"
        run.mkdir(parents=True)
        (run / "result.json").write_text(__import__("json").dumps({
            "status": status,
            "returncode": 0,
            "residuals": [],
        }))
        (home / "state.json").write_text(__import__("json").dumps({
            "last_session": "previous",
            "last_status": status,
            "last_good_session": "previous" if status == "PASS_NORMAL_TEARDOWN" else None,
        }))
        return home

    def test_normal_retirement_is_continued_automatically(self):
        with tempfile.TemporaryDirectory() as directory:
            home = self._home(directory, "PASS_NORMAL_TEARDOWN")
            import argparse
            args = argparse.Namespace(continue_session=None, new_game=False,
                                      no_terminal=True)
            self.assertEqual(entry.choose_continuation(home, args), "previous")

    def test_hold_requires_a_choice_and_keeps_the_save(self):
        with tempfile.TemporaryDirectory() as directory:
            home = self._home(directory, "HOLD")
            import argparse
            args = argparse.Namespace(continue_session=None, new_game=False,
                                      no_terminal=True)
            with self.assertRaises(SystemExit) as raised:
                entry.choose_continuation(home, args)
            self.assertEqual(raised.exception.code, 3)
            self.assertTrue((home / "runs/previous/result.json").is_file())

    def test_online_pool_state_maps_to_explicit_runner_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "settings.json"
            config.write_text('{"online": true, "online_pool": "huntersdream", '
                              '"thd_online_id": "id", "thd_token": "secret"}')
            import argparse
            args = argparse.Namespace(offline=False, online_pool="thd",
                                      online_account_config=str(config), bbsc=False)
            pool, account, bbsc = entry.online_choice(args)
            self.assertEqual(pool, "thd")
            self.assertEqual(account, config.resolve())
            self.assertFalse(bbsc)
            self.assertTrue(entry.runner_supports_online())

    def test_online_is_offline_by_default(self):
        import argparse
        args = argparse.Namespace(offline=False, online_pool=None,
                                  online_account_config=None, bbsc=False)
        self.assertEqual(entry.online_choice(args), ("offline", None, False))

    def test_bbsc_is_rejected_outside_shadnet(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "settings.json"
            config.write_text("{}")
            import argparse
            args = argparse.Namespace(offline=False, online_pool="thd",
                                      online_account_config=str(config), bbsc=True)
            with self.assertRaises(SystemExit):
                entry.online_choice(args)

    def test_daily_forwards_explicit_online_flags(self):
        import argparse, contextlib, io
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            (home / "localinputs").mkdir()
            (home / "localinputs/runtime.json").write_text("{}")
            account = home / "settings.json"
            account.write_text("{}")
            old_bundle = entry._BUNDLE_CACHE
            entry._BUNDLE_CACHE = {
                "schema": "bbport-nrbridge-bundle-v1",
                "daily_ready": True,
                "native": {"source": "a" * 40, "daily_portable": True},
                "helper": {"build_id": "b" * 16,
                           "protocol": {"interface_revision": 4, "wire_version": 1}},
                "forwarder": {"build_id": "c" * 16},
            }
            args = argparse.Namespace(
                home=home, session="online-test", continue_session=None,
                new_game=True, online_pool="shadnet", offline=False,
                online_account_config=str(account), bbsc=True,
                joystick_mouse=True, no_terminal=True, print_command=True,
            )
            output = io.StringIO()
            try:
                with contextlib.redirect_stdout(output):
                    code = entry.daily(args)
            finally:
                entry._BUNDLE_CACHE = old_bundle
            self.assertEqual(code, 0)
            line = output.getvalue()
            self.assertIn("--online-pool shadnet", line)
            self.assertIn("--online-account-config", line)
            self.assertIn("--bbsc", line)
            self.assertIn("--joystick-mouse", line)

    def test_new_game_skips_continuation(self):
        with tempfile.TemporaryDirectory() as directory:
            home = self._home(directory, "PASS_NORMAL_TEARDOWN")
            import argparse
            args = argparse.Namespace(continue_session=None, new_game=True,
                                      no_terminal=True)
            self.assertIsNone(entry.choose_continuation(home, args))


class PinGenerationTest(unittest.TestCase):
    def test_pins_cover_required_and_tree_files(self):
        spec = importlib.util.spec_from_file_location(
            "nr_runtime_inputs", ROOT / "tools/nr_dev/runtime_inputs.py"
        )
        runtime_inputs = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runtime_inputs)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tree = root / "tree"
            tree.mkdir()
            (tree / "a").write_text("a")
            required = root / "required"
            required.write_text("r")
            pins = entry.generate_pins(
                runtime_inputs, {}, [required], [tree]
            )
            names = {item["path"] for item in pins}
            self.assertIn(str(tree / "a"), names)
            self.assertIn(str(required), names)
            self.assertTrue(all(len(item["sha256"]) == 64 for item in pins))


class AppRunInstalledLayoutTest(unittest.TestCase):
    def run_apprun(self, markers=("ini", "settings"), overrides=None, image_directory="packages", via_symlink=True):
        with tempfile.TemporaryDirectory(prefix="nrbridge AppRun ") as directory:
            root = Path(directory) / "installed root"
            package = root / image_directory / "preview.AppImage"
            package.parent.mkdir(parents=True)
            package.write_text("fixture image")
            current = root / "runtime/current"
            current.parent.mkdir()
            current.symlink_to(package)
            if "ini" in markers:
                (root / "bbport.ini").write_text("upscaler=dlss\n")
            if "settings" in markers:
                config = root / "config/bbport-launcher/settings.json"
                config.parent.mkdir(parents=True)
                config.write_text('{}')
            app = Path(directory) / "extracted AppDir"
            interpreter = app / "usr/bin/python3.14"
            interpreter.parent.mkdir(parents=True)
            # Shell stand-in records the real generated AppRun environment without
            # loading Python/GTK, the coordinator, account state or any GPU code.
            interpreter.write_text(
                '#!/bin/sh\n'
                'printf "%s\\0" "${BB_DATA_DIR-unset}" "${BB_CONFIG-unset}" '
                '"${XDG_CONFIG_HOME-unset}" "${NRBRIDGE_HOME-unset}"\n')
            interpreter.chmod(0o755)
            build.write_apprun(app)
            keys = ("BB_DATA_DIR", "BB_CONFIG", "XDG_CONFIG_HOME", "NRBRIDGE_HOME")
            env = {key: value for key, value in os.environ.items() if key not in keys}
            env.update(APPIMAGE=str(current if via_symlink else package), HOME=str(Path(directory) / "user-home"),
                       XDG_DATA_HOME=str(Path(directory) / "fresh-data"),
                       XDG_CACHE_HOME=str(Path(directory) / "cache"))
            env.update(overrides or {})
            completed = subprocess.run([str(app / "AppRun"), "--check"], env=env,
                capture_output=True, check=True, timeout=10)
            values = completed.stdout.decode().split('\0')
            self.assertEqual(values[-1], '')
            self.assertFalse((root / 'data').exists(), 'discovery must not create or migrate player data')
            return root, dict(zip(keys, values[:-1])), env

    def test_direct_installed_image_and_runtime_symlink_keep_original_paths(self):
        for via_symlink in (False, True):
            with self.subTest(via_symlink=via_symlink):
                root, actual, _env = self.run_apprun(via_symlink=via_symlink)
                self.assertEqual(actual, {
                    "BB_DATA_DIR": str(root / "data"), "BB_CONFIG": str(root / "bbport.ini"),
                    "XDG_CONFIG_HOME": str(root / "config"), "NRBRIDGE_HOME": str(root / "data/nrbridge"),
                })

    def test_explicit_environment_values_win_independently(self):
        explicit = {"BB_DATA_DIR": "/chosen/data", "BB_CONFIG": "/chosen/bbport.ini",
                    "XDG_CONFIG_HOME": "/chosen/config", "NRBRIDGE_HOME": "/chosen/nr-home"}
        _root, actual, _env = self.run_apprun(overrides=explicit)
        self.assertEqual(actual, explicit)
        empty = dict.fromkeys(explicit, "")
        _root, actual, _env = self.run_apprun(overrides=empty)
        self.assertEqual(actual, empty)
        root, actual, _env = self.run_apprun(overrides={"BB_CONFIG": "/chosen/bbport.ini"})
        self.assertEqual(actual["BB_CONFIG"], "/chosen/bbport.ini")
        self.assertEqual(actual["BB_DATA_DIR"], str(root / "data"))
        self.assertEqual(actual["NRBRIDGE_HOME"], str(root / "data/nrbridge"))

    def test_missing_markers_or_noninstalled_location_use_standalone_defaults(self):
        for markers, image_directory in (((), "packages"), (("ini",), "packages"),
                (("settings",), "packages"), (("ini", "settings"), "downloads")):
            with self.subTest(markers=markers, image_directory=image_directory):
                _root, actual, env = self.run_apprun(markers=markers, image_directory=image_directory)
                self.assertEqual(actual["BB_DATA_DIR"], "unset")
                self.assertEqual(actual["BB_CONFIG"], "unset")
                self.assertEqual(actual["XDG_CONFIG_HOME"], "unset")
                self.assertEqual(actual["NRBRIDGE_HOME"], env["XDG_DATA_HOME"] + "/bbport/nrbridge-home")


class PackagingSourceTest(unittest.TestCase):
    def test_model_and_private_data_are_not_referenced_as_payload(self):
        source = (PACK / "build.py").read_text()
        self.assertIn("model_bundled", source)
        self.assertIn("credentials_bundled", source)
        # The builder must not copy the game or private seed into AppDir.
        self.assertNotIn("CUSA03173", source)

    def test_launcher_has_no_hardcoded_artifact_identity(self):
        entry_source = (ROOT / "launcher/nrbridge_entry.py").read_text()
        for literal in ("6753999346d4314430eb7ae95ae7c7cc81884fcc",
                        "7f69c1e556810b2b",
                        "99bfd217d96013301f26ab8e68b7c4d7eeabf8cc"):
            self.assertNotIn(literal, entry_source)

    def test_defaults_are_build_host_only_and_runner_is_reused(self):
        entry_source = (ROOT / "launcher/nrbridge_entry.py").read_text()
        self.assertIn('"--daily"', entry_source)
        self.assertIn("tools/nr_dev", entry_source)
        self.assertNotIn("--context-recovery", entry_source)
        self.assertIn("--continue-session", entry_source)


if __name__ == "__main__":
    unittest.main()
