#!/usr/bin/env python3
"""Build the private bbport NRBridge AppImage preview.

This intentionally reuses the upstream package's shape and pruning decisions
(``packaging/default.nix``): the Native four-artifact baseline, the launcher,
GTK/Adwaita without GStreamer's media pipeline, Mesa/driver handling delegated
to the host, CJK-capable fonts, and only the libraries the binaries need.

It differs where the private candidate requires it: the Native/Helper/forwarder
builds live in the private data root rather than a Nix store, so the ELF closure
is collected explicitly, absolute development RUNPATHs are removed, and the
Helper runtime configuration is generated on the user's machine from local
Wine/GE and model selections.  The model, game, saves and credentials are never
copied into the image.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from closure import _is_project_owned, collect  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_NATIVE_SHA = "c91bf9467604c36d70d97dd200844ae1ac8e8428"
DEFAULT_HELPER_BUILD = "1eddac56e4de26c0"
DEFAULT_FORWARDER_BUILD = "9a8251430ccfa68a"
PYTHON_VERSION = "3.14"
APPIMAGE_NAME = "bbport-NRBridge-preview-x86_64.AppImage"

# Build inputs stay outside the image; runtime paths are selected on first run.
DEV_ROOT = Path(os.environ.get("NR_DEV_ROOT", os.environ.get(
    "NRBRIDGE_HOME", str(Path.home() / "Code/nrbridge-dev"))))
DEFAULT_NATIVE = DEV_ROOT / "build/baselines" / DEFAULT_NATIVE_SHA
DEFAULT_HELPER = DEV_ROOT / "build/helper-runtime-model" / DEFAULT_HELPER_BUILD / "wine_runtime_model.exe"
DEFAULT_FORWARDER = DEV_ROOT / "build/convergence/forwarder" / DEFAULT_FORWARDER_BUILD / "nvngx.dll_dlssnr.dll"
DEFAULT_SYSROOT_LIB = ROOT / "out/deps/root/usr/lib"
DEFAULT_ONLINE = (Path(os.environ["BB_ONLINE_DEPS_ROOT"]) / "root/usr/lib"
                  if os.environ.get("BB_ONLINE_DEPS_ROOT") else None)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fail(message: str) -> "NoReturn":
    raise SystemExit("nrbridge packaging: " + message)


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> str:
    result = subprocess.run(
        command, cwd=cwd, env=env, capture_output=True, text=True, timeout=600
    )
    if result.returncode != 0:
        fail(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout[-2000:]}{result.stderr[-4000:]}"
        )
    return result.stdout


def copy_tree(src: Path, dst: Path, ignore=()) -> None:
    shutil.copytree(src, dst, symlinks=False, ignore=shutil.ignore_patterns(*ignore))


def stage_file(src: Path, dst: Path, mode: int | None = None) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    dst.chmod(mode if mode is not None else (src.stat().st_mode & 0o777) | 0o600)


def parse_artifacts(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            fail(f"invalid artifact line in {path}: {line!r}")
        digest, name = match.groups()
        rel = Path(name)
        if rel.is_absolute():
            key = f"gpu/{rel.name}" if rel.parent.name == "gpu" else rel.name
        else:
            key = str(rel)
        if key in out:
            fail(f"duplicate artifact {key}")
        out[key] = digest
    return out


def verify_native(build: Path) -> dict:
    """Derive the Native identity from the selected build's own manifest.

    No commit, protocol revision or artifact hash is assumed here: the daily
    candidate changes independently of this packaging code.  A baseline is
    still required to be clean and commit-addressed, and every listed artifact
    must match its SHA in ``artifacts.sha256``.
    """
    if not build.is_dir():
        fail(f"missing Native build: {build}")
    head = (build / "source-head.txt").read_text().strip()
    if not re.fullmatch("[0-9a-f]{40}", head):
        fail(f"Native source-head.txt is not a full commit: {head!r}")
    if build.parent.name == "baselines" and build.name != head:
        fail(f"baseline directory {build.name} does not match source head {head}")
    if (build / "source-diff.patch").stat().st_size:
        fail("Native baseline has a non-empty source diff")
    artifacts = parse_artifacts(build / "artifacts.sha256")
    required = {
        "bb-probe",
        "gpu/libbbgpu.so",
        "libbbport_dlss.so",
    }
    ngx = [name for name in artifacts if name.startswith("libnvidia-ngx-dlss.so.")]
    if len(ngx) != 1:
        fail(f"Native manifest must name exactly one NGX runtime, got {ngx}")
    required.add(ngx[0])
    if set(artifacts) != required:
        fail(f"unexpected Native artifact set: {sorted(artifacts)}")
    for name, digest in artifacts.items():
        path = build / name
        if not path.is_file() or sha256(path) != digest:
            fail(f"Native artifact SHA mismatch: {path}")
    for name in ("gpu/libbbnet.so", "gpu/libbbnet20.so"):
        if not (build / name).is_file():
            fail(f"Native build is missing the online module {name}")
    build_inputs = {}
    if (build / "build-inputs.json").is_file():
        build_inputs = json.loads((build / "build-inputs.json").read_text())
    gpu_bytes = (build / "gpu/libbbgpu.so").read_bytes()
    daily_portable = b"NRBRIDGE_NATIVE_DAILY_RING64_V2" in gpu_bytes
    return {
        "source": head,
        "artifacts": artifacts,
        # The bundle needs identity, not the development root's full input
        # inventory.  Keeping only the protocol avoids publishing old paths.
        "protocol": build_inputs.get("protocol", {}),
        # Older Natives hardcode a development runs directory. Record
        # the fact so the launcher refuses a portable daily launch instead of
        # failing deep inside the renderer.
        "daily_portable": daily_portable,
    }


def verify_helper(exe: Path) -> dict:
    manifest_path = exe.parent / "build.json"
    if not manifest_path.is_file():
        fail(f"missing Helper manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != "nrbridge-helper-build-v1":
        fail("unexpected Helper manifest schema")
    if manifest.get("status") != "BUILD_ONLY_PASS":
        fail(f"Helper status is {manifest.get('status')!r}")
    if manifest.get("artifact") != str(exe):
        fail("Helper manifest artifact path does not match the selected PE")
    digest = sha256(exe)
    if manifest.get("artifact_sha256") != digest:
        fail("Helper PE SHA mismatch against its manifest")
    protocol = manifest.get("protocol", {})
    if type(protocol.get("wire_version")) is not int or protocol["wire_version"] != 1:
        fail("Helper protocol must use the supported wire_version 1")
    if type(protocol.get("interface_revision")) is not int or protocol["interface_revision"] not in (3, 4):
        fail("Helper protocol interface_revision must be 3 or 4")
    exports = protocol.get("exports")
    required_exports = {
        "native_runtime_model.h",
        "pre_sr_wire.h",
        "pre_sr_spec.h",
        "effective-spec.json",
    }
    if (not isinstance(exports, dict) or not required_exports <= set(exports)
            or set(exports) - required_exports - {'nr_dimensions.h'}):
        fail("Helper protocol export set is incomplete")
    export_dir = exe.parent / "protocol"
    for name, export_digest in exports.items():
        if not isinstance(export_digest, str) or not re.fullmatch("[0-9a-f]{64}", export_digest):
            fail(f"Helper export SHA is not exact: {name}")
        export_file = export_dir / name
        if not export_file.is_file() or sha256(export_file) != export_digest:
            fail(f"Helper export file/SHA mismatch: {name}")
    manifest_digest = protocol.get("export_manifest_sha256")
    if not isinstance(manifest_digest, str) or not re.fullmatch("[0-9a-f]{64}", manifest_digest):
        fail("Helper export_manifest_sha256 is missing or malformed")
    source = manifest.get("source_head", "")
    if not re.fullmatch("[0-9a-f]{40}", source):
        fail(f"Helper manifest has no full source head: {source!r}")
    build_id = exe.parent.name
    if not re.fullmatch("[0-9a-f]{16,64}", build_id):
        fail(f"Helper output directory is not content-addressed: {build_id!r}")
    return {
        "build_id": build_id,
        "source_head": source,
        "artifact_sha256": digest,
        "manifest_sha256": sha256(manifest_path),
        "protocol": protocol,
    }


def verify_forwarder(dll: Path) -> dict:
    manifest_path = dll.parent / "build.json"
    if not manifest_path.is_file():
        fail(f"missing forwarder manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != "nrbridge-forwarder-build-v1":
        fail("unexpected forwarder manifest schema")
    if manifest.get("status") != "BUILD_EXPORTS_PASS":
        fail(f"forwarder status is {manifest.get('status')!r}")
    if manifest.get("artifact") != str(dll):
        fail("forwarder manifest artifact path does not match the selected DLL")
    digest = sha256(dll)
    if manifest.get("artifact_sha256") != digest:
        fail("forwarder DLL SHA mismatch against its manifest")
    exports = manifest.get("exports", [])
    if not isinstance(exports, list) or not exports:
        fail("forwarder manifest has no export table")
    build_id = dll.parent.name
    if not re.fullmatch("[0-9a-f]{16,64}", build_id):
        fail(f"forwarder output directory is not content-addressed: {build_id!r}")
    return {
        "build_id": build_id,
        "source_head": manifest.get("source_head", ""),
        "artifact_sha256": digest,
        "manifest_sha256": sha256(manifest_path),
        "exports": len(exports),
    }


def protocol_matches(native: dict, helper: dict) -> None:
    """Native and Helper must speak the same exported protocol.

    The private daily protocol is versioned independently (wire 1/revision 3
    today, wire 2/revision 4 for the 64-slot candidate), so compare what the
    manifests say instead of accepting one fixed revision.
    """
    np = native.get("protocol", {})
    hp = helper.get("protocol", {})
    required = ("wire_version", "interface_revision", "export_manifest_sha256")
    for side, record in (("Native", np), ("Helper", hp)):
        missing = [key for key in required if key not in record]
        if missing:
            fail(f"{side} protocol manifest is missing fields: {missing}")
        if record["wire_version"] != 1:
            fail(f"{side} protocol wire_version must be 1")
        if record["interface_revision"] not in (3, 4):
            fail(f"{side} protocol interface_revision must be 3 or 4")
    for key in required:
        if np[key] != hp[key]:
            fail(
                f"Native/Helper protocol mismatch: Native {key}={np[key]!r}, "
                f"Helper {key}={hp[key]!r}"
            )


def relative_artifacts(build: Path, artifacts: dict[str, str]) -> str:
    lines = []
    for name in sorted(artifacts):
        if not (build / name).is_file():
            fail(f"artifact disappeared: {name}")
        lines.append(f"{sha256(build / name)}  {name}")
    return "\n".join(lines) + "\n"


def stage_code(app: Path, bundle: dict) -> None:
    share = app / "usr/share/bbport"
    share.mkdir(parents=True, exist_ok=True)
    stage_file(ROOT / "run.sh", share / "run.sh", 0o755)
    copy_tree(ROOT / "scripts", share / "scripts", ignore=("__pycache__", "*.pyc"))
    copy_tree(ROOT / "patches", share / "patches", ignore=("__pycache__", "*.pyc"))
    copy_tree(
        ROOT / "launcher",
        share / "launcher",
        ignore=("__pycache__", "*.pyc", "shell.nix"),
    )
    tools_dst = share / "tools/nr_dev"
    tools_dst.mkdir(parents=True, exist_ok=True)
    for source in (ROOT / "tools/nr_dev").glob("*.py"):
        stage_file(source, tools_dst / source.name)
    # Development READMEs/templates name private workspace roots; they are
    # not runtime inputs and do not belong in the image.
    stage_file(ROOT / "LICENSE", app / "usr/share/doc/bbport/LICENSE")
    (app / "usr/share/doc/bbport/BUILD_ONLY").write_text(
        "BUILD_ONLY: CPU/static packaging candidate; not a GPU exit or online acceptance.\n"
    )
    known = ROOT / "packaging/nrbridge/KNOWN_ISSUES.md"
    if known.is_file():
        stage_file(known, app / "usr/share/doc/bbport/KNOWN_ISSUES.md")
    # The launcher reads this manifest instead of hardcoding a Native commit,
    # Helper build id or protocol revision.
    (share / "nrbridge-bundle.json").write_text(json.dumps(bundle, indent=2) + "\n")
    # Machine-readable provenance for the private preview, including the
    # Helper source/artifact distinction.
    provenance = app / "usr/share/doc/bbport/nrbridge-preview.json"
    provenance.parent.mkdir(parents=True, exist_ok=True)
    provenance.write_text(
        json.dumps(
            {
                "schema": "bbport-nrbridge-preview-v1",
                "native_source": bundle["native"]["source"],
                "native_protocol": bundle["native"].get("protocol", {}),
                "build_status": bundle.get("build_status", "BUILD_ONLY"),
                "daily_ready": bundle.get("daily_ready", False),
                "helper_source_commit": bundle["helper"]["source_head"],
                "helper_build_id": bundle["helper"]["build_id"],
                "helper_artifact_sha256": bundle["helper"]["artifact_sha256"],
                "forwarder_build_id": bundle["forwarder"]["build_id"],
                "forwarder_artifact_sha256": bundle["forwarder"]["artifact_sha256"],
                "model_bundled": False,
                "game_bundled": False,
                "saves_bundled": False,
                "credentials_bundled": False,
                "helper_source_note": (
                    "The Helper artifact is selected by its own content-addressed "
                    "manifest. If the checked source HEAD differs from the build "
                    "manifest source, the difference is recorded here rather than "
                    "claimed as a same-source rebuild."
                ),
            },
            indent=2,
        )
        + "\n"
    )


def stage_fg(app: Path, runtime: Path) -> None:
    """Ship the sealed FG binary and its small source receipt, not an SDK/tree."""
    share=app/'usr/share/bbport'
    record=json.loads((runtime/'m2-wine-probe-build.json').read_text())
    if record.get('schema')!='dlssg-wine-probe-build-v1':fail('Invalid FG runtime manifest')
    for name, identity in record['inputs'].items():
        rel=Path(name)
        if rel.is_absolute() or '..' in rel.parts:fail('Invalid FG source receipt path')
        source=ROOT/rel
        if source.stat().st_size!=identity['bytes'] or sha256(source)!=identity['sha256']:
            fail('FG runtime source receipt mismatch: '+name)
        stage_file(source,share/rel)
    stage_file(ROOT/'tools/fg_dev/build_wine_probe.py',share/'tools/fg_dev/build_wine_probe.py')
    for name,identity in record['artifacts'].items():
        if Path(name).name!=name or sha256(runtime/name)!=identity['sha256']:fail('FG artifact mismatch')
        stage_file(runtime/name,share/'fg-runtime'/name)
    for name in ('m2-wine-probe-build.json','nvcuda.dll'):
        stage_file(runtime/name,share/'fg-runtime'/name)


def stage_native(app: Path, build: Path, artifacts: dict[str, str]) -> dict[str, str]:
    share = app / "usr/share/bbport"
    bin_dir = share / "bin"
    (bin_dir / "gpu").mkdir(parents=True, exist_ok=True)
    stage_file(build / "bb-probe", bin_dir / "bb-probe", 0o755)
    stage_file(build / "bb-gpu-capabilities", bin_dir / "bb-gpu-capabilities", 0o755)
    stage_file(build / "gpu/libbbgpu.so", bin_dir / "gpu/libbbgpu.so", 0o755)
    for name in ("libbbnet.so", "libbbnet20.so"):
        src = build / "gpu" / name
        if src.is_file():
            stage_file(src, bin_dir / "gpu" / name, 0o755)
    stage_file(build / "libbbport_dlss.so", bin_dir / "libbbport_dlss.so", 0o755)
    for ngx in build.glob("libnvidia-ngx-dlss.so.*"):
        stage_file(ngx, bin_dir / ngx.name, 0o755)
    license_path = build / "NVIDIA-DLSS-LICENSE.txt"
    if license_path.is_file():
        stage_file(license_path, share / "NVIDIA-DLSS-LICENSE.txt")
    # Provenance is rewritten to in-tree relative paths because the runtime
    # copy is created under NRBRIDGE_HOME and checked by checked_native_build.
    stage_file(build / "source-head.txt", share / "build-provenance/source-head.txt")
    if (build/'build-inputs.json').is_file():
        stage_file(build/'build-inputs.json',share/'build-provenance/build-inputs.json')
    (share / "build-provenance/source-diff.patch").write_text("")
    staged = {
        name: sha256(bin_dir / name) for name in artifacts
    }
    (share / "build-provenance/artifacts.sha256").write_text(
        "\n".join(f"{digest}  {name}" for name, digest in sorted(staged.items())) + "\n"
    )
    # The four checked artifacts are also the files the staged build needs;
    # keep their names visible at the package root of this subtree.
    (share / "build-provenance/README").write_text(
        "Derived from the read-only Native baseline.  The staged copy has its\n"
        "development RUNPATHs removed, so artifacts.sha256 records the staged\n"
        "hashes; the original hashes stay in the bundle manifest.\n"
    )
    return staged


def refresh_native_provenance(app: Path, bundle: dict) -> None:
    """Record hashes of the artifacts as they actually exist after RPATH edits."""
    share = app / "usr/share/bbport"
    staged = {
        name: sha256(share / "bin" / name)
        for name in bundle["native"]["original_artifacts"]
    }
    bundle["native"]["staged_artifacts"] = staged
    (share / "build-provenance/artifacts.sha256").write_text(
        "\n".join(f"{digest}  {name}" for name, digest in sorted(staged.items())) + "\n"
    )
    (share / "nrbridge-bundle.json").write_text(json.dumps(bundle, indent=2) + "\n")


def stage_helper(app: Path, helper: Path, bundle: dict) -> None:
    dest = app / "usr/share/bbport/nrbridge"
    dest.mkdir(parents=True, exist_ok=True)
    stage_file(helper, dest / "wine_runtime_model.exe", 0o755)
    stage_file(helper.parent / "build.json", dest / "helper-build.json")
    (dest / "forwarder").mkdir(exist_ok=True)
    stage_file(helper_forwarder, dest / "forwarder" / "nvngx.dll_dlssnr.dll", 0o755)
    stage_file(helper.parent / "build.json", dest / "provenance" / "helper-build.json")
    stage_file(
        helper_forwarder.parent / "build.json",
        dest / "provenance" / "forwarder-build.json",
    )
    (dest / "README").write_text(
        "Private NRBridge runtime artifacts. The model DLL is not here:\n"
        "select your own nvngx_dlssnr.dll at setup; it is verified and never copied.\n"
        f"Helper build {bundle['helper']['build_id']}; "
        f"forwarder {bundle['forwarder']['build_id']}.\n"
    )


def stage_fonts(app: Path) -> None:
    fonts = app / "usr/share/fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    sources = [
        (Path("/usr/share/fonts/adobe-source-han-sans/SourceHanSansCN-Regular.otf"),
         "SourceHanSansCN-Regular.otf"),
        (Path("/usr/share/fonts/adobe-source-han-sans/SourceHanSansCN-Bold.otf"),
         "SourceHanSansCN-Bold.otf"),
        (Path("/usr/share/fonts/TTF/DejaVuSans.ttf"), "DejaVuSans.ttf"),
        (Path("/usr/share/fonts/noto/NotoColorEmoji.ttf"), "NotoColorEmoji.ttf"),
    ]
    for src, name in sources:
        if src.is_file():
            stage_file(src, fonts / name)
    etc = app / "etc/fonts"
    etc.mkdir(parents=True, exist_ok=True)
    (etc / "fonts.conf").write_text(
        """<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir prefix="relative">../usr/share/fonts</dir>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <dir prefix="xdg">fonts</dir>
  <cachedir prefix="xdg">bbport/fontconfig</cachedir>
  <alias><family>sans-serif</family><prefer><family>DejaVu Sans</family></prefer></alias>
  <alias><family>serif</family><prefer><family>DejaVu Serif</family></prefer></alias>
  <alias><family>monospace</family><prefer><family>DejaVu Sans Mono</family></prefer></alias>
