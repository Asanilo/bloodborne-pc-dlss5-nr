# Q2 two-frame controlled NR behavior fixture

All additions are standalone CPU tools/assets. Q1 sources, artifacts, contract and archives stay frozen. Integration alone launches at most six Wine fixture processes under the existing GPU lock/private-prefix/own-group watchdog; B runs no GPU or Wine. A supplies the new independently built fixture. This does not substitute for a 120-frame gate.

The preregistered `fixture-spec.json` fixes the mathematical scene, noise, cases, history, masks and thresholds; `CASE-SCHEMA.md` fixes the exact helper interface. Two 1080 HDR Color frames are **identical assets across all cases**. A known `(12,-6)` output-pixel foreground translation crosses stationary background at nonconstant Depth/MV slat boundaries. The clean truth is generated independently of NR. Guides are 720 or1080 samples of that same physical scene; raw 720 raster has known current jitter, corrected guides remove that generated shift. None of this identifies actual game's k_output.

Each case primary/repeat/zero/reverse/reset_second branch creates and releases an actual fresh feature, with creation/last-use fences and actual reset1/0 (reset control1/1). Model/device may be reused. Public feature recreation is isolation evidence; undocumented model-global history cannot be proved absent. Repeat branch provides an empirical two-sample noise bound, **not a statistical confidence interval**. Identical uploaded source/depth/mv readbacks, actual Evaluate/resolve/fences, raw poison replacement, output finite/sentinel/alpha and normal release are mandatory functional gates.

Five same-mask controls prevent non-sensitive/bypass output, uncontrolled noise or mere hash changes being called a semantic victory. Color MSE vs analytic truth and repeat RMS are reported independently. Selection requires all preregistered negative-control, 2% MSE margin and repeat-bound checks, including fixed spatial splits. Remaining ambiguity is NOT_IDENTIFIED. Report MSE relative to input too: utility relative to negative controls is not a general quality certification. No parameter fitting or residual-selected masks.

CPU asset generation (new output only):

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/nr_probe/q2_controlled/generate.py --out /absolute/new/q2-assets
PYTHONDONTWRITEBYTECODE=1 python3 tools/nr_probe/q2_controlled/build_only.py --assets /absolute/new/q2-assets --out /absolute/new/build-evidence
```

The generated asset-index lists six individual case JSONs; source/guide local files are intentional canonical fixture **inputs**, uploaded by the same Wine process. They do not prove Linux↔Wine GPU sharing. Clean truth/masks are for CPU oracle only, not model input. Existing generated v1: `bbport/runs/q2-controlled-assets-v1`,34 assets364,492,800 bytes, six manifests ~26KB each.

After Integration supplies real run directories:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/nr_probe/q2_controlled/audit.py --case /absolute/assets/ref1080-s1.json --run /absolute/new/run > /absolute/new/audit.json
PYTHONDONTWRITEBYTECODE=1 python3 tools/nr_probe/q2_controlled/audit.py --compare /absolute/run-audit1.json /absolute/run-audit2.json ... > /absolute/new/comparison.json
```

Per-case audit checks exactly five branches/ten frames. Full source/depth/mv independent GPU copies must byte-match each immutable input; model raw RGB and resolved RGBA checked over all pixels. Pair compare distinguishes scale1/1.5 within each extent/phase, and raw/corrected phase within each720scale. It never converts physical pixel factor1.5 into a claim about NR's opaque internal scale normalization. Game NR phase and MVecScale remain UNVERIFIED unless separate game-source evidence exists.
