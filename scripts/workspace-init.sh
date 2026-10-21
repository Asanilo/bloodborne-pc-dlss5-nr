#!/usr/bin/env bash
# Prepare the two independent development worktrees (NR and FG) without copying dependencies.
#
#   bash scripts/workspace-init.sh
#   bash scripts/workspace-init.sh --nr .worktree/nrbridge --fg .worktree/fg --base nrbridge
#
# Each worktree gets out/deps -> main workspace out/deps (read-only shared SDK/headers), its
# submodules checked out locally, and an ignored out/WORKSPACE.md with the build commands.
# Source logic is not modified.
set -euo pipefail
# See scripts/dev-build.sh: operate on the main worktree even if this file lives in a branch.
main_worktree() {
    local first
    first=$(git -C "$1" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | head -n1)
    printf '%s\n' "${first:-$1}"
}
script_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=${BB_SHARED_ROOT:-$(main_worktree "$script_root")}
cd "$root"
nr=.worktree/nrbridge
fg=.worktree/fg
base=nrbridge
while (($#)); do
    case $1 in
        --nr) nr=$2; shift 2 ;;
        --fg) fg=$2; shift 2 ;;
        --base) base=$2; shift 2 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ -f out/deps/bbport-env.sh ]] || { echo 'Missing shared dependencies: out/deps/bbport-env.sh' >&2; exit 2; }
deps=$(cd out/deps && pwd -P)

# NR is the existing nrbridge worktree; FG branches from it by default so it starts from the
# latest playable source. Neither branch is rewritten here.
if [[ -d $nr && ! -e $nr/.git && -z $(ls -A "$nr" 2>/dev/null) ]]; then rmdir "$nr"; fi
if [[ -d $fg && ! -e $fg/.git && -z $(ls -A "$fg" 2>/dev/null) ]]; then rmdir "$fg"; fi
if [[ ! -d $nr/.git && ! -f $nr/.git ]]; then
    if git show-ref --verify --quiet refs/heads/nrbridge; then
        git worktree add "$nr" nrbridge
    else
        echo "NR worktree not found and branch nrbridge is missing: $nr" >&2
        exit 1
    fi
fi
if [[ ! -d $fg/.git && ! -f $fg/.git ]]; then
    if git show-ref --verify --quiet refs/heads/fg; then
        git worktree add "$fg" fg
    else
        git worktree add -b fg "$fg" "$base"
    fi
fi

for wt in "$nr" "$fg"; do
    wt=$(cd "$wt" && pwd -P)
    mkdir -p "$wt/out"
    if [[ ! -e $wt/out/deps ]]; then
        link=$(realpath --relative-to="$wt/out" "$deps")
        ln -s "$link" "$wt/out/deps"
        echo "linked $wt/out/deps -> $link"
    fi
    # Submodules are per-worktree checkouts; build.sh applies the FSR-Vulkan patches to each.
    if [[ ! -f $wt/gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f $wt/gpu/third_party/imgui/imgui.h || ! -f $wt/third_party/LibAtrac9/C/src/decoder.c ]]; then
        echo "initialising submodules in $wt"
        git -C "$wt" submodule update --init --recursive
    fi
    cat > "$wt/out/WORKSPACE.md" <<EOF
# Development workspace

Source: \`$wt\`
Shared dependencies: \`$deps\` (symlinked as \`out/deps\`; do not copy)
Shared compiler cache: \`$root/out/cache/ccache\`

Daily incremental build (one mutable tree, no new copy per experiment):

\`\`\`bash
bash $root/scripts/dev-build.sh --worktree $wt --test
\`\`\`

Clean only rebuildable outputs (keeps evidence and shared deps):

\`\`\`bash
bash $root/scripts/dev-build.sh --worktree $wt --clean
\`\`\`

Immutable release/baseline (writes out/releases/<id>/, not this out/):

\`\`\`bash
bash $root/scripts/release-build.sh --worktree $wt --test
\`\`\`

Reclaim old releases/AppImages (dry-run first):

\`\`\`bash
python3 $root/scripts/artifact-gc.py
python3 $root/scripts/artifact-gc.py --apply
\`\`\`
EOF
done

echo
echo 'worktrees:'
git worktree list
echo
echo "NR: $nr  ($(git -C "$nr" rev-parse --short HEAD) $(git -C "$nr" branch --show-current))"
echo "FG: $fg  ($(git -C "$fg" rev-parse --short HEAD) $(git -C "$fg" branch --show-current))"
echo "build: bash $root/scripts/dev-build.sh --worktree $nr --test"
echo "       bash $root/scripts/dev-build.sh --worktree $fg --test"
