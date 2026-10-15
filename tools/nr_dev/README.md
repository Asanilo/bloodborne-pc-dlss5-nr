# Isolated development entry

Build: `bash native/tools/nr_dev/build.sh`. Outputs live under `nrbridge-dev/build/native`; incremental rebuilds reuse that one directory. The source dependency copies under inputs are private and made once. SDK/sysroot versions remain the stable locked versions. No old build ancestor or execution ledger is imported.

Helper: `python3 native/tools/nr_dev/prepare_helper.py` exports the f527 quoted-include closure and compiles the wire CPU checker and PE. Only includes are relocated. The effective spec is verified against `671c7644c69de5f681b099fb4c3d402ba36f1e4538e65cd5538441e2bd47643b`. Export provenance is in `inputs/helper-f527/manifest.json`. Subsequent cold rebuilds use `prepare_helper.py --rebuild` with only exported source and locked SDK/compiler, without reading historical build ancestry.

The coordinator must first inspect current host GPU holders and driver-hang evidence, then record `inputs/host-safety.json` with current Linux `boot_id`, `unresolved_driver_hang: false`, and `evidence` referring to that real observation. This is a safety observation, not a historical approval or a launch count allowance. Runtime dependency hashes are checked using `inputs/runtime-pins.json`.

From the development root:

```
python3 native/tools/nr_dev/runner.py preflight
python3 native/tools/nr_dev/runner.py start dev-UNIQUE --mode skipEvaluate --sky off
python3 native/tools/nr_dev/runner.py scene-ready dev-UNIQUE
python3 native/tools/nr_dev/runner.py start-helper dev-UNIQUE
python3 native/tools/nr_dev/runner.py arm dev-UNIQUE
python3 native/tools/nr_dev/runner.py stop dev-UNIQUE
```

`scene-ready` records an actual coordinator/player observation of a controllable private scene; it must not be issued speculatively. `arm` checks the exact session's real Helper GPU/FD retired readiness. The same runner supports resident NR after lifecycle validation; `--sky persistent` requires the new Native implementation. GPU runs are coordinator-owned and globally flock-exclusive.

Each session has independent save copy, profile, cache, Wine prefix, generated guest, provenance and controls. Game assets remain read-only inputs; no formal installation or real save is used. Native retains tuple/UUID/PID/starttime/FD/ownership/last-use checks. Normal close is sent to a window only after its X11 PID matches the exact private game identity. Timeout creates HOLD and requests normal close, retains the GPU lock while the process remains alive, and never kills to claim completion. New-context HOLDs block subsequent sessions until independently resolved with appended evidence by the coordinator. The runner never modifies old FAIL/HOLD/STOPPED records.

`present-metrics.json` measures actual unique nonreuse swapchain Present CPU intervals (FPS/P50/P95/P99/max); it is not pixel quality acceptance. `gpu-memory.jsonl` records PID-associated NVIDIA memory samples. Review the actual terminal wire/FD audits and player quality in addition to the runner's normal-teardown result. A runtime failure remains HOLD, not BUILD_ONLY success.
