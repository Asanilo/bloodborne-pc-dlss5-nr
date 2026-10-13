# Once-only actual-current edit diagnostic

Opt-in: BB_NR_NATIVE_ASYNC_EDIT_DIAGNOSTIC_ONCE=1, only resident.
Launcher remains tools/nr_probe/run_game_native_async.sh SESSION MODE; Root owns
approval, GPU lock, fresh private seed, scene arm and bounded launches. Default
is zero readback. The independent localnoop diagnostic is unchanged.

The diagnostic is recorded after the final sparse completion/catchup view is
selected, not at the old localnoop readback site. The actual selected view must
resolve to the same current packet's regular or replay output image. Its source
is that current packet's immutable converted unedited SR, never origin Color.
A verified Captured origin establishes diagnostic identity; a later application
must have the same epoch/history/params, age 0..4 and job 0..3. Invalid, skipped,
obsolete, unsupported and SR fallback paths do not consume the one-shot latch.
This admission does not establish pixel validity or visual quality.

Two RGBA32F 1920x1080 staging copies (66,355,200 bytes) use the existing Native
open tick, barriers and priority completion callback. Actual merge binds its
own tick and GPU callback after merge recording. No extra Flush, renderer wait,
file polling, Helper wait, new wire or math change. Tick-owned images/leases and
staging remain alive until actual use completion; managed writer waits for both
proofs before CPU invalidate/write. No hot per-frame readback. Diagnostic runs
are excluded from performance. Once per session, even across chain restart.

Files: private RUN/native-sky-edit-once/source.rgba32f, composite.rgba32f,
readback.json. Metadata binds origin/current/age/epoch/history/params/job,
resource set, Native input/copy/merge tick and Helper serial as source identifier.
Join present_join to the unique nonreuse NATIVE_ASYNC_CURRENT_PRESENT for the
same session/current source/merge tick/sparse job and effect_applied=1. Copy or
merge callbacks alone are not Present proof. Separate Presenter timeline ticks
and Helper fence serials are never compared numerically to Native copy ticks.
Metadata pixels_and_quality=NOT_AUDITED and phase=UNVERIFIED remain until Root
independently checks actual bytes and Present; effect selection is not PASS.

NATIVE_ORIGIN_INPUT_PUBLISHED records Native steady nanoseconds immediately after
successful atomic INPUT publication. NATIVE_ORIGIN_COMPLETE_VERIFIED records
Native steady nanoseconds after strict COMPLETE verification and before Complete
phase publication. Both bind session/job/origin/input tick. Helper serial is only
an identity; no subtraction from Wine QPC. Existing post-Present Native monotonic
ledger can establish actual current-SR presentation during the model interval.
Failed writes/validation produce no successful-point event.

Baseline rollback: commit 9678bb163b2d025ae9331ad2d8691171652b7987,
111 sources and four artifacts validated before changes. Artifacts preserved at
bbport/runs/native-identity-baseline-9678bb1; original source/build snapshots at
bbport/runs/native-async-build-20261013T065438Z. No A/helper/math/protocol,
Scheduler, original P0, formal assets/save or Root approval/budget modifications.

Validation: production admission CPU checks plus complete isolated BUILD_ONLY.
GPU runtime, effect pixels, Present association and benefits NOT_RUN/UNMEASURED
for this increment. Earlier skip4-c identity/import PASS did not arm or publish
an origin; normal retirement remains UNPROVED and watchdog cleanup is retained.
