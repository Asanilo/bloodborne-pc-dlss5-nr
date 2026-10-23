# NR execution extent contract v3 (M5)

This optional daily profile keeps the historical wire 1/2 profiles unchanged.
`BB_NR_DIMENSIONS_V3=1` selects wire 3. The versioned profile identity binds color,
depth, motion, NR model and resolved-output extents, plus the final SR output.
In the historical revision-1 direct profile, render, guide, execution and resolved-output
sizes were identical; `working_scale=1`, MV values remain previous-minus-current render
pixels with scale 1/1. Revision 2 below is the active variable-render contract. Source jitter is recorded; model jitter correction remains
unverified and no proprietary jitter setter is claimed.

**Normative correction:** the old frozen `effective-spec.json` COMPLETE prose says
Encode1080/downsample720. The frozen production code actually does Encode720 →
Evaluate720 → Resolve720. Under v3 each of these stages uses the negotiated NR
execution extent; no 1080 encode or downsample stage is implied. The old JSON and
its import SHA are retained for stable-profile identity and provenance.

Every session creates a feature and one externally owned image set of its exact
execution extent. A mismatched image or profile is rejected before use. No old
history/descriptor set can be treated as belonging to a different extent. Rebuilds
must freeze jobs, drain exact completions/CONSUMED and actual Native last-use,
release imports/FDs, complete STOP_READY/NATIVE_RELEASED/Helper shutdown and reap
before establishing a new extent epoch. Unknown completion remains HOLD.

The initial 8 GiB policy bounds model working pixels to 1920×1080 and reports
an image working-set estimate separately from actual NGX/model/driver allocation.
Output dimensions and NR execution dimensions are different budget domains.
Capability markers reject old Native/Helper builds rather than silently running
raw SR. SR presets are independent of NR Preset A/B. New-dimension FG is not
admitted until its owner adapts the fixed mailbox and validates it.

## Revision 2: independent SR render domain

The `NRBRIDGE_DIMENSIONS_V3R2` binary marker and `sr-variable-r2` profile identify
an INPUT `source_layout` with actual render-subrect and SR output sizes. The
immutable CONFIG/SHARED execution extents still match exactly. Old v3 peers
cannot be used with this revision. Live SR preset/output/aspect changes keep
this resource set and reset the visual history; a different NR execution size
requires a normally retired session followed by a new session.

Native records NR before the game's HDR-to-display conversion. Color and depth
are resampled from the actual scene proxy to the bounded execution domain;
motion vectors are converted once from render pixels to execution pixels.
NGX MVecScale remains 1/1. Only the NR HDR residual is transferred back to the
actual scene, preserving render-resolution detail and alpha. A two-pass finite
and F16-range guard leaves the entire actual HDR frame unchanged on failure.
The normal game tone conversion, Native SR and HUD path follow this prepass.

Source viewport jitter metadata is expressed in execution pixels; actual SR
jitter stays in render pixels. This records domains, not evidence of NR jitter
correction: model phase remains UNVERIFIED. Layout changes set source_reset and
invalidate history/candidates through the existing epoch gates. Replacing the
render-size motion image waits on the actual Native scheduler; the fixed
adapter images are retained with the Native shared-resource owner on HOLD.

The 8 GiB policy caps default NR execution at 1080p pixels, output at 4K pixels.
This is a conservative image budget, not a measured promise that every allowed
combination fits every device. An explicit `--nr-execution` selects a smaller
working set without changing the SR render/output preset. WorkingScale itself
remains 1.0 inside the NR feature; this is a separate Native input adapter.


Revision 2 requires live host target scaling (`BB_LIVE_RES=1`); the startup game
resolution patch is rejected. Its aligned render dimensions and restart-only
selection are a different interface. Native binds the NR presentation ledger
when the actual scaled display image is selected by Presenter, using the real
GPU frame/prepare tick. This consumes the same association rule as the HDR
1080p path without changing FG/Presenter implementation. Hot association does
not trigger whole-frame readback; diagnostic captures remain explicit.
