# NRBridge Preview — 2026-10-20 post-reboot acceptance

Native runtime source: **c91bf9467604c36d70d97dd200844ae1ac8e8428**.
Helper source713f8ac9 / PE1eddac56e4de26c0, forwarder9a825 unchanged.
Packaging fix5ea553e does not change the Native/Helper runtime. Cold build410
and tools246 passed previously; packaging regression28/28 includes actual ELF
source resolution. Export wire1/interface4; daily bootstrap wire2/ring64.

On boot **7fb96dc9-a8cd-4e24-803e-d92f2d009997**:

- `preview-appimage-c91-layoutfix-20261020` ran611.12s from scene-ready,
  including121s on the title, then resumed NR jobs with the same Helper/session.
- 4765 finalized,4696 captured,69 obsolete,0 skipped; Feature create/release1/1.
  `PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`, Native actual final use proved,
  wait0, no residual project holders, kernel PASS, boot receipt retired.
- Four private save files changed content. Independent process
  `preview-appimage-r2-reload-20261020` automatically continued the normally
  retired session and visibly loaded Clinic/300 echoes. Its SR-only normal exit,
  actual no-peer final use, no residuals and kernel checks passed.
- Default native mouse was actually installed; the guest implementation is
  unchanged from the player-approved version. Joystick fallback remains.

The initial AppImage exposed two packaging errors: missing fresh-home `runs/`
and a duplicate `libbbgpu.so` under the generic closure. Strict mapping checks
correctly refused controls; Helper never started. That window exited normally.
5ea553e creates canonical runs and excludes project-owned libraries from the
closure, preserving their pinned Native paths. The existing home was migrated
with the duplicate bytes and old configs/pins preserved; the imported private
seed was not replaced. Real fresh-home setup/check/print-command passed.

Normal home: `~/.local/share/bbport/nrbridge-home`; no development worktree is
needed to launch. Enter the actual scene, then press Enter in the launcher
terminal to prepare NR. There is no fixed480/600s session shutdown; per-job,
GPU and normal-close grace checks remain. Model, game, GE and host NGX remain
external. The package contains no private saves, account, NR model or SDK.

Public online endpoints were reachable. The startup UI is open for the player's
own Discord login; actual login/messages/bloodstains/phantoms and online shutdown
remain **NOT TESTED**. BBSC stays optional, with mouse fallback; no multiplayer
acceptance claim. Account state and tokens are not printed.

Known limits: NR shading flicker/occasional valid0 and bright tails remain;
SR-only did not flicker in the player's comparison. No multi-hour or broad-scene
stability claim; controller/DLC/multiplayer coverage remains limited. This private
CachyOS closure includes GLIBC2.44 requirements; older distributions are untested.
Bundle `BUILD_ONLY` is builder metadata, separate from the external runtime proof.

The old boot54bbe... run finalized8066 jobs but remains **HOLD / Native final use
UNKNOWN**. No retirement was fabricated; its records remain unchanged. Reboot
established a new context and did not retire the old resources retroactively.
Stable5635804/7f69/9a825, finite verified6753999 and runtime-s4 remain unchanged.
No master merge, NVIDIA model update, FG or rendering-quality rework occurred.

Evidence: repository `out/preview-20261020/postreboot-acceptance-summary.json`,
independent NR/reload retirement JSON, freshhome-r2 identity/logs, screenshots,
package-layout-migration and the original runs. AppImage identity and shortest
launch instructions are in repository-root `dist/`.
