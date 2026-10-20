# bbport NRBridge Preview — Known issues

This is a private preview. The package's build metadata remains **BUILD_ONLY**
(the builder does not claim a GPU or online acceptance); that field describes
the packaging artifact, not the independent external runtime acceptance. The
limited acceptance below does not make this preview "stable".

## Current limited acceptance (new boot `7fb96dc9…`)

- Same-byte `c91bf94…` Native + `1eddac56…` Helper: a controlled real-NR
  session reached scene-ready and closed normally at **611.12 s**.
- Session accounting: **4765 finalized / 4696 captured / 69 obsolete /
  0 skipped**; Feature create/release each **1**; result
  `PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`; wait **0**, holders empty,
  kernel PASS, bootguard retired.
- After about **121 s** resident on the title, the scene recovered; the same
  Helper/session resumed processing new NR jobs.
- Scope: this is limited NR / title-recovery / normal-exit evidence only.
  Multi-hour runs and additional scenes were not exercised and are not
  acceptance.

## Historical HOLD (old boot `54bbe…`)

- The old continuous-service run with **8066 tasks** remains a recorded HOLD:
  job 8066 `CompleteVerified` was not consumed, the Helper exited after the
  60 s timeout, Native entered an isolated HOLD, and Native last-use was
  unknown.
- That HOLD is retained as historical evidence. It is **not retroactively
  marked resolved** by the new boot's normal exit, and it must not be used to
  deny the new boot's limited evidence either.

## Save reload / continuation

- An independent r2 process automatically continued and loaded the private
  progress into **Clinic / "300 echoes"**.
- Base save write and independent-process reload passed: at copy time the
  source save and its manifest inventory matched; after launch `userdata0010`
  updated normally. This does **not** claim every byte stays unchanged after
  the new boot.
- The SR-only reload round passed `PASS_NORMAL_TEARDOWN`; no-peer real
  last-use PASS, holders empty, kernel PASS.

## Rendering

- **着色闪 / colour flicker**: still observed in the private real-NR path.
  The `valid0` user's F8 SR path did **not** flicker in that observation; do not
  extrapolate that to "flicker fixed" or to other SR/history paths.
- **亮尾 / bright tail**: not fixed. Do not report it as resolved.

## Online

- No account login or real online session was performed in this acceptance.
  Real 谏言/messages and online/THD/BBSC remain unverified; do not mark them as
  passed.

## Packaging

- The old `6753999` / `7f69` package is `--check`-only: it has no
  `NRBRIDGE_NATIVE_DAILY_RING64_V2` marker, so `--daily` is blocked.
- The exact packaged Native/Helper identity is shown by the launcher and
  `nrbridge-bundle.json`. Helper `1eddac56` uses protocol wire 1 / interface revision 4.
- `BUILD_ONLY` in the bundle/build report is packaging metadata. External
  real-NR acceptance is recorded separately and is not implied by that field.
- The AppImage mount only lives while the AppRun process lives. The GTK launcher
  now tracks terminal/Online child processes and refuses to close while one is
  running; forcibly killing the GUI still unmounts the image.
