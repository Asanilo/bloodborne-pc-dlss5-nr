#!/usr/bin/env bash
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
ROOT=${NR_DEV_ROOT:-/home/arinp/Code/nrbridge-dev}
# Prepared, patched source copies and the SDK/sysroot environment are independent
# read-only inputs. Initialization stays an explicit preparation operation.
DEPS=${NR_SOURCE_DEPS_ROOT:-$ROOT/inputs/source-deps}
ENV_ROOT=${NR_BUILD_ENV_ROOT:-/home/arinp/Code/bloodborne_pc/out/deps}
BUILD=${BB_BUILD_DIR:-$ROOT/build/native}
fail() { echo "nr-dev build: $*" >&2; exit 3; }
for path in "$ROOT" "$DEPS" "$ENV_ROOT" "$BUILD"; do
 [[ $path == /* ]] || fail "absolute input/output paths required: $path"
done
# Verify the in-repository protocol copy before any build/lock/link writes.
PROTOCOL_PROOF=$(python3 "$WT/tools/nr_dev/check_protocol_export.py" --native-root "$WT")
DEPS=$(realpath -e -- "$DEPS") || fail 'missing prepared source dependencies'
ENV_ROOT=$(realpath -e -- "$ENV_ROOT") || fail 'missing SDK/sysroot environment'
BUILD=$(realpath -m -- "$BUILD")
# Verify the complete file inventory, including unexpected files, before links,
# output directories, locks, provenance or compilation can be written.
python3 - "$DEPS" "$ENV_ROOT" "$BUILD" "$WT" <<'PY'
import hashlib,re,sys
from pathlib import Path
root,env,build,wt=map(Path,sys.argv[1:])
def fail(message):raise SystemExit('nr-dev build: '+message)
for source in (root,env,wt):
    if build.is_relative_to(source) or source.is_relative_to(build):
        fail('build output overlaps an input/source root: '+str(source))
for name in ('fsr-vulkan','imgui','LibAtrac9'):
    directory=root/name;manifest=root/(name+'.content.sha256')
    if not directory.is_dir() or directory.is_symlink() or not manifest.is_file() or manifest.is_symlink():
        fail('missing regular prepared dependency/manifest: '+name)
    expected={}
    for line in manifest.read_text().splitlines():
        match=re.fullmatch(r'([0-9a-f]{64})  \./(.+)',line)
        if not match:fail('invalid dependency manifest: '+str(manifest))
        digest,relative=match.groups();path=Path(relative)
        if path.is_absolute() or '..' in path.parts or relative in expected:
            fail('unsafe/duplicate manifest path: '+relative)
        expected[relative]=digest
    if not expected:fail('empty dependency manifest: '+str(manifest))
    actual={}
    for path in directory.rglob('*'):
        if path.is_symlink():fail('dependency symlink not in locked file inventory: '+str(path))
        if path.is_file():actual[str(path.relative_to(directory))]=path
    if actual.keys()!=expected.keys():fail('dependency file inventory drift: '+name)
    for relative,path in actual.items():
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected[relative]:
            fail('dependency content drift: '+str(path))
PY
[[ -f $ENV_ROOT/bbport-env.sh && -d $ENV_ROOT/root/usr && -d $ENV_ROOT/dlss-sdk ]] || fail 'incomplete SDK/sysroot environment'
# Prepared inputs are selected directly by the lower-level build and CMake.
# Existing Git submodule checkouts and links remain untouched and unused.
[[ -f $DEPS/fsr-vulkan/CMakeLists.txt && -f $DEPS/imgui/imgui.h && -d $DEPS/LibAtrac9/C/src ]] || fail 'incomplete source dependency layout'
# The lower-level build.sh retains submodule initialization/patch behavior. This
# wrapper accepts only the already-patched closure, so it cannot mutate inputs.
for patch in "$WT"/gpu/patches/fsr-vulkan/*.patch; do
 git -C "$DEPS/fsr-vulkan" apply --reverse --check "$patch" 2>/dev/null || fail "prepared FSR patch missing: $patch"
done
source "$ENV_ROOT/bbport-env.sh"
# Sourcing a relocated env script alone does not relocate its exports. Reject
# old absolute SDK/sysroot paths instead of silently using a different tree.
[[ $(realpath -e -- "${DLSS_SDK_ROOT:-/nonexistent}") == "$ENV_ROOT/dlss-sdk" ]] || fail 'environment DLSS_SDK_ROOT does not select configured SDK'
[[ $(realpath -e -- "${VULKAN_INCLUDE:-/nonexistent}") == "$ENV_ROOT/root/usr/include" ]] || fail 'environment VULKAN_INCLUDE does not select configured sysroot'
[[ ${CMAKE_PREFIX_PATH%%:*} == "$ENV_ROOT/root/usr" ]] || fail 'environment CMAKE_PREFIX_PATH does not select configured sysroot'
export PKG_CONFIG_PATH="$ENV_ROOT/root/usr/lib/pkgconfig:$ENV_ROOT/root/usr/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
mkdir -p "$BUILD"
exec 9>"$BUILD/build.lock"
flock -n 9 || fail 'build directory already has a writer'
export NR_SOURCE_DEPS_ROOT="$DEPS" BB_BUILD_DIR="$BUILD" BB_PGO=off BB_LTO=OFF CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-8}
git -C "$WT" rev-parse HEAD > "$BUILD/source-head.txt"
git -C "$WT" diff --ignore-submodules=all --binary HEAD > "$BUILD/source-diff.patch"
# Record selected inputs and the env's actual exports, not a claim of runtime
# confinement or a portable sysroot. Runner/GPU paths are configured separately.
python3 - "$DEPS" "$ENV_ROOT" "$BUILD" "$WT" "$PROTOCOL_PROOF" "$@" <<'PY'
import hashlib,json,os,sys
from pathlib import Path
root,env,build,wt=map(Path,sys.argv[1:5])
protocol_proof=json.loads(sys.argv[5])
protocol_dir=wt/'gpu/render'
files=[env/'bbport-env.sh',*(root/(n+'.content.sha256') for n in ('fsr-vulkan','imgui','LibAtrac9')),
       protocol_dir/'native_runtime_model.h',protocol_dir/'native_runtime_model.export.json',
       wt/'tools/nr_dev/check_protocol_export.py']
record={'source_deps_root':str(root),'build_env_root':str(env),'build_dir':str(build),
        'protocol':protocol_proof,'wrapper_args':sys.argv[6:],'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        'environment':{key:os.environ.get(key) for key in ('DLSS_SDK_ROOT','VULKAN_INCLUDE','VULKAN_LIBRARY','CMAKE_PREFIX_PATH','CPATH','LIBRARY_PATH','LD_LIBRARY_PATH','PKG_CONFIG_PATH','CC','CXX','BB_BUILD_JOBS','CMAKE_BUILD_PARALLEL_LEVEL','BB_PGO','BB_LTO')}}
(build/'build-inputs.json').write_text(json.dumps(record,indent=2)+'\n')
PY
bash "$WT/build.sh" "$@" 2>&1 | tee "$BUILD/build.log"
sha256sum "$BUILD/bb-probe" "$BUILD/gpu/libbbgpu.so" "$BUILD/libbbport_dlss.so" "$BUILD"/libnvidia-ngx-dlss.so.* > "$BUILD/artifacts.sha256"
