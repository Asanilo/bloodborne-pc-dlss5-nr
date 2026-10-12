# P0 native SR/publication timing candidate

Baseline: `64c4e06bba09b536d614ece60a10aa75f0291f9c` / 062913
`libbbgpu8d73`. The baseline source/artifact copy is preserved at
`bbport/runs/p2-pre-sr-timing-20261012T082950Z/manifest.json`.

This candidate only adds local timing: normal Quality HDR (1280x720 to
1920x1080), both P0 NR modes and srOnly. The SR interval brackets the native
`Dlss::Evaluate` recording call, after feature creation. ALL_COMMANDS
start/end include the emitted commands/barriers and their dependencies;
they exclude feature creation, motion generation, sharpening, NR, merge,
Present and the rest of game GPU work. It is not pure SR kernel time.

Four atomic query slots hold an eight-query pool. Existing Scheduler GPU
completion callbacks prove the recorded timeline tick before reading or
reusing a slot. The callbacks share-own the pool through its last GPU use,
and capture no raw Upscaler/Scheduler pointer. They use NO_WAIT + 64-bit
availability results (32B), mask the delta to timestampValidBits, and report
NOT_MEASURED for unsupported/busy/not-ready/error samples. This adds no
renderer Flush/wait, pixel/validator readback, shader change or new thread.
The existing pending-op worker's timeline wait is not a new renderer stall.

`P0_SR_GPU_TIMING` is keyed by actual source_game_frame and tick. Overlay
shows the latest completed source explicitly, not a claimed current-frame
or full-game time. Shared helper clocks are not synchronized by this patch.

Native per-frame records preserve input_published_cpu_monotonic_ns as its
legacy BEFORE-AtomicWrite point. The added input_publish_completed point
is taken after successful atomic rename returns. Local publication cost and
publication-to-complete-observation are separate; the latter still includes
Helper GPU/CPU work, publication, wakeups and polling, not just IPC.
Complete observation and selection-ready remain native steady-clock points.
Selection-ready means validated/consumed before caller selection/merge,
including skip fallback, not swapchain Present or photon time.

BUILD_ONLY: p0-build-20261012T084059Z PASS, 79 live/snapshot/pre/post source
pins and four artifacts match. CPU131 PASS (32 added checks): slot completion
proof, NOT_READY/error/availability/unsupported handling, 8/64-bit wrap,
invalid period, publication order and UI stale-source rejection. These are
pure arithmetic/state checks, not Vulkan execution. No compiler warning;
the two existing CMake policy warnings remain. The initial missing-type
include compilation failure is preserved in p0-build-20261012T083342Z.

Actual SR query regression and performance effect are NOT_RUN/UNMEASURED.
Helper, protocol, model parameters, source-Present acceptance, ownership,
reset, finite wait deadlines, 500ms soft acceptance and fallback/quarantine
are unchanged. No GPU/Wine/game run accompanies this candidate.
