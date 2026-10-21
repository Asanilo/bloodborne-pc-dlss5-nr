# NRBridge Preview: implementation and validation

This is a sanitized summary. Raw logs, machine identifiers, private paths and
saves stay in ignored development data. No historical failure is retroactively
converted into a pass.

## Last GPU-exercised reference runtime

| Component | Identity |
|---|---|
| Native | a037043fc989e46683958366186bae057b7b0630 |
| Helper source | 713f8ac99959fcdf470b2600c2645a1e5b24b7c8 |
| Helper PE build | 1eddac56e4de26c0 |
| Forwarder build | 9a8251430ccfa68a |
| Protocol | wire 1 / interface 4; daily bootstrap wire 2 / ring 64 |

The full-launcher implementation is a9e2275 + fd389ae. The F8/HUD update is
cbbe474 + c031a37: the latter removes the extra confirmation dialog. An initial
cold build was followed by a focused incremental rebuild of that simplification;
both passed the Native build's CPU tests. Later documentation commits are not
another Native build.

## Launcher translations and slow-walk restoration

The subsequent installed Native is 025503b53ecf66d69ba08e4dff93737b84f06e56.
Its only runtime input change from a037043 restores the missing slow-walk default:
hold Left Alt with WASD for the existing reduced stick magnitude. Left Ctrl remains
L3. The launcher exposes the slow-walk binding again, and explicit old bindings or
intentional unbindings still take precedence. The 0.6 merge had retained the walk
implementation while dropping its default and launcher row.

The launcher now translates the missing online, Discord, log and current control
labels into Chinese, including the table-driven options. Game/service names,
technical identifiers and canonical key names remain recognizable. Regression
checks cover the translation catalog and formatting placeholders. The actual
packaged full launcher was instantiated with private offline settings: translated
pages, account controls, log buttons, Left Alt walk and Ctrl L3 were checked.
No account sign-in or game launch was invoked by that widget check.

The update passed the 8 native-input CPU cases normally and under ASan/UBSan,
the Native build's CPU tests, and 211 Python checks (28 optional cases skipped).
The CLI-only private-image fixture is exercised separately by the input test
runner, not imported through unittest discovery. The input checks exercise
sustained slow-walk, release to full movement, Ctrl/L3 separation, custom keys,
and unaffected independent gamepad axes. This focused incremental build and GUI
check do not constitute a new GPU/gameplay acceptance; the a037043 GPU evidence
below remains specific to that earlier binary. The installed previous runtime
and package are preserved; formal save/config file hashes were unchanged.

## BBSC and native mouse coexistence

The a037043 update removes a local blanket refusal when the built-in BBSC module
is enabled. Review of its fixed patch/write ranges found no overlap with the
native camera detour, protected camera region or constants. Unknown modules and
actual alternate camera-hook modes retain their existing admission checks; this
is a reviewed built-in contract, not a live detector of arbitrary future writes.

BBSC cinematic camera uses F6 in bbport, leaving F1 for mouse release. Entering
or leaving cinematic mode cancels pending native input, advances its epoch and
clears held input. Normal capture resumes through the existing focus/menu/capture
intent, flushing queued motion and settling before publishing a fresh capture.
The guest detour and register protection, NR, Helper protocol and GPU lifecycle
are unchanged.

The packaged a037043 runtime loaded a private save with BBSC and the native camera
hook both actually installed. In-game F6 on/off was exercised before and during NR;
F8 started NR without a terminal or dialog. It finished 343 jobs (336 captured /
7 obsolete / 0 skip), passed normal Native/Helper GPU, FD and model retirement,
and synced its private save. Independent post-exit checks found no project holders
or new kernel GPU fault; 17 formal save/config files were unchanged. IP sockets
were denied and a dummy account fixture was used: actual login, multiplayer and
physical mouse feel with BBSC remain unverified. This run used the AppImage's
built-in extract-and-run path because the test's no-new-privileges filter prevents
FUSE mounting.

CPU checks passed: 8 native-mouse cases both normally and with ASan/UBSan,
4 compiled cinematic variants using both source copies and port/upstream hotkeys,
production capture-resumption tests, native admission/module loading, 291 developer
checks, 57 frontend/packaging checks, and the Native build's CPU tests. This was a
focused incremental build; earlier cold-build evidence is not a new cold build.

## In-game activation and compact HUD

The exact c031a37 AppImage loaded a private save while its coordinator had no
terminal input. In a controllable scene, one F8 press enabled NR without a dialog
or window switch. Two later F8 presses actually disabled and restored the effect,
as observed in the HUD and control revisions. Helper remained running throughout.
The session finished 2023 jobs (1888 captured / 135 obsolete / 0 skip), passed actual
Native/Helper GPU, FD and model retirement, and synced its private progress.
Post-exit checks found no project holders or new kernel GPU fault. Player saves,
INI and account settings were unchanged. This is limited scene acceptance, not
multi-hour endurance or an independent full-game assessment.

The HUD now shows one line of FPS/NR status; full counters remain in Delete's
diagnostics. A bounded, atomic same-session intent file connects F8 to the existing
identity and Helper-readiness checks. It grants no GPU proof. SR-only sessions and
older Native binaries do not receive this capability; the launcher/CLI confirmation
path remains available. Effect bypass does not release or restart Helper.

CPU regression passed 291 development checks, 57 frontend/packaging checks and
8 upstream-launcher checks during this update, including compiled publication
fault injection. The final simplification also passed its targeted CPU checks.
The installed candidate is identified by its artifact SHA in private deployment
metadata; older accepted c91bf94 and 5635804 runtimes remain rollback references.

## Implemented and exercised

- Upstream 0.6 integration preserves the gameplay work, composite bindings and NR.
- One native camera hook, mailbox/epoch consumption, cancellation and state
  protection; the player confirmed better mouse feel, stopping, F1 and menu recovery.
- Full Settings / Online / Log launcher, Chinese UI, selected INI/dependency
  readback and managed launch; NR conflicts reject explicitly.
- Private-copy save transactions use the original launcher's locks/backups and
  sync back only after actual normal teardown. Copy/swap/external-change faults
  retain recoverable progress rather than overwriting the source.
- The earlier c91bf94 Preview AppImage loaded a private save through the full GUI in SR-only
  mode, exited normally and synced it. An independent NR process reloaded the
  synced progress, processed 572 jobs (569 captured / 3 obsolete / 0 skip), and passed actual
  Native/Helper GPU, FD and model retirement; no project holders remained.
- That c91bf94 runtime passed 611 seconds after scene readiness,
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
