#!/usr/bin/env python3
"""Exercise one DLSS-G motion scatter stage on synthetic arrays via the CUDA Driver API."""
import argparse
import ctypes as c
import hashlib
import math
from pathlib import Path
import struct

MODULE_SHA = "aef4c911831b0381a6afa0164b743138e8571822646b6a4226e810a035ad678e"
NATIVE_SHA = "850895d0e61061b16e6ee4f6f03df741de7f47168253dac16a4daf41da0a70d4"
PTX_SHA = "c2d2efeaa8c73077bbcc631f2e2f6e6fb5e8b944bf2e9451f9b5190b561d5490"
KERNEL = b"Kernel_EstimateIntermMvecsScatter"
FATBIN_OFFSET = 6722081
WIDTH = HEIGHT = 32
PIXELS = WIDTH * HEIGHT
DELTA = (12.0, -6.0)
ENDPOINTS = ((22, 14), (10, 20))  # current, previous
PHASES = (("1/3", 1.0 / 3), ("1/2", 0.5), ("2/3", 2.0 / 3))
SENTINEL32 = 0x7FFFFFFF
SENTINEL = SENTINEL32 * 0x100000001
ROI = [y * WIDTH + x for y in range(12, 20) for x in range(12, 20)]
TOLERANCE_PIXELS = 0.01
PARAM_SIZE = 144

Int, UInt, Size, Ptr = c.c_int, c.c_uint, c.c_size_t, c.c_void_p
U64, Float = c.c_uint64, c.c_float


class ArrayDesc(c.Structure):
    # CUDA_ARRAY_DESCRIPTOR_v2
    _fields_ = [("width", Size), ("height", Size), ("format", Int), ("channels", UInt)]


class Copy2D(c.Structure):
    # CUDA_MEMCPY2D_v2
    _fields_ = [
        ("src_x", Size), ("src_y", Size), ("src_kind", Int), ("src_host", Ptr),
        ("src_device", U64), ("src_array", Ptr), ("src_pitch", Size),
        ("dst_x", Size), ("dst_y", Size), ("dst_kind", Int), ("dst_host", Ptr),
        ("dst_device", U64), ("dst_array", Ptr), ("dst_pitch", Size),
        ("width", Size), ("height", Size),
    ]


class Resource(c.Union):
    _fields_ = [("array", Ptr), ("reserved", Int * 32)]


class ResourceDesc(c.Structure):
    _fields_ = [("kind", Int), ("resource", Resource), ("flags", UInt)]


class TextureDesc(c.Structure):
    _fields_ = [
        ("address_mode", Int * 3), ("filter_mode", Int), ("flags", UInt),
        ("anisotropy", UInt), ("mipmap_filter", Int), ("mipmap_bias", Float),
        ("mipmap_min", Float), ("mipmap_max", Float), ("border", Float * 4),
        ("reserved", Int * 12),
    ]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def kernel_payloads(path):
    image = path.read_bytes()
    if digest(image) != MODULE_SHA:
        raise ValueError("expected the pinned Linux x86-64 DLSS-G 310.9.1 release module")

    # Fatbin member format and LZ4 expansion follow evidence/fixtures/check_ptx.py.
    magic, version, header, nbytes = struct.unpack_from("<IHHQ", image, FATBIN_OFFSET)
    if (magic, version, header) != (0xBA55ED50, 1, 16):
        raise ValueError("unexpected fatbin header")
    pos = FATBIN_OFFSET + header
    end = pos + nbytes
    if end > len(image):
        raise ValueError("fatbin extends beyond the input module")
    native = ptx = None
    while pos + 64 <= end:
        kind, = struct.unpack_from("<H", image, pos)
        size, padded, compressed = struct.unpack_from("<IQI", image, pos + 4)
        arch, = struct.unpack_from("<I", image, pos + 28)
        expanded, = struct.unpack_from("<Q", image, pos + 56)
        start = pos + size
        pos = start + padded
        if size < 64 or not padded or pos > end:
            raise ValueError("invalid fatbin member")
        if (kind, arch) == (2, 89) and not compressed:
            native = image[start:pos]
        if (kind, arch) == (1, 120) and 0 < compressed <= padded and 0 < expanded <= 2**20:
            decompress = c.CDLL("liblz4.so.1").LZ4_decompress_safe
            decompress.argtypes = (c.c_char_p, Ptr, Int, Int)
            decompress.restype = Int
            text = c.create_string_buffer(expanded)
            if decompress(image[start:start + compressed], text, compressed, expanded) != expanded:
                raise ValueError("PTX expansion failed")
            ptx = text.raw.rstrip(b"\0")
    if pos != end or native is None or ptx is None:
        raise ValueError("missing linked Ada cubin or Blackwell PTX member")
    if digest(native) != NATIVE_SHA or digest(ptx) != PTX_SHA:
        raise ValueError("fatbin member hash mismatch")
    if ptx.count(b".target sm_120") != 1 or KERNEL + b"_param_0[144]" not in ptx:
        raise ValueError("unexpected PTX target or argument declaration")
    return native, ptx


