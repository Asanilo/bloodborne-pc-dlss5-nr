#!/usr/bin/env bash
# Install the local compiler cache used by scripts/dev-build.sh.
#
#   bash scripts/install-toolchain.sh
#
# Puts the static ccache release under out/toolchain/ (ignored by git) and creates the
# cc/c++ wrappers. Nothing is installed system-wide and no sudo is needed.
set -euo pipefail
# See scripts/dev-build.sh: install into the main worktree even if this file lives in a branch.
main_worktree() {
    local first
    first=$(git -C "$1" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | head -n1)
    printf '%s\n' "${first:-$1}"
}
script_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=${BB_SHARED_ROOT:-$(main_worktree "$script_root")}
cd "$root"
version=4.11.3
url="https://github.com/ccache/ccache/releases/download/v$version/ccache-$version-linux-x86_64.tar.xz"
sha256=7766991b91b3a5a177ab33fa043fe09e72c68586d5a86d20a563a05b74f119c0
dest=out/toolchain/ccache
mkdir -p out/toolchain out/cache/ccache
if [[ -x $dest/ccache ]]; then
    echo "ccache already installed: $dest/ccache"
else
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    echo "downloading $url"
    curl -fL --retry 3 -o "$tmp/ccache.tar.xz" "$url"
    echo "$sha256  $tmp/ccache.tar.xz" | sha256sum -c -
    tar -xJf "$tmp/ccache.tar.xz" -C "$tmp"
    mkdir -p out/toolchain
    rm -rf "$dest"
    mv "$tmp/ccache-$version-linux-x86_64" "$dest"
    chmod +x "$dest/ccache"
fi
mkdir -p out/toolchain/bin
cat > out/toolchain/bin/cc <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec "\${BB_CCACHE_BIN:-$root/out/toolchain/ccache/ccache}" \\
    "\${BB_REAL_CC:-/usr/bin/cc}" "\$@"
EOF
cat > out/toolchain/bin/c++ <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec "\${BB_CCACHE_BIN:-$root/out/toolchain/ccache/ccache}" \\
    "\${BB_REAL_CXX:-/usr/bin/c++}" "\$@"
EOF
chmod +x out/toolchain/bin/cc out/toolchain/bin/c++
"$dest/ccache" --version | head -2
echo "cache: $root/out/cache/ccache"
