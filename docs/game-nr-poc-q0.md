> Historical note: local paths and machine identifiers are redacted for publication.
> Original evidence is preserved in private development data; this is not a new acceptance run.
> Current player instructions are in the project README.

# Q0 normal live DLSS Quality capture (build-only)

Source baseline is C4 polling milestone `527b827077a411cc9d8442ddfa9a772136cca064`. Root canonical is `integration/QUALITY-CAPTURE.md`. No Quality NR protocol is implemented. Stable repository is unchanged.

Tools: `tools/nr_probe/build_q0.sh`; Root-only launch interface `tools/nr_probe/run_game_q0.sh SESSION` in the isolated worktree. Case `bbport/runs/q0-game-SESSION`, private marker `profile/.q0-profile`; Root prepares a verified private NRTest seed and owns GPU lock/watchdog/input. Launcher sets DLSS preset1/output1080/jitter1/object-motion1/sharpen0/uncap+limit90, clears render-resolution overrides and old NR client modes. It starts no Wine helper or model. Default-off opt-in is `BB_NR_Q0_CAPTURE=1`.

Normal unset-render/1080 route predicts `!Scaled()` and reduced SceneTargets1280x720 in HDR `Run()`. `RecordDlss(...hdr=true)` produces output1920x1080 before original post/tonemap/UI. Q0 checks and records actual route/proxy extent rather than forcing RunScaled or reusing old C0 profile_match. The old explicit startup-patched RunScaled LDR route is blocked for Q0.

`SceneTargets::CaptureCreateInfo` inspects the actual owned proxy by image identity without creating/resolving it. Metadata separately labels guest allocations, actual SR color/depth proxy handles/views/createinfo/usage/layout/validrect, motion and SR output. C0 definitions remain intact. Capture-enabled proxy and motion creation include TRANSFER_SRC; Q0 checks the four source usages before recording copies. The nonessential object attachment is not dumped; its actual motion-pass validity and bound view are logged alongside projection/reprojection/current and previous camera matrices. No guide resampling, Depth inversion, extra MV jitter subtraction, or SR algorithm change occurs.

Only Root creates the private dump trigger after an actual 3D scene is visible. Two frames per trigger; Q0 indices remain unique over later stationary/motion trigger sequences. Local raw data is audit evidence only. Existing Scheduler deferred callbacks log recorded_tick, actual known completed tick, IsFree, bytes written and file success, without adding Flush or blocking waits. Present captures are requested separately through existing UI-inclusive present trigger; a source_game_frame0 on that generic dump means no explicit SR-source association, not a fabricated same-frame proof.

Build `bbport/runs/q0-build-20261011T151426Z/build-result.json` is PASS/exit0. Forty-six source hashes are equal across pre/post/live/snapshot; four live artifacts match. libbbgpu SHA `a9eb6faf3d6d4e68a794328e51172f3cccc5e93af6327df6e75c006952b66b61`. Old outputs are retained in pre-q0-out and prior milestone archives. Small delivery index is `bbport/q0-build-delivery.json`; original logs, command provenance and all sources remain in the build directory.

GPU/game/Quality resource capture/NR are NOT_RUN at delivery. Nonzero jitter correctness and guide alignment are UNVERIFIED_FOR_NR; unknown exposure remains unknown. Runtime capture PASS requires legal completed copies, actual nonzero jitter and real scene evidence plus bounded cleanup. Subsequent noop, NR, phase, matched Quality SR-only/NR120/1800 and quality/performance gates are separate. A and Root independently review this frozen build before Root serial execution.

## Actual isolated capture and diagnostic evidence

The initial delivery above was BUILD_ONLY. Integration subsequently ran the same capture implementation and the separately pinned optional wait diagnostics. The final executable candidate is `bbport/runs/q0-ordered-phase-build-20261011T161535Z/build-result.json`: PASS/exit0, 53 compiled/build inputs equal across live/snapshot/pre/post and four build artifacts matched. `libbbgpu.so` SHA256 is `7e1a9af5232bff00b7cc51f593d557033eb5df2019815bca4de1eca917b613ec`. This documentation and the offline auditor are not compiled inputs; no build or GPU rerun accompanied this evidence update.

Resource capture now has actual successful normal Quality evidence:

- `runs/quality-capture-20261011T163025.070733Z/game.log`, case `bbport/runs/q0-game-q0-quality-repeat-20261011`: actual static sources8358/8359. The attempted later motion input arrived after the watchdog had ended the game and was not consumed. Only two resource captures are accepted for this run.
- `runs/quality-capture-20261011T163641.147928Z/game.log`, case `bbport/runs/q0-game-q0-quality-motion-20261011`: actual static sources12241/12242 and camera-motion sources12332/12333. Each frame has four legally completed GPU readbacks, matched bytes/SHA and finite values. The current/previous view matrices differ for the motion pair and frame12333's previous view equals frame12332's current view. Integration viewed the Clinic/HUD and camera rotation. The outer controlled stop returned0 with empty own process group/private-prefix inventory.