def expected_motion(t):
    # Stored vectors map from the intermediate sample back to the source endpoint.
    return ((1 - t) * DELTA[0], (1 - t) * DELTA[1]), (-t * DELTA[0], -t * DELTA[1])


def make_params(texture_handles, outputs, t):
    params = bytearray(PARAM_SIZE)
    struct.pack_into("<4Qf", params, 0, *texture_handles, t)
    struct.pack_into("<2Q", params, 40, *outputs)
    struct.pack_into("<IIfffB", params, 112, WIDTH, HEIGHT, 4.0, 2.0, 0.5, 0)
    return params


def decode_motion(word):
    y, x, depth = struct.unpack("<eef", word.to_bytes(8, "little"))
    return x * WIDTH, y * HEIGHT, depth


def float32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def scatter_anchor(source, t):
    t = float32(t)
    scale = -float32(1 - t) if source == 0 else t
    return tuple(p + math.trunc(float32(scale * d)) for p, d in zip(ENDPOINTS[source], DELTA, strict=True))


def depth_values(anchor):
    values = [0x3F000000] * PIXELS  # 0.5 background depth
    values[anchor[1] * WIDTH + anchor[0]] = 0x3E800000  # 0.25 at the moving point
    return struct.pack(f"<{PIXELS}I", *values)


def summarize(words, expected):
    vectors = [decode_motion(words[index]) for index in ROI if words[index] != SENTINEL]
    if len(vectors) != len(ROI) or any(not all(map(math.isfinite, v)) for v in vectors):
        return len(vectors), math.nan, math.nan, math.inf
    errs = [max(abs(x - expected[0]), abs(y - expected[1]), min(abs(z - 1), abs(z - 2 / 3)))
            for x, y, z in vectors]
    return len(vectors), sum(v[0] for v in vectors) / len(vectors), sum(v[1] for v in vectors) / len(vectors), max(errs)