</fontconfig>
"""
    )


def stage_gtk_resources(app: Path) -> None:
    # Typelibs and compiled schemas are small; taking the complete set avoids
    # missing an Adw/Gtk dependency that is only discovered at runtime.
    copy_tree(Path("/usr/lib/girepository-1.0"), app / "usr/lib/girepository-1.0")
    schemas = app / "usr/share/glib-2.0/schemas"
    schemas.mkdir(parents=True, exist_ok=True)
    shutil.copyfile("/usr/share/glib-2.0/schemas/gschemas.compiled", schemas / "gschemas.compiled")
    for icon_theme in ("Adwaita", "hicolor"):
        src = Path("/usr/share/icons") / icon_theme
        if src.is_dir():
            copy_tree(src, app / "usr/share/icons" / icon_theme)
    gtk_data = Path("/usr/share/gtk-4.0")
    if gtk_data.is_dir():
        copy_tree(gtk_data, app / "usr/share/gtk-4.0")
    # Gdk-pixbuf loaders: the SVG loader is what libadwaita icons need.
    loaders_src = Path("/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders")
    loaders_dst = app / "usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
    if loaders_src.is_dir():
        copy_tree(loaders_src, loaders_dst)
        query = shutil.which("gdk-pixbuf-query-loaders")
        if query:
            out = run([query, *sorted(str(p) for p in loaders_dst.glob("*.so"))])
            # The AppImage mount path changes on every run.  Keep a template
            # with a placeholder; AppRun expands it into the writable data
            # root before GTK starts.
            out = out.replace(str(app), "@APPDIR@")
            (loaders_dst.parent / "loaders.cache.in").write_text(out)
    # Adwaita's symbolic icons are resolved through the icon theme cache.
    cache = shutil.which("gtk4-update-icon-cache")
    if cache:
        for theme in ("Adwaita", "hicolor"):
            root = app / "usr/share/icons" / theme
            if root.is_dir():
                subprocess.run(
                    [cache, "-f", "-t", "-q", str(root)],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )


def stage_python(app: Path) -> None:
    py = Path("/usr/bin") / f"python{PYTHON_VERSION}"
    if not py.is_file():
        fail(f"host python{PYTHON_VERSION} not found")
    stage_file(py, app / "usr/bin" / f"python{PYTHON_VERSION}", 0o755)
    os.symlink(f"python{PYTHON_VERSION}", app / "usr/bin/python3")
    stdlib = Path("/usr/lib") / f"python{PYTHON_VERSION}"
    dest = app / "usr/lib" / f"python{PYTHON_VERSION}"
    ignore = {
        "site-packages",
        "__pycache__",
        "test",
        "tests",
        "idlelib",
        "turtledemo",
        "ensurepip",
        "tkinter",
        "config-3.14-x86_64-linux-gnu",
        "Tools",
    }
    shutil.copytree(
        stdlib,
        dest,
        ignore=shutil.ignore_patterns(*ignore),
        symlinks=False,
    )
    # Only the GObject bindings are needed; do not pull the host's 389 MB of
    # unrelated site-packages into the image.
    copy_tree(
        stdlib / "site-packages/gi",
        dest / "site-packages/gi",
        ignore=("__pycache__", "*.pyc"),
    )
    # Tk is not part of the launcher and its shared library is not in the
    # pruned closure; keeping the extension would only create a dangling need.
    for module in dest.glob("lib-dynload/_tkinter*.so"):
        module.unlink()


def stage_gtk_libs(app: Path, libdir: Path) -> list[Path]:
    names = [
        "libgtk-4.so.1",
        "libadwaita-1.so.0",
        "libgdk_pixbuf-2.0.so.0",
        "libgirepository-1.0.so.1",
        "libgirepository-2.0.so.0",
    ]
    roots: list[Path] = []
    for name in names:
        matches = sorted(Path("/usr/lib").glob(name + "*"))
        matches = [p for p in matches if p.is_file() and not p.is_symlink()]
        if not matches:
            fail(f"missing host GTK dependency {name}")
        roots.append(matches[0])
    return roots


def patch_rpath(path: Path, rpath: str, patchelf: str, *, check: bool = True) -> None:
    path.chmod(path.stat().st_mode | 0o200)
    def versions() -> str:
        # patchelf legitimately moves sections, so their file offsets change.
        # The version names/indices must not.
        return "\n".join(
            line for line in run(["readelf", "-V", str(path)]).splitlines()
            if "Addr:" not in line and "Offset:" not in line
        )

    if check:
        before = versions()
    run([patchelf, "--remove-rpath", str(path)])
    run([patchelf, "--set-rpath", rpath, str(path)])
    if check:
        after = versions()
        if before != after:
            fail(f"patchelf changed symbol versions in {path}")


def build_closure(app: Path, patchelf: str, native: Path, online: Path) -> dict[str, str]:
    libdir = app / "usr/lib/bbport"
    search = [libdir, native / "gpu", online]
    # Build-time sysroot libraries are part of the closure but not of the
    # image's GTK/Python set; keep them in the same directory so the copied
    # RUNPATHs can be reduced to $ORIGIN.
    for extra in (DEFAULT_SYSROOT_LIB,):
        if extra.is_dir():
            search.append(extra)
    gtk_roots = stage_gtk_libs(app, libdir)
    # Copy the GTK roots first so ldd can scan the package copies.
    for root in gtk_roots:
        _copy_lib(root, libdir / root.name)
    roots = [
        app / "usr/share/bbport/bin/bb-probe",
        app / "usr/share/bbport/bin/gpu/libbbgpu.so",
        app / "usr/share/bbport/bin/gpu/libbbnet.so",
        app / "usr/share/bbport/bin/gpu/libbbnet20.so",
        app / "usr/share/bbport/bin/libbbport_dlss.so",
        app / "usr/bin" / f"python{PYTHON_VERSION}",
    ]
    for module in sorted((app / "usr/lib" / f"python{PYTHON_VERSION}" / "lib-dynload").glob("*.so")):
        roots.append(module)
    for module in sorted((app / "usr/lib" / f"python{PYTHON_VERSION}" / "site-packages/gi").glob("*.so")):
        roots.append(module)
    for loader in sorted((app / "usr/lib/gdk-pixbuf-2.0/2.10.0/loaders").glob("*.so")):
        roots.append(loader)
    roots.extend(gtk_roots)
    copied = collect(roots, libdir, search, verbose=False)
    leaked = sorted(path.name for path in libdir.iterdir() if _is_project_owned(path.name))
    if leaked:
        fail(f"project-owned libraries leaked into the generic closure: {leaked}")
    print(f"runtime closure: {len(set(copied.values()))} host libraries copied")
    # Runtime search: our own libraries, then the host.  Patch only ELFs that
    # still name a development tree; rewriting an unrelated system library's
    # RUNPATH is unnecessary and patchelf can reorder its symbol versions.
    for elf in sorted(libdir.glob("*")):
        if not elf.is_file() or not os.access(elf, os.X_OK):
            continue
        dynamic = run(["readelf", "-d", str(elf)])
        if any(
            tag in line and ("/home/" in line or "/nix/store" in line)
            for line in dynamic.splitlines()
            for tag in ("RPATH", "RUNPATH")
        ):
            patch_rpath(elf, "$ORIGIN", patchelf)
    for rel, rpath in (
        ("usr/share/bbport/bin/bb-probe", "$ORIGIN/gpu"),
        ("usr/share/bbport/bin/bb-gpu-capabilities", "$ORIGIN:$ORIGIN/gpu"),
        ("usr/share/bbport/bin/gpu/libbbgpu.so", "$ORIGIN:$ORIGIN/.."),
        ("usr/share/bbport/bin/gpu/libbbnet.so", "$ORIGIN:$ORIGIN/.."),
        ("usr/share/bbport/bin/gpu/libbbnet20.so", "$ORIGIN:$ORIGIN/.."),
        ("usr/share/bbport/bin/libbbport_dlss.so", "$ORIGIN"),
    ):
        path = app / rel
        if path.is_file():
            patch_rpath(path, rpath, patchelf)
    return copied


def _copy_lib(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists():
        shutil.copy2(src.resolve(), dst, follow_symlinks=True)
        dst.chmod(0o755)


def write_apprun(app: Path) -> None:
    apprun = app / "AppRun"
    apprun.write_text(
        r"""#!/bin/sh