All six captures use `!Scaled()/Run()/reduced`, actual input Color97 and Depth130 proxies1280x720 (not guest1920x1080 allocations), actual MV83 at1280x720, and successful HDR SR Color97 at1920x1080. Actual usages are respectively `0x8001f`, `0x27`, `0xd`, `0xd`, all legal TRANSFER_SRC/GENERAL. Actual current and previous viewport/SR jitter are nonzero and logged; capture source_reset is0, not a forged fresh-history reset. Input/output alpha is entirely0; synthetic .75 is inapplicable. This establishes Quality resource capture, not NR guide alignment, NR Evaluate, image quality, or performance. Live exposure remains unknown.

Prior failures remain evidence, not repaired behavior:

- Unfocused startup remained near1FPS. The ordered-phase run `runs/quality-capture-20261011T161716.440691Z` sampled an active ASYNC_SUBMIT waiting for the static global submit_mutex, owned by the Presenter inside PRESENT_CALL. The three total deferred-signal waits were approximately1992/999/1991ms. A snapshot locates the lock chain; it does not time a complete individual driver call or prove compositor internals. The next same-artifact run `162137.046255Z` associated a uniquely new Bloodborne/appbb-probe window with the game's creation (not compositor PID equality), then Integration focused that window once: startup progressed from~1FPS through14.8 to sustained90FPS statistics. No lock, notify, rendering route, or desktop configuration was modified. This is a startup window-state comparison, not real-scene performance.
- `runs/quality-capture-20261011T162436.494348Z/game.log`: after Continue, top-level PM4 submission50616 asserted on dword0x217 at offset0x204 in size0x80c; exit23, no scene capture/SR Evaluate/NR helper, empty cleanup inventory. The default submission-copy path retains a SubmittedCopy until decoding completes; the reported memory type-1/prot0 for a host address is not proof of a guest unmap. Actual copy-entry bytes and original guest address were not logged, so corruption/producer/cursor root cause is unproven. The two subsequent real-scene runs above did not reproduce the assertion. No assert bypass, PM4 instrumentation, copy-policy change, or notification fix was made.

## Offline camera comparison, not guide-phase acceptance

`tools/nr_probe/audit_q0_camera.py` reads only the four motion-case resource sets and their same-frame Q0_CAMERA/Dump-camera logs. Reproduce from the isolated worktree with:

```sh
python3 tools/nr_probe/audit_q0_camera.py \
  --poc ${HOME}/Code/nrbridge-design/game-poc-20261011 \
  --case ${HOME}/Code/nrbridge-design/game-poc-20261011/bbport/runs/q0-game-q0-quality-motion-20261011 \
  --log ${HOME}/Code/nrbridge-design/game-poc-20261011/runs/quality-capture-20261011T163641.147928Z/game.log
```

The small checked-in result is `docs/q0-camera-audit.json`. It verifies every raw-file hash/size/completion and uses the actual printed R3x4, projection, depth and current jitter to reconstruct the existing camera shader's previous-minus-current render-pixel prediction. It fixes a geometric mask before examining residuals: forward finite depth, positive previous z, two-pixel border exclusion, local3x3 view-z spread≤2%, and, for the adjacent f1/f3 pairs, predicted previous-raster depth consistency≤2%. It never selects pixels by MV agreement. All frames have object_valid1 but no per-pixel object selector attachment; this is a geometric screen, not an independent camera-only/object mask.

The predeclared component budget is half of the larger neighboring RG16F ULP at the predicted component plus0.01 render pixel. The added numeric allowance is conservative diagnostic policy, not a proven bound on GPU FMA/printed-matrix errors. No budget or mask was adjusted to obtain a result.

Motion f2/f3 have818302/802552 geometrically screened pixels. Correct camera prediction has median maximum-component residual0.002497/0.002272 pixels, with94.56%/94.51% within budget. Wrong sign yields14.808/15.269-pixel median residual and essentially zero budget matches. Adding either sign of another jitter-delta correction yields~0.333/~0.556-pixel median residual and zero budget matches. Static near-zero prediction cannot distinguish motion sign; extra jitter is still distinguishable. The remaining motion residuals, including multi-pixel outliers, stay unclassified: dynamic object motion, geometry, selector behavior and numerical details were not separately measured. No all-pixel MV PASS or NR phase PASS is assigned.

The result supports the existing render-pixel sign/de-jitter interpretation on the diagnostic sample. It does not establish SR output sampling phase, low-resolution-guide-to-model interpretation or NR_MVecScale. SR's actual MVScale1 remains a separate SR fact. Integration owns the next Quality contract; this milestone adds no guide resampling, model invocation or Quality renderer changes.
