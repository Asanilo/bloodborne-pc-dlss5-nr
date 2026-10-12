# Q2 controlled fixture interface — v1

Canonical preregistered plan: `fixture-spec.json`. Generator emits **one case per JSON**, plus an asset-index listing six cases. CLI is `wine_q2_motion_fixture.exe RUN_DIR_WINDOWS CASE_JSON_WINDOWS`. This is an offline synthetic model behavior test, no Q1 wire, DMA-BUF, SR, game, or game phase identification. Root is the only runtime executor (at most six PE fixture starts, no retry).

Case top-level fields (generated JSON is the exact example):

- `schema="q2-known-translation-v1"`, `case_id` one of the six plan IDs.
- `model_extent=[1920,1080]`, `guide_extent=[1280,720]` or `[1920,1080]`; `phase="raw"|"corrected"`; `nr_mvec_scale=[1.0,1.0]` or `[1.5,1.5]`.
- `model_parameters`: the plan's exact object. Production encode/resolve uses WorkingScale1, scene-linear/manualWP1, Passthrough=false, strengths1/transfer1/maxRatio2. Evaluate width/height1920/1080, guide subrect and resource wrappers use actual guide extent; Evaluate's final MV scale args come only from `nr_mvec_scale`. Frame ms 1000/60 and exposure/preExposure1 are canonical host fixture declarations/request echo only; the fixed NR API does not receive them and its runtime defaults remain UNVERIFIED. No NR jitter setter or local MV extra multiplier.
- `budgets_ms`: GPU10000/cold120000/outer300000.
- `branches`: exactly ordered primary, repeat_primary, zero_mv, reverse_mv, reset_second. Each `{branch_id,fresh_feature:true,frames:[F0,F1]}`. Each frame has `{frame:0|1,reset:0|1,source:asset,depth:asset,mv:asset}`. Resets1/0 except reset_second1/1. Model/core/device may stay alive; feature must be released after its last completed GPU use and a new feature actually created/fenced for EACH branch. Never just reset and claim independent features.
- Each asset is `{path:absolute Linux path,windows_path:"Z:"+path,sha256:64lowerhex,bytes:uint,format:109|100|103,extent:[width,height],channels:4|1|2,dtype:"<f4"}`. Load only the frame source/depth/mv assets, never truth/masks. Tight little-endian row-major bytes. Root validates SHA/bytes before PE launch. Helper must fail missing/size/extent/type; upload those exact bytes then GPU copy/readback bytecompare against the loaded bytes before Evaluate. Record actual per-asset GPU readback, not just file SHA. SHA implementation inside helper unnecessary when Root pin gate + actual upload equality is evidenced.
- `translation_output_pixels=[12,-6]`, `jitter_render_reference_pixels` as plan, `known_color_output_phase=[0,0]` describe generated physical inputs; **do not send a nonexistent NR jitter parameter**. `phase` selects assets only. Color/truth/noise identical in all six cases. Depth physical scene identical; guide extent/coordinate sampling differ as explicit experimental variables. MV raw units are G/O * (-12,+6) on moving slats, zero background. Raw guides sample at output position minus J720*O/720, corrected at output position; this controlled correction does NOT determine game k_output.
- `oracle={truth_frames:[asset,asset],masks:{name:asset},criteria:object}` and `plan_sha256`, `asset_input_scope` are native audit metadata only. Oracle assets never supplied to model.

Required `RUN_DIR/run-result.json` (success is execution/functional scope, NOT selected semantics):

```
{
 "schema":"q2-known-translation-v1", "case_id":"...", "status":"PASS",
 "model_extent":[1920,1080], "guide_extent":[...], "phase":"...", "nr_mvec_scale":[...],
 "model_parameters":{exact canonical request echo only},
 "parameter_application":{
   "frame_ms":"HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API",
   "exposure":"HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API",
   "pre_exposure":"HOST_FIXTURE_DECLARATION_NOT_SENT_TO_NR_API",
   "runtime_default_values":"UNVERIFIED"
 },
 "device_uuid":"32lowerhex", "driver_uuid":"32lowerhex",
 "feature_create_calls":5, "feature_release_calls":5, "evaluate_calls":10, "resolve_calls":10,
 "normal_shutdown":true,
 "branches":[{
   "branch_id":"...", "feature_ordinal":1, "fresh_feature_created":true,
   "creation_GPU_complete":true, "last_GPU_complete_before_release":true, "feature_released":true,
   "frames":[{
      "frame":0, "reset":1, "evaluate_calls":1, "evaluate_result":1, "resolve_calls":1,
      "completed_GPU":true, "upload_readback_equal":true,
      "source_readback":"primary/frame-0-source.rgba32f",
      "depth_readback":"primary/frame-0-depth.r32f", "mv_readback":"primary/frame-0-mv.rg32f",
      "raw_readback":"primary/frame-0-raw.rgba16f", "output_readback":"primary/frame-0-output.rgba32f"
   }, {"frame":1,"...":"same required keys"}]
 }]
}
```

The exact four-key `parameter_application` object above is REQUIRED at the result top level and in EACH of the ten frames; missing, extra, differently typed or differently valued entries fail closed. It is also bound into cross-case provenance alongside actual device/driver UUID, requested model parameters, plan/oracle hashes and Color/truth input SHA. `model_parameters` is never evidence that all its declarations reached the model API.

Each frame separately requires `evaluate_parameters` recording actual width/height, guide_width/guide_height, depth_inverted, reset, intensity, style, local_structure, local_tone, skin_structure, use_auto_mask, mvec_scale_x/y, enabled and jitter_setter="NONE", plus the actual four resource role/format/extents and subrects. It MUST NOT contain frame_ms/exposure/pre_exposure as actual scalar arguments. Source/model extents remain1920x1080; guide resource/subrect extents match that case's720 or1080 guides; MV scale is the case's candidate exactly once. Auditor validates this record independently of request echo. The application recording amendment does not alter spec, generated cases/assets, physical scene, masks, candidates or oracle thresholds.

Every readback path must be a regular local file within unique RUN_DIR; no peer pixels IPC. Use actual emitted filenames in this result (auditor does not infer filenames). All five branches and both frames required. Record actual feature create/result, GPU creation fence, Evaluate args/result, resolve calls, prepoison raw NaN/output-1234 perframe, and GPU completion/release/shutdown evidence in raw log. No normal release on unknown in-flight failures. Explicit API failure exit2; missing/runtime ability/permission/watchdog exit3. Keep raw failure and partial result. Raw RGB must replace NaN with finite values, resolved RGBA finite/non-sentinel and original source alpha exact. CPU oracle rechecks bytes independently; helpers may add timing/actual args fields, not remove required fields.

Behavior oracle (frozen plan): repeated reference RGB RMS supplies noise bound `max(1e-4,5*repeatRMS)`. Every zero/reverse/reset control must differ above that bound before calling behavior MV/history sensitive. At least2% MSE advantage over all three controls AND RMSE gap above bound in all/foreground-interior/foreground-boundary masks for utility. Matched scale/phase pair selection additionally requires the same advantage in both fixed tile-parity domains, and winning candidate sensitive/useful. Failure to distinguish is NOT_IDENTIFIED, not a false technical FAIL. No thresholds/masks may be changed after outputs.
