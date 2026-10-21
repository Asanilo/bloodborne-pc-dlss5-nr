# bbport NRBridge Preview — Known issues

This is a private preview. The package's build metadata remains **BUILD_ONLY**
(the builder does not claim a GPU or online acceptance); that field describes
the packaging artifact, not the independent external runtime acceptance. The
limited acceptance below does not make this preview "stable".

## Current limited acceptance (new test context)

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

## Historical HOLD (previous context)

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

## Full launcher and player saves

- The original Settings / Online / Log pages are retained, with an NRBridge
  section for NR, native mouse, checked local dependencies and artifact identity.
  NR activation requires confirming that the character is controllable. With the
  in-game-start capable Native, press F8 in a controllable scene; the launcher button
  remains available. Later F8 presses toggle the effect while Helper keeps running.
  The compact HUD shows FPS/status; full counters are in Delete's diagnostics.
- The selected INI (including bindings), language, Mods/patches and save directory
  are passed to the managed runner. Incompatible NR settings are rejected with an
  explanation; turn NR off to use the ordinary upscaler settings.
- Native mouse and the built-in BBSC module can coexist. BBSC's cinematic camera
  uses F6, takes keyboard/mouse input only while active, and cancels pending native
  motion at handoff. F1 remains mouse release. Actual multiplayer remains unverified.
- Player saves run from a locked private copy. Only proven normal teardown can
  sync progress back, with the original launcher's backups and restore lock.
  An abnormal exit or save-sync error keeps the private progress for recovery.
- FSR 4.1.1 DLL conversion is unavailable in this Preview: its separate GPU
  workflow is not admitted by the NRBridge lifecycle. Prepared resources can be
  selected with NR off; this does not claim FSR GPU acceptance.
- The installed `packages/` layout is discovered only with the existing INI and
  launcher-settings markers. Explicit environment paths take precedence.

## Packaging

- The old `6753999` / `7f69` package is `--check`-only: it has no
  `NRBRIDGE_NATIVE_DAILY_RING64_V2` marker, so `--daily` is blocked.
- The exact packaged Native/Helper identity is shown by the launcher and
  `nrbridge-bundle.json`. Helper `1eddac56` uses protocol wire 1 / interface revision 4.
- `BUILD_ONLY` in the bundle/build report is packaging metadata. External
  real-NR acceptance is recorded separately and is not implied by that field.
- The AppImage mount only lives while the AppRun process lives. The GTK launcher
  retains its managed game child and refuses to close until it exits normally;
  a HOLD also blocks GUI close. Forcibly killing the GUI still unmounts the image.
