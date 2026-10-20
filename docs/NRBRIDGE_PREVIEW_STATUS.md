# NRBridge Preview — 2026-10-20

Native build: c91bf9467604c36d70d97dd200844ae1ac8e8428 (complete cold build,
410 targets + build.sh CPU checks; GPU NOT RUN). Helper source713f8ac9,
PE1eddac56e4de26c0/b372b1f1e3eb5215447941db0c0fbfccc41c921fec5edf354b248549f9e014e4;
forwarder9a825 unchanged. Export interface4, daily wire2/ring64. Stable
5635804/7f69/9a825 and finite verified6753999 remain untouched.

Delivered private candidate: repository-root dist/
Bloodborne-NRBridge-Preview-20261020-c91bf94.AppImage, SHA256
c2952c0780647849fa11e7a1ebc4e9441994597f3a05eff3e15ca2fb47bd1030.
It is BUILD_ONLY pending reboot and actual exit/reload acceptance, not an accepted
player baseline. Normal data home is ~/.local/share/bbport/nrbridge-home;
AppImage does not need a development worktree. Game, model, GE and host NGX remain
external. No game/save/account/model is bundled. Current private build targets
this CachyOS host; collected dependencies include GLIBC2.44 requirements.

Actual daily run preview-daily-nr-66b4a1a-20261020 finalized8066 real NR jobs,
ran beyond15minutes with bounded transport/logs and wrote four private save files.
On return to title, job8066 completed HelperGPU but did not receive Native
CONSUMED. It timed out60s, then Native quarantined exporter liveness; ordinary
close happened101s after completion. Native final use remains UNKNOWN. No kill,
normal-retirement claim or subsequent GPU launch occurred. Current boot
54bbe091-3e4f-4fdf-abf3-4505c72a7f71 is held globally across data homes.

83ba268 fixes the concrete missing no-SR-frame drain using existing actual
acquire/release/fence proofs, with rendering ended before external barriers.
It retains permanent profile-change stop and quarantine. Actual function CPU
cases passed; complete tools246/246; native mouse8 normal+8 ASan/UBSan; packaging23.
C91 cold build includes this fix and per-UID boot admission. Reboot is required
before another GPU test; do not synthesize CONSUMED or clear old HOLD.

Private written-save snapshot and failure evidence are in repository
out/preview-20261020/hold-snapshot-66b4a1a. Fifteen save files were independently
copied into the normal home's selected seed, with fresh pins. This transfers
files only: no GPU, protocol, visual history, account or NR runtime state. It
neither marks the source normally retired nor proves reload. Empty-seed config
backup is retained. AppImage static --check passed after import.

Player reported NR shading flicker/occasional valid0; SR-only display did not
flicker, and the valid0 event later stopped reproducing. No model/Shader/quality
change was made. Both pools' public endpoints answered unauthenticated checks;
actual login/messages/bloodstains/phantoms and network shutdown remain NOT TESTED.
BBSC is optional and forces mouse fallback; no multiplayer acceptance claim.

Next after reboot: verify new boot/kernel/no project holders, open this exact
AppImage, load imported private progress, activate NR in the scene, test
scene→title→scene and normal close, then independently audit actual final GPU/FD
retirement and restart/load. User must perform Discord login before actual
community-message testing. Preserve old failure records and stable selections.