# Private bbport NRBridge preview.  All writable state goes to NRBRIDGE_HOME;
# the AppImage mount is read-only and is never used as a data directory.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APPDIR=$HERE
export APPDIR
export PATH="$APPDIR/usr/bin:$PATH"
export LD_LIBRARY_PATH="$APPDIR/usr/lib/bbport${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONHOME="$APPDIR/usr"
export PYTHONPATH="$APPDIR/usr/lib/python3.14/site-packages${PYTHONPATH:+:$PYTHONPATH}"
export GI_TYPELIB_PATH="$APPDIR/usr/lib/girepository-1.0"
export GSETTINGS_SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas"
# The loader cache cannot contain the build-time AppDir path: the AppImage is
# mounted elsewhere.  Expand the bundled template into the writable data root.
GDK_CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/bbport/gdk-pixbuf"
mkdir -p "$GDK_CACHE"
if [ -r "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in" ]; then
    sed "s|@APPDIR@|$APPDIR|g" "$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache.in" > "$GDK_CACHE/loaders.cache"
    export GDK_PIXBUF_MODULE_FILE="$GDK_CACHE/loaders.cache"
fi
export GDK_PIXBUF_MODULEDIR="$APPDIR/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders"
export XDG_DATA_DIRS="$APPDIR/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export FONTCONFIG_FILE="$APPDIR/etc/fonts/fonts.conf"
export FONTCONFIG_PATH="$APPDIR/etc/fonts"
export LC_ALL="${LC_ALL:-C.UTF-8}"
if [ ! -r "${SSL_CERT_FILE:-}" ]; then
    for bundle in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/cert.pem; do
        if [ -r "$bundle" ]; then export SSL_CERT_FILE="$bundle"; break; fi
    done
