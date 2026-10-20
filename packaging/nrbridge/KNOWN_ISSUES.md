# bbport NRBridge Preview — Known issues

This is a private preview. The following are observed/known and must not be
reported as passing acceptance:

## Rendering

- **着色闪 / colour flicker**: still observed in the private real-NR path.
  The `valid0` user's F8 SR path did **not** flicker in that observation; do not
  extrapolate that to "flicker fixed" or to other SR/history paths.

## Exit / retirement (await fix)

- A real private NR session ran **>15 min / 8066 tasks** with the continuous
  service. After switching back to the title/menu, job **8066
  CompleteVerified was not CONSUMED**; the Helper exited after the 60 s timeout
  and Native entered an isolated **HOLD**. Native last-use is **unknown**.
- The development root has stopped all new GPU work until reboot and a controlled test of the
  menu-drain fix (83ba268, actual-function CPU tests passed). Do not run GPU, force-kill, or claim a
  normal exit/retirement for this case. Re-test only with the new fixed Native
  identity and the controlled procedure.

## Online

- Both public pools were reachable at the network level, but no account login or
  real online session was performed. Do not mark online/THD/BBSC as passed.

## Packaging

- The old `6753999` / `7f69` package is `--check`-only: it has no
  `NRBRIDGE_NATIVE_DAILY_RING64_V2` marker, so `--daily` is blocked.
- The exact packaged Native/Helper identity is shown by the launcher and
  `nrbridge-bundle.json`. Helper `1eddac56` uses interface revision 4 and
  daily wire v2. The menu-drain correction has CPU coverage; normal GPU exit
  and private save reload still require acceptance after reboot.
- The AppImage mount only lives while the AppRun process lives. The GTK launcher
  now tracks terminal/Online child processes and refuses to close while one is
  running; forcibly killing the GUI still unmounts the image.
