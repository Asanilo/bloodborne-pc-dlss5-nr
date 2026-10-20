# NRBridge Preview: implementation and validation

This is a sanitized summary. Raw logs, machine identifiers, private paths and
saves stay in ignored development data. No historical failure is retroactively
converted into a pass.

## Reference runtime

| Component | Identity |
|---|---|
| Native | c91bf9467604c36d70d97dd200844ae1ac8e8428 |
| Helper source | 713f8ac99959fcdf470b2600c2645a1e5b24b7c8 |
| Helper PE build | 1eddac56e4de26c0 |
| Forwarder build | 9a8251430ccfa68a |
| Protocol | wire 1 / interface 4; daily bootstrap wire 2 / ring 64 |

The full-launcher implementation is a9e2275 + fd389ae; 9afb192 recorded its
acceptance. Later documentation/portability source changes are **not another
Native build or GPU acceptance**. The installed private Preview is unchanged.

## Implemented and exercised

- Upstream 0.6 integration preserves the gameplay work, composite bindings and NR.
- One native camera hook, mailbox/epoch consumption, cancellation and state
  protection; the player confirmed better mouse feel, stopping, F1 and menu recovery.
- Full Settings / Online / Log launcher, Chinese UI, selected INI/dependency
  readback and managed launch; NR conflicts reject explicitly.
- Private-copy save transactions use the original launcher's locks/backups and
  sync back only after actual normal teardown. Copy/swap/external-change faults
  retain recoverable progress rather than overwriting the source.
- An exact Preview AppImage loaded a private save through the full GUI in SR-only
  mode, exited normally and synced it. An independent NR process reloaded the
  synced progress, processed 572 jobs (569 captured / 3 obsolete / 0 skip), and passed actual
  Native/Helper GPU, FD and model retirement; no project holders remained.
- The identical runtime previously passed 611 seconds after scene readiness,
  including 121 seconds on the title followed by scene recovery with the same Helper.
  This is one limited session beyond 600 seconds, not multi-hour endurance.
- At full-launcher integration,271 tools + 54 frontend/packaging + 8 upstream-launcher
  CPU checks passed. Subsequent source-only portability checks are separate.

## Available, not fully accepted

- THD/Seamless network modules build and register; the original login and optional
  BBSC controls remain. Actual account login, messages, bloodstains, phantoms,
  network shutdown and multiplayer were not exercised in these checks.
- DLC/content mount boundary tests passed on CPU; real DLC assets are unverified.
- Controllers, lock-on/free camera, more areas and extended play have partial coverage.
- Prepared alternative upscalers remain selectable with NR off. FSR 4.1.1 DLL
  conversion is explicitly disabled in NRBridge Preview because its separate GPU
  workflow is not admitted by the lifecycle guard.

## Known limits

NR shading flicker, occasional valid-history drops and bright trails remain.
Current private AppImage dependencies include GLIBC 2.44 requirements; compatibility
with older distributions is unverified. No FG or model update is included.

A historical long-session failure still has unknown Native final use and remains
HOLD in its original evidence. Reboot starts a fresh context; it does not prove
retirement of the old one. Never clear a current-boot HOLD or fabricate completion
just to resume play.

The earlier5635804/7f69/9a825 rollback reference remains preserved in private data.
The installed package and previous runtime can be selected only after normal
retirement and with no current unsafe context. Paths and exact installed-package
SHA records are private deployment metadata, not public launch instructions.
