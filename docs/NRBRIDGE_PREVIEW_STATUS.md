# Full launcher / daily installation — 2026-10-21

Implementation commits: a9e2275 (settings/save transaction), fd389ae (full UI/package).
Runtime remains Native **c91bf9467604c36d70d97dd200844ae1ac8e8428**,
Helper source713f8ac9 / PE1eddac56e4de26c0, forwarder9a825; no new Native build.

- Original Settings / Online / Log, Chinese UI, bindings, Mods, patches and save
  management remain in the full launcher. NR/native mouse default on; joystick
  fallback and explicit scene confirmation remain. Settings/dependency edits are
  read back and forwarded to the managed runner; incompatible NR choices reject.
- Saves use the original launcher locks/backups, a per-run private copy and sync
  only after actual normal retirement. Copy/backup/swap faults and external save
  changes preserve private progress. Runtime game selection preserves old pins
  and seeds, including inventories larger than128KiB.
- CPU:271 tools,54 NRBridge frontend/packaging,8 original-launcher tests PASS.
  Astra confirmed managed normal-close/mount lifetime and the FSR conversion
  guard. FSR4.1.1 DLL conversion stays explicitly unavailable in Preview; prepared
  resources with NR off remain selectable, without a new FSR acceptance claim.
- Exact r4 AppImage full Chinese GUI / SR-only loaded private Clinic300 echoes,
  installed native camera and exited normally with private save sync. Independent
  r4 NR run reloaded that synced copy, displayed actual NR history, finalized572
  jobs (569captured/3obsolete/0skip), Feature create/release1/1, and passed
  PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP. No residual project holders,
  current-boot kernel PASS and global receipt retired. Formal saves unchanged.
- Installed package: `/home/arinp/bloodborne/packages/Bloodborne-bbport-x86_64.AppImage`,
  SHA256 `2ed34e9881bdfa081431c9022bb384c221795a139135f809b72ea54c94a483c1`.
  `runtime/current` -> `releases/20261020T160000Z-nrbridge-c91bf94`;
  `runtime/previous` -> old6ad9496e release. Old package renamed
  `Bloodborne-bbport-x86_64.pre-nrbridge-20261021.AppImage`.
  Old runtime retained **in place**, without duplicate backup:37of56 files differ
  from/are absent in the old AppImage, so it cannot reconstruct that local release.
- Existing `launch.sh`, INI, launcher settings and15formal save files were unchanged
  at deployment. Installed entry --check passed and the full Chinese GUI visibly
  reads the existing game/save paths. New managed home is `data/nrbridge`; game,
  GE, NR model and NGX stay external. No history/workspace cleanup was performed.

Start `/home/arinp/bloodborne/launch.sh` or the installed AppImage. Click Start,
then confirm **进入游戏场景后开启 NR** once the actual scene is visible.
Online login/messages/bloodstains/phantoms remain untested; no automatic login.
NR flicker/bright tails and limited broad-scene/endurance/controller/DLC coverage
remain known issues. Earlier611s evidence remains applicable to identical runtime
bytes; this shorter UI integration check does not claim another long soak.

Evidence: Fork `out/launcher-integration-20261020/` (tests, screenshots, deployment,
old-runtime comparison) and normal-home `runs/nrbridge-20261020-235038-35f26d25`,
`runs/full-launcher-nr-20261020`. Source rollback uses normal revert; installed
rollback requires normal retirement/no current-boot HOLD, then restoring the old
package/current link from deployment.json. Old stable5635804/7f69/9a825 remains.

The following is the earlier runtime acceptance snapshot, retained as evidence.

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
