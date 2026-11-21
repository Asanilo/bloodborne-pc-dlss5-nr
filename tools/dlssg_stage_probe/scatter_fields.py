#!/usr/bin/env python3
"""Export native Ada scatter fields from a 64x32 synthetic rightward translation."""
import argparse
import ctypes as c
import os
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import phase_math as scatter

WIDTH, HEIGHT = 64, 32
PIXELS = WIDTH * HEIGHT
DELTA_X = 24
DIRECTIONS = ("current", "previous")
CENTER = [y * WIDTH + x for y in range(8, 24) for x in range(24, 40)]


def check_field(words, expected_x):
    valid = 0
    for word in words:
        if word == scatter.SENTINEL:
            continue
        y, x, depth = struct.unpack("<eef", word.to_bytes(8, "little"))
        if not (abs(x * WIDTH - expected_x) <= scatter.TOLERANCE_PIXELS and y == 0 and abs(depth - 1.0) <= 0.001):
            raise ValueError(f"unexpected scatter value {(x, y, depth)} for x={expected_x} px")
        valid += 1
    if any(words[index] == scatter.SENTINEL for index in CENTER):
        raise ValueError("scatter did not fill the common interior")
    return valid


def export_fields(cu, function, inputs, outputs):
    host = (scatter.U64 * PIXELS)()
    fields = {}
    for phase, t in scatter.PHASES:
        for output in outputs:
            cu.call("cuMemsetD32_v2", output, scatter.SENTINEL32, PIXELS * 2)
        args = c.create_string_buffer(bytes(scatter.make_params(inputs, outputs, t)))
        params = (scatter.Ptr * 1)(c.addressof(args))
        cu.call("cuLaunchKernel", function, WIDTH // 16, HEIGHT // 16, 1, 324, 1, 1, 0, None, params, None)
        cu.call("cuCtxSynchronize")
        expected = ((1 - t) * DELTA_X, -t * DELTA_X)
        for direction, output, expected_x in zip(DIRECTIONS, outputs, expected, strict=True):
            cu.call("cuMemcpyDtoH_v2", host, output, c.sizeof(host))
            valid = check_field(host, expected_x)
            filename = f"phase-{phase.replace('/', '-')}-{direction}.bin"
            data = bytes(host)
            fields[filename] = data
            print(f"FIELD {filename} x_pixels={expected_x:g} valid={valid} sentinel={PIXELS - valid} "
                  f"sha256={scatter.digest(data)}", flush=True)
    return fields


def capture(native):
    # Driver.texture and make_params take extents from this fixture's in-process module.
    scatter.WIDTH, scatter.HEIGHT = WIDTH, HEIGHT
    cu = scatter.Driver()
    device, ctx = scatter.Int(), scatter.Ptr()
    cu.call("cuInit", 0)
    cu.call("cuDeviceGet", c.byref(device), 0)
    major, minor = scatter.Int(), scatter.Int()
    cu.call("cuDeviceGetAttribute", c.byref(major), 75, device)
    cu.call("cuDeviceGetAttribute", c.byref(minor), 76, device)
    if (major.value, minor.value) != (8, 9):
        raise ValueError("native scatter fixture requires CUDA sm_89")
    cu.call("cuCtxCreate_v2", c.byref(ctx), 0, device)
    try:
        module, function = cu.load_kernel(native)
        current = cu.texture(struct.pack("<2f", -DELTA_X / WIDTH, 0) * PIXELS, 0x20, 2)
        previous = cu.texture(struct.pack("<2f", DELTA_X, 0) * PIXELS, 0x20, 2)
        depth = cu.texture(struct.pack("<I", 0x3F000000) * PIXELS, 3, 1)
        buffers = [scatter.U64(), scatter.U64()]
        for buffer in buffers:
            cu.call("cuMemAlloc_v2", c.byref(buffer), PIXELS * c.sizeof(scatter.U64))
        fields = export_fields(cu, function, (current, previous, depth, depth), [b.value for b in buffers])
        cu.call("cuModuleUnload", module)
        return fields
    finally:
        cu.call("cuCtxDestroy_v2", ctx)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("output_directory", type=Path, help="existing empty private directory, mode 0700")
    args = parser.parse_args()
    directory = args.output_directory.resolve(strict=True)
    info = directory.stat()
    if not directory.is_dir() or info.st_uid != os.geteuid() or info.st_mode & 0o077 or any(directory.iterdir()):
        raise ValueError("output must be an empty private directory owned by this user")
    if sys.byteorder != "little":
        raise ValueError("scatter output requires a little-endian host")
    native, _ = scatter.kernel_payloads(args.module)
    print(f"NATIVE sha256={scatter.digest(native)} extent={WIDTH}x{HEIGHT} endpoint_dx={DELTA_X}", flush=True)
    for name, data in capture(native).items():
        with (directory / name).open("xb") as output:
            output.write(data)


if __name__ == "__main__":
    main()
