#!/usr/bin/env bash
# Fetch the two isolated build tools the preview needs.  Nothing is installed
# into the system: appimagetool is unpacked under out/tools and patchelf lives
# in a private virtualenv.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
tools=out/tools
mkdir -p "$tools"
if [[ ! -x $tools/appimagetool-x86_64.AppImage ]]; then
    curl -fL --retry 3 -o "$tools/appimagetool-x86_64.AppImage" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$tools/appimagetool-x86_64.AppImage"
fi
if [[ ! -x $tools/runtime-x86_64 ]]; then
    curl -fL --retry 3 -o "$tools/runtime-x86_64" \
        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
    chmod +x "$tools/runtime-x86_64"
fi
if [[ ! -x $tools/patchelf-venv/bin/patchelf ]]; then
    python3 -m venv "$tools/patchelf-venv"
    "$tools/patchelf-venv/bin/pip" install --quiet patchelf
fi
"$tools/patchelf-venv/bin/patchelf" --version
"$tools/appimagetool-x86_64.AppImage" --appimage-extract-and-run --version
