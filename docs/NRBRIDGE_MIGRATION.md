# NRBridge source migration and NR 310.9.16 experiment

## Frozen source and independent runtime

Migration commit: `2a634bf`. The source is imported from
[OptiScaler-Linux-NR](https://github.com/Asanilo/OptiScaler-Linux-NR) commit
`713f8ac99959fcdf470b2600c2645a1e5b24b7c8`, `nrbridge/` tree
`6cdc70ff18a31609eeba8721ce6d9d08c6665a9e`. Import reads Git objects at that
commit, not the changing upstream worktree. [migration.json](../nrbridge/migration.json)
records all 38 original files and SHA256 identities.

The closure includes the production forwarder, public NR shader constants/header,
production SPIR-V and its original header, historical HLSL, generator and recipe.
The build has no OptiScaler source-tree dependency. Existing LLVM MinGW,
Vulkan/NGX headers and JSON are configurable external inputs; no SDK, DLL, game,
account config or copied toolchain is committed.

Native and Helper remain independently built programs. GPU sharing, process/device
identity, protocol versions, creation fences, CONSUMED, actual Native GPU final
use, FD ownership and retirement are unchanged. Local runtime paths are explicit.
The accepted pre-migration Helper/forwarder/model and current/previous player
installations remain untouched. [License notes](../nrbridge/LICENSE-NOTES.md)
retain GPLv2/GPLv3 boundaries and the pre-existing Native protocol-header issue.

## Migration validation

The existing Native `out/` build (recorded source `d2216f6`) is reused, with build provenance for the managed
Runner. No full AppImage is repacked. Private runs use the existing project GPU
lock and global current-boot session guard; no HOLD or retirement proof is reset.

On RTX 4060 Laptop, 1920×1080 DLSS SR Quality, 1280×720 immutable NR origin:

| Run | Model | Evaluate / Resolve | Captured / obsolete | Shutdown |
|---|---|---:|---:|---|
| Migrated Helper | 310.8.2 stable | 195 / 195 | 184 / 11 | PASS_NORMAL_TEARDOWN |
| Migrated Helper | 310.9.16 SF Default | 307 / 307 | 304 / 3 | PASS_NORMAL_TEARDOWN |
| Preset Helper | 310.9.16 SF A | 473 / 473 | 472 / 1 | PASS_NORMAL_TEARDOWN |
| Preset Helper | 310.9.16 SF B | 338 / 338 | 337 / 1 | PASS_NORMAL_TEARDOWN |
| Same Preset Helper, old GPU baseline | 310.8.2 Default | 245 / 245 | 243 / 2 | PASS_NORMAL_TEARDOWN |

All five runs (1558 Evaluate/Resolve calls total) created and released one feature each, returned normal Helper shutdown,
proved actual Native GPU final use and reported no residual resources. These are
focused integration checks, not an all-scene visual acceptance or longevity test.
The final post-exit check found no NR holders, a retired global receipt and no
current-boot kernel GPU fault under the existing Runner check. Raw logs, sealed
configs, manifests and screenshots remain in ignored `out/nr-dev/`.

## Community DLL identity

Source: [NR 310.9.16 release](https://github.com/thx114/HoYoShadeHub-DLSSNR-Runtimes/releases/tag/nr-310.9.16).
The release describes SF as the general compatibility variant for RTX 40/50 and
unknown NVIDIA GPUs. GitHub release asset metadata matches the local ZIP:

| Input | Bytes | SHA256 |
|---|---:|---|
| `nvngx_dlssnr_310.9.16-SF.zip` | 192378839 | `0c973b9cd94f859551d045b81e38b9101bc1da54bc8f66f21e2554b0c16dfa36` |
| extracted `nvngx_dlssnr.dll` | 276162560 | `cdfa6c1ec784867182164c7836f324f087b5c9f374d0c9fe46f54999feabf3ed` |

PE version resource: file/product `310.9.16.0`. Its Authenticode certificate table
is absent (offset/size 0/0). This is a community supplied modified runtime, not an
authenticated original NVIDIA DLL. The [patcher project](https://github.com/dev-camo/dlssnr-patcher)
explains that patching invalidates the original signature. The inspected artifact
is not attributed to a particular patcher invocation without a reproduction
record. The old model and its sealed configuration are not overwritten.

New NGX initialization and Vulkan feature creation both returned `0x00000001`.
Actual Encode, Evaluate, Resolve, shared output capture and normal release passed
using the existing forwarder. A sampled new-model job measured Encode 0.167 ms,
Evaluate commands 7.235 ms and Resolve 0.181 ms; those are scoped GPU query
intervals, not pure inference timing or a performance average.

## Presets and running the experiment

Preset support commit: `950ccd7`. Version 2 local input configs select `nr_preset: "Default"`, `"A"` or `"B"`.
The selection is fixed for a session and survives style/intensity feature rebuilds.
The model hint values are 0/1/2, separate from DLSS SR Quality and NR style.
A configuration with an incompatible old Helper is rejected before GPU launch.
The Helper checks hint readback after the actual creation fence. Both A (1→1)
and B (2→2) passed readback, canary checks and actual shared output. In-game F8
started A, then disabled and restored its effect (control revisions 1→2→3). NGX does not
expose an internal weights identifier; parameter readback is not such a proof.

See [Helper build/runtime](../nrbridge/README.md) for dependency roots and schemas.
Seal private inputs with `tools/nr_dev/prepare_runtime_inputs.py`. Use the existing
scene-first play entry, which starts a private save copy and retains F8:

```bash
NRBRIDGE_HOME=/absolute/private-nr-development \
  bash tools/nr_dev/play-experiment.sh /absolute/private-nr-development/localinputs/nr310-A.json
```

Close normally before selecting B or the sealed old-model profile. The player
installation is not switched by this experiment. The protocol, shared buffers
and Native temporal logic do not change for a preset switch.

## Initial performance and visual scope

Recorded nonreuse swapchain Present windows with NR history selected gave 60.09
FPS / 23.07 ms p95 for the old model (846 frames, 14.06 s) and 58.22 FPS / 21.42 ms
p95 for the new Default (1431 frames, 24.56 s). Different windows and a brief
camera turn prevent a controlled performance ranking. A new-model device-total
VRAM sample was 4037 MiB of 8188 MiB; this includes driver and other applications,
not solely the Helper. Old-run peak VRAM was not retained, so no VRAM reduction
is claimed.

The Preset A run retained 277 actual GPU-query samples: median Encode 0.169 ms,
Evaluate commands 16.284 ms, Resolve 0.181 ms, total 16.674 ms. Preset B retained
311 samples: 0.168 / 9.111 / 0.189 / 9.511 ms respectively. Sampling reads existing
completion JSON without GPU readback or extra Vulkan synchronization. Samples
may be missed when the normal ring consumes files; they are not every job.
The old model was then sampled with the same final Helper and hint 0, without
changing the original rollback artifacts. Across 159 sampled jobs its median
Encode/Evaluate/Resolve/total was 0.179 / 15.896 / 0.191 / 16.316 ms. These scoped
command intervals include queue dependencies and are not pure inference timings.

| Private short sample | Evaluate commands median | Device-total VRAM snapshot |
|---|---:|---:|
| Old 310.8.2 Default | 15.896 ms | 4040 MiB |
| 310.9.16 A | 16.284 ms | 3941 MiB |
| 310.9.16 B | 9.111 ms | 3929 MiB |

Device-total VRAM includes the driver and other applications, and these are
snapshots rather than comparable per-process peaks. Additional Present windows
were A 54.39 FPS / 25.05 ms p95 (911 frames, 16.73 s) and B 59.62 FPS / 21.34 ms
p95 (366 frames, 6.12 s). Different camera positions, windows and thermal state
prevent a general performance ranking. B is the default in this machine's
ignored `out/nr-dev/play.sh` experiment; A and the old model remain selectable.

Initial observation covers the clinic's dark shelves, stone arches, candlelight
and the starting character's cloth. It does not cover the hunter's leather hat or
broad outdoor AO/color-bleed conditions. Screenshot inspection alone cannot
establish temporal stability, bright trails or flicker performance.

## Upstream relationship and publication

Continue reviewing OptiScaler-Linux-NR and Sky for scale mapping, Pre-SR,
DualFeature, parameters, new models, Encode/Resolve, history, masks and flicker.
The Sky integration was inspected at `48e4c5798b8e1e92a3a6de5110a5b433e2337e58`;
no newer shader or generic framework was imported in this migration.
Expanded quality/resolution/aspect matrices and the new memory model are deferred.

Repository topics were updated and read back successfully: `native-linux`, `vulkan`, `neural-rendering`, `dlss`,
`wine`, `gpu-interoperability`.

## Checks and local delivery

- Native incremental `bash scripts/dev-build.sh --test`: passed. The shared
  ccache required normal workspace write access; no dependencies were duplicated.
- Helper PE build: passed; host wire/protocol/config/ring64 checks, retirement
  tests, builder tests and SPIR-V validation passed.
- Forwarder: all 26 export names/ordinals matched the frozen interface.
- Developer tools: 292 Python checks passed. A local-HOLD test was isolated from
  the real per-UID global guard, so concurrent FG tests do not change its fixture.
  The production guard is unchanged.
- Launcher/packaging: 57 Python checks passed; shell syntax and local doc links passed.

The final exercised Helper build is `16bdc730da6029bd`, PE SHA256
`e3004ebcbeb7c47952ef395bd1de64d7a2aa1ff0bed868a171c7812d257c2931`;
forwarder build `78d663ed3116c1f5`, SHA256
`a632dee5a0f96f9684f2345408dbff7f21fceec86e82bda2167c46327926d70b`.
Build receipts retain exact source inputs and HEAD/diff separately from these GPU
results. No new immutable release or AppImage is created. Private experiment
instructions and A/B/old launch selections are in ignored `out/nr-dev/PLAY.md`.
Logical added workspace bytes were about 4.0 GB, mostly five isolated runtime
prefixes/caches, the one extracted community DLL and logs; source import is about
0.6 MB. Prepared dependencies, SDKs, compiler and player runtimes are reused or
retained in place. No cleanup of another agent's output was performed.
