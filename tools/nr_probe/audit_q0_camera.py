#!/usr/bin/env python3
"""Offline Q0 camera diagnostic; no GPU, pixel IPC, or all-pixel phase acceptance.

Masks are geometric, fixed before residual inspection. Object-selected pixels remain
unclassified because Q0 captures only the composited MV, not its per-pixel selector.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

import numpy as np

COMPUTE_BUDGET_PIXELS = 0.01
LOCAL_Z_SPAN_RELATIVE = 0.02
PREVIOUS_Z_ERROR_RELATIVE = 0.02


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def named_camera(line):
    words = line.split()
    result = {}
    for key, count in (("view", 12), ("prev_view", 12), ("proj", 4),
                       ("prev_proj", 4), ("jitter", 2), ("prev_jitter", 2)):
        at = words.index(key) + 1
        result[key] = np.asarray(words[at:at + count], dtype=np.float32)
    return result


def local_span(z):
    # The outer two pixels are excluded separately; rolled borders are not samples.
    lo, hi = z.copy(), z.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            neighbor = np.roll(np.roll(z, dy, axis=0), dx, axis=1)
            lo, hi = np.minimum(lo, neighbor), np.maximum(hi, neighbor)
    return (hi - lo) / np.maximum(np.abs(z), np.float32(1e-6))


def summarize(prediction, actual, mask, budget):
    residual = np.abs(actual - prediction)[mask]
    if residual.size == 0:
        return {"samples": 0, "status": "NO_GEOMETRIC_SAMPLES"}
    bounded = np.all(residual <= budget[mask], axis=1)
    return {"samples": int(residual.shape[0]),
            "within_budget_pixels": int(bounded.sum()),
            "within_budget_fraction": float(bounded.mean()),
            "max_component_error_quantiles_pixels": {
                k: float(v) for k, v in zip(("p50", "p95", "p99", "max"),
                np.quantile(residual.max(axis=1), (.5, .95, .99, 1)))}}


def audit(poc, case, log):
    audit_path = case / "root-resource-audit.json"
    root = json.loads(audit_path.read_text())
    text = log.read_text(errors="replace")
    cameras, transforms, resources = {}, {}, {}
    for line in text.splitlines():
        if line.startswith("Dump camera: frame "):
            n = int(line.split()[3])
            cameras[n] = named_camera(line)
        elif line.startswith("Q0_CAMERA "):
            n = int(re.search(r"dump_frame=(\d+)", line)[1])
            r = re.search(r"reproject=([^ ]+)", line)[1].rstrip(",").split(",")
            transforms[n] = {"R": np.asarray(r, dtype=np.float32).reshape(3, 4),
                             "object_valid": int(re.search(r"object_valid=(\d+)", line)[1])}
    evidence, frame_results = [], []
    for entry in root["frames"]:
        cap = entry["capture"]
        n = int(cap["dump_frame"])
        width, height = map(int, cap["render"].split("x"))
        arrays = {}
        for role, item in entry["resources"].items():
            path = poc / item["path"]
            actual_hash = digest(path)
            if actual_hash != item["sha256"] or path.stat().st_size != item["bytes"]:
                raise ValueError(f"Evidence mismatch: {path}")
            channels = 1 if role == "depth" else 2 if role == "motion" else 4
            dims = cap["output"] if role == "output" else cap["render"]
            w, h = map(int, dims.split("x"))
            dtype = "<f4" if role == "depth" else "<f2"
            a = np.fromfile(path, dtype=dtype).reshape(h, w, channels).astype(np.float32)
            if not np.isfinite(a).all():
                raise ValueError(f"Nonfinite Q0 source: {path}")
            complete = item["completion"]
            if complete["GPU_complete"] != "1" or complete["file_ok"] != "1":
                raise ValueError(f"Incomplete Q0 source: {path}")
            evidence.append({"frame": n, "role": role, "path": str(path.relative_to(poc)),
                             "sha256": actual_hash, "bytes": path.stat().st_size,
                             "samples": int(a.size), "finite": int(np.isfinite(a).sum()),
                             "recorded_tick": complete["recorded_tick"],
                             "completed_tick": complete["completed_tick"]})
            arrays[role] = a
        resources[n] = (cap, arrays)
    for n in sorted(resources):
        cap, arrays = resources[n]
        w, h = map(int, cap["render"].split("x"))
        c, R = cameras[n], transforms[n]["R"]
        depth, motion = arrays["depth"][:, :, 0], arrays["motion"]
        size = np.asarray((w, h), dtype=np.float32)
        y, x = np.indices((h, w), dtype=np.float32)
        centers = np.stack((x + np.float32(.5), y + np.float32(.5)), axis=-1)
        uv = centers / size
        scene_uv = uv - c["jitter"] / size
        ndc = scene_uv * np.float32(2) - np.float32(1)
        sky = depth >= 1
        z = np.where(sky, np.float32(1), c["proj"][3] /
                     np.minimum(depth - c["proj"][2], np.float32(-1e-8)))
        view = np.stack((ndc[..., 0] * z / c["proj"][0],
                         ndc[..., 1] * z / c["proj"][1], z,
                         (~sky).astype(np.float32)), axis=-1)
        prev = np.stack([np.sum(view * row, axis=-1, dtype=np.float32) for row in R], axis=-1)
        prev_ndc = prev[..., :2] * c["prev_proj"][:2] / np.maximum(prev[..., 2:3], np.float32(1e-4))
        prev_uv = prev_ndc * np.float32(.5) + np.float32(.5)
        predicted = (prev_uv - scene_uv) * size
        jitter_delta = c["prev_jitter"] - c["jitter"]
        previous_raster_centers = centers + predicted + jitter_delta
        mask = ((depth > 0) & (depth < 1) & np.isfinite(predicted).all(axis=-1) &
                (prev[..., 2] > 1e-4) & (x >= 2) & (x < w - 2) & (y >= 2) & (y < h - 2) &
                (previous_raster_centers[..., 0] >= 2) & (previous_raster_centers[..., 0] < w - 2) &
                (previous_raster_centers[..., 1] >= 2) & (previous_raster_centers[..., 1] < h - 2) &
                (local_span(z) <= LOCAL_Z_SPAN_RELATIVE))
        prior_n = n - 1
        previous_depth_checked = False
        if prior_n in resources and int(resources[prior_n][0]["source_game_frame"]) + 1 == int(cap["source_game_frame"]):
            prior = resources[prior_n][1]["depth"][:, :, 0]
            prior_z = c["prev_proj"][3] / np.minimum(prior - c["prev_proj"][2], np.float32(-1e-8))
            ix = np.clip(np.floor(previous_raster_centers[..., 0]).astype(np.int64), 0, w - 1)
            iy = np.clip(np.floor(previous_raster_centers[..., 1]).astype(np.int64), 0, h - 1)
            sampled_z = prior_z[iy, ix]
            mask &= ((prior[iy, ix] > 0) & (prior[iy, ix] < 1) &
                     (np.abs(sampled_z - prev[..., 2]) / np.maximum(np.abs(prev[..., 2]), 1e-6) <= PREVIOUS_Z_ERROR_RELATIVE) &
                     (local_span(prior_z)[iy, ix] <= LOCAL_Z_SPAN_RELATIVE))
            previous_depth_checked = True
        rounded = predicted.astype(np.float16)
        up = np.nextafter(rounded, np.float16(np.inf)).astype(np.float32)
        down = np.nextafter(rounded, np.float16(-np.inf)).astype(np.float32)
        half_ulp = np.maximum(np.abs(up - rounded.astype(np.float32)),
                              np.abs(rounded.astype(np.float32) - down)) * np.float32(.5)
        budget = half_ulp + np.float32(COMPUTE_BUDGET_PIXELS)
        variants = {"camera_previous_minus_current": predicted,
                    "wrong_sign": -predicted,
                    "extra_previous_minus_current_jitter": predicted + jitter_delta,
                    "extra_current_minus_previous_jitter": predicted - jitter_delta}
        frame_results.append({"dump_frame": n, "source_game_frame": int(cap["source_game_frame"]),
            "camera_view_changed": bool(not np.array_equal(c["view"], c["prev_view"])),
            "object_valid": transforms[n]["object_valid"],
            "object_pixel_classification": "UNKNOWN_NO_OBJECT_SELECTOR_DUMP",
            "mask_scope": "geometry_only_not_camera_only_no_residual_selection",
            "previous_depth_checked": previous_depth_checked,
            "geometric_samples": int(mask.sum()), "total_pixels": w * h,
            "prediction_magnitude_p50_p95_p99": np.quantile(np.linalg.norm(predicted[mask], axis=-1), (.5,.95,.99)).tolist() if mask.any() else [],
            "comparisons": {name: summarize(v, motion, mask, budget) for name, v in variants.items()},
            "all_pixel_camera_status": "NOT_CLASSIFIED",
            "NR_guide_phase": "UNVERIFIED", "NR_MVecScale": "UNVERIFIED"})
    return {"status": "OFFLINE_CAMERA_COMPARISON_ONLY", "GPU": "NOT_RUN_BY_AUDITOR",
            "inputs": {"log": str(log.relative_to(poc)), "log_sha256": digest(log),
                       "resource_audit": str(audit_path.relative_to(poc)), "resource_audit_sha256": digest(audit_path)},
            "budget_declared_before_comparison": {"compute_allowance_render_pixels": COMPUTE_BUDGET_PIXELS,
                "quantization": "0.5*max_adjacent_RG16F_ULP_of_predicted_component",
                "local_depth_relative_z_span_max": LOCAL_Z_SPAN_RELATIVE,
                "previous_depth_relative_z_error_max": PREVIOUS_Z_ERROR_RELATIVE,
                "excluded": "sky/nonfinite/behind_camera/border/geometric_discontinuity; object regions UNKNOWN",
                "limitation": "Fixed conservative numeric allowance, not a proven GPU FMA bound or independent object mask"},
            "frames": frame_results, "resources": evidence,
            "interpretation": "Camera-sign/jitter diagnostics on a geometric screen only; NOT all-pixel MV, NR phase, quality, or performance acceptance"}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--poc", type=Path, required=True)
    p.add_argument("--case", type=Path, required=True)
    p.add_argument("--log", type=Path, required=True)
    args = p.parse_args()
    print(json.dumps(audit(args.poc, args.case, args.log), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
