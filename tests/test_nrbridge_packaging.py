#!/usr/bin/env python3
"""Static/CPU checks for the private NRBridge AppImage packaging.

No GPU, Wine, account, game asset or private configuration is touched here.
"""
import importlib.util
import os
import sys
import tempfile
import unittest
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
        for name in ("libgtk-4.so.1", "libadwaita-1.so.0", "libbbgpu.so",
                     "libprotobuf.so.36.1.0", "libavformat.so.63",
                     "libminiupnpc.so.21", "libstdc++.so.6"):
            self.assertFalse(closure._is_denied(name), name)


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