fi
# Direct launches from an existing installed packages/ layout share its
# original settings and saves. Markers distinguish it from standalone files;
# each explicitly supplied environment value still wins independently.
if [ -n "${APPIMAGE:-}" ]; then
    NR_IMAGE_REAL=$(readlink -f -- "$APPIMAGE" 2>/dev/null || true)
    case "$NR_IMAGE_REAL" in
        *.AppImage)
            NR_PACKAGE_DIR=$(dirname -- "$NR_IMAGE_REAL")
            NR_INSTALL_ROOT=$(dirname -- "$NR_PACKAGE_DIR")
            if [ -f "$NR_IMAGE_REAL" ] && [ "$(basename -- "$NR_PACKAGE_DIR")" = packages ] &&
               [ -f "$NR_INSTALL_ROOT/bbport.ini" ] &&
               [ -f "$NR_INSTALL_ROOT/config/bbport-launcher/settings.json" ]; then
                export BB_DATA_DIR="${BB_DATA_DIR-$NR_INSTALL_ROOT/data}"
                export BB_CONFIG="${BB_CONFIG-$NR_INSTALL_ROOT/bbport.ini}"
                export XDG_CONFIG_HOME="${XDG_CONFIG_HOME-$NR_INSTALL_ROOT/config}"
                export NRBRIDGE_HOME="${NRBRIDGE_HOME-$NR_INSTALL_ROOT/data/nrbridge}"
            fi
            ;;
    esac