def marker_locations(words):
    return {(index % WIDTH, index // WIDTH) for index, word in enumerate(words)
            if word != SENTINEL and abs(decode_motion(word)[2] - 2 / 3) < 0.001}


class Driver:
    def __init__(self):
        lib = c.CDLL("libcuda.so.1")
        for name, args in {
            "cuInit": (UInt,), "cuDeviceGet": (Ptr, Int), "cuDeviceGetAttribute": (Ptr, Int, Int),
            "cuCtxCreate_v2": (Ptr, UInt, Int), "cuCtxDestroy_v2": (Ptr,), "cuCtxSynchronize": (),
            "cuModuleLoadData": (Ptr, Ptr), "cuModuleGetFunction": (Ptr, Ptr, c.c_char_p),
            "cuModuleUnload": (Ptr,), "cuFuncGetParamCount": (Ptr, Ptr),
            "cuFuncGetParamInfo": (Ptr, Size, Ptr, Ptr),
            "cuLinkCreate_v2": (UInt, Ptr, Ptr, Ptr),
            "cuLinkAddData_v2": (Ptr, Int, Ptr, Size, c.c_char_p, UInt, Ptr, Ptr),
            "cuLinkComplete": (Ptr, Ptr, Ptr), "cuLinkDestroy": (Ptr,),
            "cuArrayCreate_v2": (Ptr, Ptr), "cuMemcpy2D_v2": (Ptr,),
            "cuTexObjectCreate": (Ptr, Ptr, Ptr, Ptr), "cuMemAlloc_v2": (Ptr, Size),
            "cuMemsetD32_v2": (U64, UInt, Size), "cuMemcpyDtoH_v2": (Ptr, U64, Size),
            "cuLaunchKernel": (Ptr, UInt, UInt, UInt, UInt, UInt, UInt, UInt, Ptr, Ptr, Ptr),
            "cuGetErrorName": (Int, Ptr),
        }.items():
            proc = getattr(lib, name)
            proc.argtypes = args
            proc.restype = Int
            setattr(self, name, proc)

    def call(self, name, *args):
        status = getattr(self, name)(*args)
        if status:
            label = c.c_char_p()
            self.cuGetErrorName(status, c.byref(label))
            raise RuntimeError(f"{name} failed with {label.value or status}")

    def texture(self, values, format_code, channels):
        array, texture = Ptr(), U64()
        self.call("cuArrayCreate_v2", c.byref(array), c.byref(ArrayDesc(WIDTH, HEIGHT, format_code, channels)))
        upload = c.create_string_buffer(values)
        row_bytes = WIDTH * channels * 4
        copy = Copy2D(src_kind=1, src_host=c.addressof(upload), src_pitch=row_bytes,
                      dst_kind=3, dst_array=array.value, width=row_bytes, height=HEIGHT)
        self.call("cuMemcpy2D_v2", c.byref(copy))
        source = ResourceDesc(resource=Resource(array=array))
        sampler = TextureDesc(address_mode=(Int * 3)(1, 1, 1), flags=int(format_code == 3))
        self.call("cuTexObjectCreate", c.byref(texture), c.byref(source), c.byref(sampler), None)
        return texture.value

    def load_kernel(self, payload):
        module, function = Ptr(), Ptr()
        self.call("cuModuleLoadData", c.byref(module), c.create_string_buffer(payload))
        self.call("cuModuleGetFunction", c.byref(function), module, KERNEL)
        count, offset, size = Size(), Size(), Size()
        self.call("cuFuncGetParamCount", function, c.byref(count))
        self.call("cuFuncGetParamInfo", function, 0, c.byref(offset), c.byref(size))
        if (count.value, offset.value, size.value) != (1, 0, PARAM_SIZE):
            raise ValueError(f"kernel parameter ABI {count.value}/{offset.value}/{size.value}")
        return module, function

    def compile_ptx(self, payload):
        state, cubin, size = Ptr(), Ptr(), Size()
        self.call("cuLinkCreate_v2", 0, None, None, c.byref(state))
        try:
            text = c.create_string_buffer(payload)
            self.call("cuLinkAddData_v2", state, 1, text, c.sizeof(text), b"scatter.ptx", 0, None, None)
            self.call("cuLinkComplete", state, c.byref(cubin), c.byref(size))
            if not cubin or not size.value:
                raise ValueError("CUDA linker returned an empty object")
            return c.string_at(cubin, size.value)
        finally:
            self.call("cuLinkDestroy", state)


def run_cases(cu, function, textures, outputs, variant):
    host = (U64 * PIXELS)()
    passed = True
    marker_centers_x = ([], [])
    for phase, t in PHASES:
        for output in outputs:
            cu.call("cuMemsetD32_v2", output, SENTINEL32, PIXELS * 2)
        args = c.create_string_buffer(bytes(make_params(textures, outputs, t)))
        params = (Ptr * 1)(c.addressof(args))
        cu.call("cuLaunchKernel", function, WIDTH // 16, HEIGHT // 16, 1, 324, 1, 1, 0, None, params, None)
        cu.call("cuCtxSynchronize")
        for source, (name, output, ref) in enumerate(zip(("current", "previous"), outputs, expected_motion(t), strict=True)):
            cu.call("cuMemcpyDtoH_v2", host, output, c.sizeof(host))
            count, x, y, error = summarize(host, ref)
            anchor = scatter_anchor(source, t)
            markers = marker_locations(host)
            center_x = sum(point[0] for point in markers) / len(markers) if markers else math.nan
            marker_centers_x[source].append(center_x)
            ok = error <= TOLERANCE_PIXELS and anchor in markers
            passed &= ok
            print(f"SCATTER {variant} t={phase} source={name} coverage={count}/{len(ROI)} "
                  f"motion_px=({x:.5f},{y:.5f}) expected_px=({ref[0]:.5f},{ref[1]:.5f}) "
                  f"max_error={error:.6f} marker_cells={len(markers)} marker_x={center_x:.5f} "
                  f"expected_anchor={anchor} anchor_hit={int(anchor in markers)} pass={int(ok)}", flush=True)
    markers_separate = all(x1 < x2 < x3 for x1, x2, x3 in marker_centers_x)
    print(f"PHASE_SEPARATION {variant} passed={int(markers_separate)}", flush=True)
    return passed and markers_separate


def gpu_check(variants):
    cu = Driver()
    device, ctx = Int(), Ptr()
    cu.call("cuInit", 0)
    cu.call("cuDeviceGet", c.byref(device), 0)
    major, minor = Int(), Int()
    cu.call("cuDeviceGetAttribute", c.byref(major), 75, device)
    cu.call("cuDeviceGetAttribute", c.byref(minor), 76, device)
    if (major.value, minor.value) != (8, 9):
        raise ValueError("the pinned Ada cubin and the experiment require compute capability 8.9")
    cu.call("cuCtxCreate_v2", c.byref(ctx), 0, device)
    try:
        # Same endpoint motion and depth arrays for every launch.
        current = struct.pack("<2f", -DELTA[0] / WIDTH, -DELTA[1] / HEIGHT) * PIXELS
        previous = struct.pack("<2f", *DELTA) * PIXELS
        motions = (cu.texture(current, 0x20, 2), cu.texture(previous, 0x20, 2))
        depths = tuple(cu.texture(depth_values(point), 3, 1) for point in ENDPOINTS)
        textures = motions + depths
        outputs = [U64(), U64()]
        for output in outputs:
            cu.call("cuMemAlloc_v2", c.byref(output), PIXELS * c.sizeof(U64))
        addresses = [p.value for p in outputs]
        passed = True
        for variant, payload in variants.items():
            print(f"PROGRAM {variant} sha256={digest(payload)}", flush=True)
            if variant == "adapted":
                payload = cu.compile_ptx(payload)
                print(f"JIT {variant} cubin_sha256={digest(payload)} bytes={len(payload)}", flush=True)
            module, function = cu.load_kernel(payload)
            passed &= run_cases(cu, function, textures, addresses, variant)
            cu.call("cuModuleUnload", module)
        return passed
    finally:
        cu.call("cuCtxDestroy_v2", ctx)


def plan():
    assert (c.sizeof(ArrayDesc), c.sizeof(Copy2D), c.sizeof(ResourceDesc), c.sizeof(TextureDesc)) == (24, 128, 144, 104)
    assert ResourceDesc.resource.offset == 8 and ResourceDesc.flags.offset == 136
    # Construct the same ctypes descriptors as the live path without opening libcuda.
    source = ResourceDesc(resource=Resource(array=Ptr(1)))
    assert source.resource.array == 1
    assert struct.unpack_from("<4Qf", make_params((1, 2, 3, 4), (5, 6), 0.5)) == (1, 2, 3, 4, 0.5)
    for label, t in PHASES:
        refs = expected_motion(t)
        for source, (x, y) in enumerate(refs):
            encoded = struct.pack("<eef", y / HEIGHT, x / WIDTH, 1.0)
            decoded = decode_motion(int.from_bytes(encoded, "little"))
            assert math.isclose(decoded[0], x) and math.isclose(decoded[1], y)
            host = [int.from_bytes(encoded, "little")] * PIXELS
            mx, my = scatter_anchor(source, t)
            marker = struct.pack("<eef", y / HEIGHT, x / WIDTH, 2 / 3)
            host[my * WIDTH + mx] = int.from_bytes(marker, "little")
            assert summarize(host, (x, y))[3] < TOLERANCE_PIXELS
            assert marker_locations(host) == {(mx, my)}
        print(f"CPU t={label} current_px={refs[0]} previous_px={refs[1]} "
              f"current_anchor={scatter_anchor(0, t)} previous_anchor={scatter_anchor(1, t)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--run", choices=("native", "adapted", "both"))
    args = parser.parse_args()
    native, ptx = kernel_payloads(args.module)
    adapted = ptx.replace(b".target sm_120", b".target sm_89")
    print(f"MODULE sha256={MODULE_SHA}\nNATIVE sha256={digest(native)}\nPTX sha256={digest(ptx)}"
          f"\nADAPTED sha256={digest(adapted)}", flush=True)
    plan()
    if args.run:
        variants = {"native": native, "adapted": adapted}
        selected = variants if args.run == "both" else {args.run: variants[args.run]}
        return 0 if gpu_check(selected) else 4
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
