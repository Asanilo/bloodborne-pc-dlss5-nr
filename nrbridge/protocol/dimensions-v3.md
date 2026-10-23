# NR execution extent contract v3 (M5)

This optional daily profile keeps the historical wire 1/2 profiles unchanged.
`BB_NR_DIMENSIONS_V3=1` selects wire 3. The versioned profile identity binds color,
depth, motion, NR model and resolved-output extents, plus the final SR output.
For the initial direct profile, render, guide, execution and resolved-output sizes
are identical; `working_scale=1`, MV values remain previous-minus-current render
pixels with scale 1/1. Source jitter is recorded; model jitter correction remains
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
