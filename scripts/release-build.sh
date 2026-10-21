#!/usr/bin/env bash
# Immutable release/baseline build, separate from the daily development out/ tree.
#
#   bash scripts/release-build.sh --worktree .worktree/fg --test
#   bash scripts/release-build.sh --id 20261022-my-baseline --no-appimage
#
# Writes out/releases/<id>/{build,manifest.json,build.log}; the build tree is made read-only.
# The AppImage is written to out/releases/<id>/<name>.AppImage when nix is available. Old
# releases are reclaimed by scripts/artifact-gc.py (dry-run by default).
set -euo pipefail
# See scripts/dev-build.sh: locate the main worktree even when this file is copied into a branch.
main_worktree() {
    local first
    first=$(git -C "$1" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | head -n1)
    printf '%s\n' "${first:-$1}"
}
script_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
shared_root=${BB_SHARED_ROOT:-$(main_worktree "$script_root")}
shared_root=$(cd -- "$shared_root" && pwd)
target=$PWD
id=''
allow_dirty=0
no_appimage=0
test_args=()
while (($#)); do
    case $1 in
        --worktree)
            [[ $# -ge 2 ]] || { echo '--worktree needs a path' >&2; exit 2; }
            target=$2; shift 2 ;;
        --id)
            [[ $# -ge 2 ]] || { echo '--id needs a value' >&2; exit 2; }
            id=$2; shift 2 ;;
        --allow-dirty) allow_dirty=1; shift ;;
        --no-appimage) no_appimage=1; shift ;;
        --test) test_args+=(--test); shift ;;
        -h|--help)
            sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
target=$(cd -- "$target" && pwd)
cd "$target"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "Not a git worktree: $target" >&2; exit 2; }

head=$(git rev-parse HEAD)
short=$(git rev-parse --short=8 HEAD)
# Submodule dirt from gpu/patches/fsr-vulkan is a normal build side effect, not a source change.
if [[ -n $(git status --porcelain --ignore-submodules=dirty) ]]; then
    if ((!allow_dirty)); then
        echo 'Refusing a release build from a dirty worktree; commit/stash or pass --allow-dirty.' >&2
        git status --short --ignore-submodules=dirty >&2
        exit 1
    fi
    dirty=true
else
    dirty=false
fi
[[ -n $id ]] || id="$(date -u +%Y%m%dT%H%M%SZ)-$short"
release_root=$shared_root/out/releases/$id
[[ ! -e $release_root ]] || { echo "Release already exists: $release_root" >&2; exit 1; }
mkdir -p "$release_root"

echo "release: $id"
echo "  source: $target @ $head (dirty=$dirty)"
echo "  output: $release_root"

# Build into the release tree, not into the worktree's out/.
if ! BB_OUT_DIR="$release_root/build" BB_LTO=ON BB_PGO="${BB_PGO:-off}" \
    bash "$shared_root/scripts/dev-build.sh" --worktree "$target" "${test_args[@]}" \
    >"$release_root/build.log" 2>&1; then
    tail -60 "$release_root/build.log" >&2
    echo "Release build failed (full log: $release_root/build.log)" >&2
    exit 1
fi

appimage=''
if ((!no_appimage)); then
    if command -v nix >/dev/null; then
        if BB_OUT_DIR="$release_root/build" BB_DIST_DIR="$release_root" \
            BB_APPIMAGE_NAME="Bloodborne-bbport-$id.AppImage" \
            bash "$target/packaging/appimage.sh" >"$release_root/appimage.log" 2>&1; then
            appimage="Bloodborne-bbport-$id.AppImage"
        else
            tail -30 "$release_root/appimage.log" >&2
            echo 'AppImage packaging failed; build artifacts were kept.' >&2
        fi
    else
        echo 'nix not available; skipping AppImage (build artifacts kept).' >&2
    fi
fi

python3 - "$release_root" "$target" "$head" "$dirty" "$appimage" <<'PY'
import hashlib, json, os, pathlib, sys
root = pathlib.Path(sys.argv[1])
target, head, dirty, appimage = sys.argv[2], sys.argv[3], sys.argv[4] == 'true', sys.argv[5]
def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()
build = root / 'build'
artifacts = []
for p in sorted(build.rglob('*')):
    if p.is_file() and (p.name.startswith('bb-') or p.name.startswith('lib') or p.name.endswith('-test')):
        artifacts.append({'path': str(p.relative_to(root)), 'bytes': p.stat().st_size, 'sha256': sha(p)})
manifest = {
    'id': root.name,
    'source': target,
    'head': head,
    'dirty': dirty,
    'createdAt': __import__('datetime').datetime.now(__import__('datetime').timezone.utc).isoformat(),
    'artifacts': artifacts,
    'appimage': appimage or None,
}
(root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f"manifest: {root / 'manifest.json'} ({len(artifacts)} artifacts)")
PY

# Immutable release tree: future development must use out/, not this directory.
find "$release_root/build" -type f -exec chmod a-w {} + 2>/dev/null || true
echo "release ready: $release_root"
