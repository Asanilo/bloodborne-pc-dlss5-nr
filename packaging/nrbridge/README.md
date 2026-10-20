# NRBridge AppImage Preview packaging

The builder reuses the upstream 0.6 package shape and pruning rules. It stages a
verified Native build, independent Helper/forwarder and the full GTK launcher;
collects the actual ELF dependency closure; removes development RUNPATHs; and
keeps host drivers/glibc outside the image.

Game assets, saves, credentials, proprietary NR model/SDK and Wine/GE are not
bundled. Model/GE/NGX/game paths are local selections validated at runtime.
The current candidate is a private Preview, not a generic public Linux release.

## Explicit build inputs

From the Native source checkout, with prepared artifacts/dependencies:

```bash
bash packaging/nrbridge/appimage.sh \
  --native-build /path/to/build/baselines/FULL_NATIVE_COMMIT \
  --helper /path/to/helper-build/wine_runtime_model.exe \
  --forwarder /path/to/forwarder-build/nvngx.dll_dlssnr.dll \
  --online-deps /path/to/online-deps/root/usr/lib \
  --sysroot-lib /path/to/sysroot/usr/lib
```

Use `--skip-appimage` for AppDir/static checks only. Downloaded tooling stays in
`out/tools`, not the system. The Native/Helper/forwarder inputs are read-only.
Default build roots derive from `NR_DEV_ROOT` / `NRBRIDGE_HOME` and the checkout;
online dependencies require `--online-deps` or `BB_ONLINE_DEPS_ROOT`.
No personal or dated machine-local checkout is assumed.

The reference private Preview used Native c91bf94, Helper PE 1eddac56 (source 713f8ac9)
and forwarder 9a825. Identities are read from the **selected manifests and SHA**,
not accepted merely because a directory has a familiar name. New sources need a
new build identity; selecting the reference binary does not rebuild it.

## Package and runtime behavior

- Full Settings / Online / Log pages, Chinese resources, fonts, bindings,
  Mods/patches and save management remain; NRBridge adds managed runtime selection.
- Generic library closure excludes project-owned GPU/net/DLSS libraries, which stay
  in the pinned Native tree. Static verification rejects development RUNPATHs and
  unresolved/wrong project-library sources.
- Native provenance, Helper/forwarder manifests, protocol export versions and every
  staged artifact SHA are recorded. Wire 1/interface 4 plus the Native daily marker
  are required for daily mode; a marker is capability, not GPU acceptance.
- The installed `packages/` layout is detected only with existing INI and launcher
  settings markers. Explicit environment paths take precedence. Standalone files
  use their selected `NRBRIDGE_HOME` and full-launcher configuration.
- GUI and `--play` delegate to the same managed coordinator, with scene confirmation.
  There is no fixed daily session shutdown; job/retirement checks remain. The mount
  stays owned while its managed child runs, and HOLD blocks unsafe GUI close.
- Selected saves run under the original save locks/backups as private copies and
  sync only after normal teardown. Diagnostic continuation/empty seeds are separate
  from the player's selected save folder.
- Online pool, account settings path and BBSC choice come from the full Online page;
  credentials are not copied into the bundle or printed. Real login/online content
  remains unverified. FSR 4.1.1 DLL conversion is explicitly disabled in this Preview.

## Verification and limits

Builder metadata remains `BUILD_ONLY`: it describes the packaging operation, not
independent GPU/online acceptance. Read [Preview status](../../docs/NRBRIDGE_PREVIEW_STATUS.md)
and [Known issues](KNOWN_ISSUES.md) for limited actual runtime evidence.

The reference bytes passed a session beyond 600 seconds, title recovery, normal
Native/Helper GPU/FD/model retirement and private save reload. The full-launcher
AppImage also passed private SR and NR/save-sync checks. That does not establish
multi-hour endurance, full-game coverage, real DLC/online or all controller modes.
NR flicker/valid-history drops/bright trails remain. Current closure has some
GLIBC 2.44 requirements; older distributions and the Nix bundler are unverified.

Keep accepted package hashes and old installed runtimes until exact recovery is
proved. Reproducible staging directories are caches, not a substitute for unique
source, private saves or failure evidence. See [workspace guide](../../docs/WORKSPACE.md).
