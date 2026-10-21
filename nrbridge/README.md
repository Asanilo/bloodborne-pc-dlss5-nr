# Standalone NRBridge Helper

The Wine-hosted Vulkan/NGX Helper, forwarder, protocol, shaders and CPU tests are
maintained here. Native game code remains in `gpu/`. The Helper is compiled and
launched independently; it is never linked into the Native game.

The initial import is frozen at OptiScaler-Linux-NR
`713f8ac99959fcdf470b2600c2645a1e5b24b7c8`, `nrbridge/` tree
`6cdc70ff18a31609eeba8721ce6d9d08c6665a9e`. [migration.json](migration.json)
records every imported Git path and original SHA256, including the production
forwarder, shader public header, SPIR-V, historical HLSL and generation tools
outside the upstream `nrbridge/` tree. No live OptiScaler checkout is a build input.
Original authorship comments and license declarations are retained.

## Build

Select existing prepared inputs; do not copy SDKs or toolchains:

```bash
export NR_LLVM_ROOT=/absolute/llvm-mingw
export NR_VULKAN_INCLUDE=/absolute/vulkan/include
export NR_NGX_SDK_ROOT=/absolute/ngx/headers
export NR_JSON_ROOT=/absolute/nlohmann  # contains json.hpp
# Optional; defaults to this checkout's out/nr-dev
export NR_DEV_ROOT=/absolute/private-nr-development
python3 nrbridge/build.py --check-only
python3 nrbridge/build.py
python3 nrbridge/forwarder/build.py
```

Equivalent Helper options are `--llvm-root`, `--vulkan-include`, `--ngx-sdk-root`
and `--json-root`. The forwarder accepts `--llvm-root`. Header and compiler pins
retain the validated frozen closure. Build receipts record actual compiler,
transitive headers, linker libraries, source hashes and protocol exports.
Outputs live under `$NR_DEV_ROOT/build/helper-runtime-model/<digest>/` and
`$NR_DEV_ROOT/build/convergence/forwarder/<digest>/`. Existing outputs are verified
and reused; incomplete or changed artifacts are preserved and rejected.

The build runs CPU protocol, runtime config, ring64 transport and retirement tests,
validates production SPIR-V with `spirv-val`, then builds the PE with LLVM MinGW.
`llvm-readobj`, a host C++ compiler and Python 3.11+ are required. CPU/build success
does not establish GPU acceptance. Run Python tests with the same dependency
environment: `python3 -m unittest discover -s nrbridge/tests -p 'test_*.py'`.

The production SPIR-V remains SHA256 `7b44aeb1876f61c8c102979225edb10287189d1d8eddfa0a65186cf026c80239`.
The build regenerates an exact byte-array header. `shaders/regenerate.py` accepts
an explicitly pinned native Linux DXC to reproduce the historical HLSL; this
optional HLSL→SPIR-V rebuild has not been verified. Newer upstream shaders are
not substituted during migration.

## Runtime boundary

```text
Native Vulkan → shared GPU images → Wine Helper Encode/Evaluate/Resolve
              → persistent visual history → Native DLSS SR → display
```

Launch through the managed Runner/full launcher, which holds the existing project
GPU lock, pins dependencies, verifies process and device identity, and supervises
normal shutdown. Runtime resources must be selected explicitly:

```text
wine_runtime_model.exe MODE RUN_DIR_WINDOWS SESSION --runtime-config CONFIG_WINDOWS
```

The `nrbridge-helper-runtime-v1` JSON contains `schema`, `model`, `forwarder`,
`ngx_dir` and `ge_files`, with canonical absolute Linux paths. Wine maps them
through `Z:`. There are no personal default paths. The model/core/NVAPI/DXGI,
Wine and game files are user-provided, private dependencies, never public assets.

Protocol export remains versioned, with wire v1 / interface revision 4 and the
existing daily ring64 v2 bootstrap. Native consumes the recorded export in
`gpu/render`, not runtime implementation headers. The migration preserves
immutable 1280×720 origins, GPU sharing, sparse NR jobs, creation fences,
CONSUMED validation, actual Native final use, strict drain/rebuild ACKs and
shared-image/FD/model retirement. Unknown ownership and HOLD still stop reuse.

Keep the accepted old Helper, forwarder and model selected in a separate sealed
runtime config for rollback. Migration does not replace a player installation.
Future extraction may move this subtree and its public protocol; a generic SDK,
new memory model and expanded resolution matrix are outside this change.

See [Native build and runtime guide](../tools/nr_dev/README.md),
[license boundary](LICENSE-NOTES.md), and [Preview status](../docs/NRBRIDGE_PREVIEW_STATUS.md).
OptiScaler-Linux-NR and Sky remain references for scale mapping, Pre-SR,
DualFeature, model parameters, shaders and history. Import only needed code with
its exact source identity and compatible license.