fi
export NRBRIDGE_HOME="${NRBRIDGE_HOME-${XDG_DATA_HOME:-$HOME/.local/share}/bbport/nrbridge-home}"
export BB_PREBUILT=1
exec "$APPDIR/usr/bin/python3.14" "$APPDIR/usr/share/bbport/launcher/nrbridge_entry.py" "$@"
"""
    )
    apprun.chmod(0o755)
    (app / "bbport.desktop").write_text(
        """[Desktop Entry]
Type=Application
Name=bbport NRBridge Preview
Comment=Private Bloodborne NRBridge preview; select your own Wine/GE and NR model
Exec=AppRun
Icon=bbport
Terminal=false
Categories=Game;
"""
    )
    icon_svg = ROOT / "launcher/assets/bloodborne-icon.svg"
    if icon_svg.is_file():
        converter = shutil.which("rsvg-convert")
        if converter:
            run([converter, "-w", "256", "-h", "256", "-o", str(app / "bbport.png"), str(icon_svg)])
        shutil.copyfile(icon_svg, app / "bbport.svg")
        (app / ".DirIcon").write_bytes((app / "bbport.png").read_bytes())
    else:
        fail("launcher icon missing")


def write_bundle_manifest(app: Path, args, bundle: dict) -> None:
    files = []
    for path in sorted(app.rglob("*")):
        if path.is_file() and not path.is_symlink():
            files.append(
                {
                    "path": str(path.relative_to(app)),
                    "bytes": path.stat().st_size,
                    "sha256": sha256(path),
                }
            )
    record = {
        "schema": "bbport-nrbridge-appdir-v1",
        "native_source": bundle["native"]["source"],
        "build_status": bundle.get("build_status", "BUILD_ONLY"),
        "daily_ready": bundle.get("daily_ready", False),
        "helper_source_commit": bundle["helper"]["source_head"],
        "helper_build_id": bundle["helper"]["build_id"],
        "forwarder_build_id": bundle["forwarder"]["build_id"],
        "appimage": APPIMAGE_NAME,
        "files": files,
        "total_bytes": sum(item["bytes"] for item in files),
        "model_bundled": False,
        "game_bundled": False,
        "saves_bundled": False,
        "credentials_bundled": False,
        "notes": [
            "NRBRIDGE_HOME is created on first run; this AppDir is read-only.",
            "Wine/GE, game and NR model are selected by the user and validated before launch.",
        ],
    }
    (app / "usr/share/doc/bbport/appdir-manifest.json").write_text(
        json.dumps(record, indent=2) + "\n"
    )


def static_verify(app: Path, development_roots=()) -> None:
    # 1. No historical build path may remain in an ELF RUNPATH or interpreter.
    forbidden = tuple(str(Path(path).resolve()) for path in development_roots)
    for path in sorted(app.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        if path.suffix not in (".so", "") and not os.access(path, os.X_OK):
            continue
        try:
            with path.open("rb") as stream:
                if stream.read(4) != b"\x7fELF":
                    continue
        except OSError:
            continue
        dynamic = run(["readelf", "-d", str(path)])
        for line in dynamic.splitlines():
            if "RPATH" in line or "RUNPATH" in line:
                if any(needle in line for needle in (*forbidden, "/home/", "/Users/", "/nix/store/")):
                    fail(f"{path}: development RUNPATH remains: {line.strip()}")
        interpreter = run(["readelf", "-l", str(path)])
        if "Requesting program interpreter" in interpreter:
            if not re.search(r"interpreter: /lib64/ld-linux-x86-64\.so\.2", interpreter):
                fail(f"{path}: unexpected ELF interpreter")
    # 2. The private model, game, saves and credentials are never in the image.
    forbidden_names = {
        "nvngx_dlssnr.dll",
        "eboot.bin",
        "npbind.dat",
        "credential.json",
        "token.json",
    }
    for path in app.rglob("*"):
        if path.name.lower() in forbidden_names:
            fail(f"forbidden private artifact in image: {path}")
    # 3. Project-owned GPU/net/DLSS libraries stay in the Native build tree.
    # A copy under the generic libdir would win through the runner's
    # LD_LIBRARY_PATH and bypass the pinned source mapping.
    libdir = app / "usr/lib/bbport"
    leaked = sorted(path.name for path in libdir.iterdir() if _is_project_owned(path.name))
    if leaked:
        fail(f"project-owned libraries in the generic library directory: {leaked}")
    # 4. Every bundled executable must resolve with the private libdir first.
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = str(libdir)
    for rel in (
        "usr/share/bbport/bin/bb-probe",
        "usr/share/bbport/bin/gpu/libbbgpu.so",
        "usr/share/bbport/bin/gpu/libbbnet.so",
        "usr/share/bbport/bin/gpu/libbbnet20.so",
        "usr/bin/python3.14",
    ):
        path = app / rel
        result = subprocess.run(
            ["ldd", str(path)], capture_output=True, text=True, env=env, timeout=120
        )
        if result.returncode != 0 or "not found" in result.stdout:
            fail(f"{rel}: unresolved runtime library\n{result.stdout}{result.stderr}")
        for line in result.stdout.splitlines():
            if any(needle in line for needle in forbidden):
                fail(f"{rel}: resolves into a historical build path: {line.strip()}")
        # The Native loader and its online modules must resolve from the
        # staged Native build tree, never from the generic closure copy.
        if rel == "usr/share/bbport/bin/bb-probe":
            gpu = app / "usr/share/bbport/bin/gpu/libbbgpu.so"
            if f"libbbgpu.so => {gpu}" not in result.stdout:
                fail(
                    "bb-probe does not resolve libbbgpu.so from its own gpu tree "
                    f"(expected {gpu})\n{result.stdout}"
                )


def write_build_report(app: Path, args, bundle: dict, target: Path | None) -> None:
    report = {
        "schema": "bbport-nrbridge-build-report-v1",
        "appdir": str(app),
        "inputs": {
            "native_build": str(args.native_build),
            "helper": str(args.helper),
            "forwarder": str(args.forwarder),
            "online_deps": str(args.online_deps),
            "sysroot_lib": str(args.sysroot_lib),
        },
        "bundle": bundle,
        "appimage": None if target is None else {
            "path": str(target),
            "bytes": target.stat().st_size,
            "sha256": sha256(target),
        },
    }
    path = app.parent / "build-report.json"
    path.write_text(json.dumps(report, indent=2) + "\n")


def make_appimage(app: Path, out: Path, tool: Path) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    target = out / APPIMAGE_NAME
    if target.exists():
        target.unlink()
    env = dict(os.environ)
    env.setdefault("ARCH", "x86_64")
    env.setdefault("SOURCE_DATE_EPOCH", "1792454400")  # 2026-10-20
    command = [str(tool), "--appimage-extract-and-run"]
    runtime = tool.parent / "runtime-x86_64"
    if runtime.is_file():
        command += ["--runtime-file", str(runtime)]
    command += [str(app), str(target)]
    run(command, env=env)
    target.chmod(0o755)
    return target


def main(argv=None) -> int:
    global helper_forwarder, DEFAULT_SYSROOT_LIB
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-build", type=Path, default=DEFAULT_NATIVE)
    parser.add_argument("--helper", type=Path, default=DEFAULT_HELPER)
    parser.add_argument("--forwarder", type=Path, default=DEFAULT_FORWARDER)
    parser.add_argument("--online-deps", type=Path, default=DEFAULT_ONLINE)
    parser.add_argument("--sysroot-lib", type=Path, default=DEFAULT_SYSROOT_LIB)
    parser.add_argument("--out", type=Path, default=ROOT / "dist")
    parser.add_argument("--appdir", type=Path, default=ROOT / "out/nrbridge-appimage/AppDir")
    parser.add_argument("--appimagetool", type=Path, default=ROOT / "out/tools/appimagetool-x86_64.AppImage")
    parser.add_argument("--patchelf", type=Path, default=ROOT / "out/tools/patchelf-venv/bin/patchelf")
    parser.add_argument("--skip-appimage", action="store_true")
    parser.add_argument("--reuse-appdir", type=Path, help="Reuse an installed GTK/Python closure with copy-on-write; never modify its files")
    parser.add_argument("--fg-runtime", type=Path, help="Existing sealed Wine FG runtime, packaged without a Wine prefix")
    parser.add_argument("--keep-appdir", action="store_true")
    args = parser.parse_args(argv)

    helper_forwarder = args.forwarder
    DEFAULT_SYSROOT_LIB = args.sysroot_lib
    if args.online_deps is None:
        parser.error("--online-deps or BB_ONLINE_DEPS_ROOT is required")
    if not args.online_deps.is_dir():
        fail(f"online dependency library directory missing: {args.online_deps}")
    if not args.patchelf.is_file():
        fail(f"patchelf not found: {args.patchelf} (use an isolated venv)")

    native = verify_native(args.native_build)
    if not native.get("daily_portable", False):
        print(
            "warning: selected Native has no NRBRIDGE_NATIVE_DAILY_RING64_V2 marker; "
            "this AppImage is a static/preview candidate only"
        )
    helper = verify_helper(args.helper)
    forwarder = verify_forwarder(args.forwarder)
    protocol_matches(native, helper)
    bundle = {
        "schema": "bbport-nrbridge-bundle-v1",
        "build_status": "BUILD_ONLY",
        "native": native,
        "helper": helper,
        "forwarder": forwarder,
        "daily_ready": bool(
            native.get("daily_portable")
            and helper["protocol"].get("interface_revision") == 4
        ),
        "model_bundled": False,
        "game_bundled": False,
        "saves_bundled": False,
        "credentials_bundled": False,
    }

    app = args.appdir.resolve()
    if app.exists():
        # Only the builder-owned AppDir is removed.  Verify the resolved path.
        expected = (ROOT / "out/nrbridge-appimage/AppDir").resolve()
        if app != expected:
            fail(f"refusing to remove unexpected AppDir {app} (expected {expected})")
        shutil.rmtree(app)
    app.mkdir(parents=True)
    if args.reuse_appdir:
        prior=args.reuse_appdir.resolve()
        if not (prior/'usr/share/bbport/nrbridge-bundle.json').is_file():fail('Not a reusable bbport AppDir')
        if app==prior or app in prior.parents or prior in app.parents:fail('AppDir paths must be separate')
        run(['cp','-a','--reflink=auto',str(prior)+'/.' ,str(app)])
        shutil.rmtree(app/'usr/share/bbport')
        for name in ('appdir-manifest.json','nrbridge-preview.json','BUILD_ONLY'):
            (app/'usr/share/doc/bbport'/name).unlink(missing_ok=True)
    staged = stage_native(app, args.native_build, native["artifacts"])
    bundle["native"]["original_artifacts"] = native["artifacts"]
    bundle["native"]["staged_artifacts"] = staged
    stage_code(app, bundle)
    stage_helper(app, args.helper, bundle)
    if args.fg_runtime:stage_fg(app,args.fg_runtime)
    if not args.reuse_appdir:
        stage_fonts(app)
        stage_gtk_resources(app)
        stage_python(app)
    build_closure(app, str(args.patchelf), args.native_build, args.online_deps)
    refresh_native_provenance(app, bundle)
    write_apprun(app)
    write_bundle_manifest(app, args, bundle)
    static_verify(app, (args.native_build, args.helper.parent, args.forwarder.parent,
                        args.sysroot_lib, args.online_deps))
    print(f"AppDir: {app}")
    if args.skip_appimage:
        write_build_report(app, args, bundle, None)
        return 0
    target = make_appimage(app, args.out.resolve(), args.appimagetool)
    write_build_report(app, args, bundle, target)
    print(f"AppImage: {target} ({target.stat().st_size / 1e6:.1f} MB)")
    if not args.keep_appdir:
        # Keep the AppDir by default: it is the fast path for the next build
        # and for static verification.  A removed AppDir is only for CI.
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
