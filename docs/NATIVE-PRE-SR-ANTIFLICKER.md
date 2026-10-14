# Native SR-input/history experiments — not an antiflicker PASS

Independent branch/worktree fromacd7f340, Helper f527/profile/wire/math/Intensity1/
age4 unchanged. Two exact Native-only local selectors, no Helper keys:
BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY=domain_reset|game_only (defaultdomain_reset);
BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT=switching|fixed (defaultswitching).
Four combinations executable using run_game_native_async_pre_sr_antiflicker.sh
SESSION resident|skipEvaluate. Root alone prepares approval/private seed/GPU lock,
records same scene and chooses each single variable independently. No new runner.

`game_only` only removes cache-domain extra SRreset; original predecision game,
SR feature recreation/resource/cut/history requests all remain. Fixed input resource
entry/exit adds a separately logged protected resource reset in both history modes;
it is not every raw/cache transition. NRReset1 remains independent. Defaults restore
old switching + ORdomain behavior, no fixed image allocation. Native flags off leaves
old renderer/SR/P0 behavior intact; old WT/artifacts are not modified.

`fixed` allocates one native-only persistent R16G16B16A16/97 image1280x720 after actual
Armed + supported route. Current originalraw or already validated guarded-cache97
is copied bitwise into it; exact source CI/TRANSFER_SRC and current source are checked.
Original immutable snapshot is always taken before this copy from actual game raw;
the fixed resource is never captured recursively or exposed to Helper. One additional
image packed7372800B (existing468305988B ->475678788B total packed), no origin packets,
cache banks or shared slots added. Actual VMA/VRAM/model footprint UNKNOWN.

Same Native Scheduler/queue only: previous SR read -> next copy write -> next SRread
is GPU ordered by ALL_COMMANDS/MEMORY_READ|WRITE toTRANSFER_WRITE andTRANSFER_WRITE
back toALL_COMMANDS/MEMORY_READ barriers. This is a recorded GPU dependency, not a
CPU completion claim; no wait/Flush added. Track copy, exact SR binding/read attempt
and actual merge targets; actual target callbacks retain the resource and publish
observed completion. Normal stop waits for Ready (sameNative observed>=lastuse) then
API destroys fixed resource BEFORE publishing native quiescence; unknown remains
stickyheld. NoPeer cannot allocate because fixed requires Armed/actual importsReady;
its absent-resource proof also explicitly checks no fixed object. N0 is not GPUproof.

Existing ActualCurrentPresent/INPUT_DOMAIN/SR_READ remain; local SR_EXPERIMENT reports
history/input choices, original reset, optionaldomainextra, protectedfixedtransition,
appliedreset and actualinputimage. FIXED_SR_READ reports realcopy/SRread/lastuse targets.
These are not photon timings. Host selected cache still does not prove modifiedpixels.
Hotonce0 zero fullreadback. Explicit experiment modes may reuse existing resident-only
720once primitive: original-current97 vs EXACT actual SR-bound97 (fixed or switching),
guard4B/copy+SR+merge/managedwriter/uniquePresentjoin; never use109/origin substitute.
Diagnostic cost excluded from performance and Root approval required for any GPU.

Current evidence: acd7 resident20s219 realupdates,67.744FPS/cache27.675% and normal
retirement PASS; user reports flicker, dynamic quality FAIL. Root window430 domain
transitions had no actual game reset; correlation is not proven cause. No threshold,
strength, age, jitter, model or shader change in this experiment. CPU/build only;
runtime/antiflicker benefit/phase/DepthInverted0 remain UNVERIFIED/UNMEASURED.

Fixed API destruction has a separate Native-only atomic proof: callbacks may still
hold a shared owner just after completed tick publication. The actual destructor
frees view+image before setting fixed_input_absent_or_destroyed; owner awaits this
AND original cache quiescence before existing NATIVE_RELEASED. No wire fields or
Helper changes. Zero snapshot count does not substitute for fixed actual GPU use.
CPU first -Werror parser indentation failure preserved; only formatting corrected.

C source mapping (one pass): bbport/runs/pre-sr-antiflicker-C-source-mapping/analysis.json.
INPUT is hardpacket0/chain-origin; new Mode2 Capture resets shared historybanks.
No safe small wait removal identified. Conversion-only/pending-suffix API needs exact
actualslot/lease and shown-guide contract before integration; no age/capacity extension.
Initial134514 fullbuild PASS preserved; final binding/retirement trace increment only.
