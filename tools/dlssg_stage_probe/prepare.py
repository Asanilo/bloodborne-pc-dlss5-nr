#!/usr/bin/env python3
"""Extract the pinned linked Ada cubin intact and check the paired PTX ABI."""
import argparse
import ctypes as c
import hashlib
from pathlib import Path
import struct

MODULE_SHA = "aef4c911831b0381a6afa0164b743138e8571822646b6a4226e810a035ad678e"
PAYLOADS = {
    "blend": (0x647481, b"Kernel_BlendCandidatesFused", b"240", 16800, 42030,
              "16d5c585faf924a47f068442ecbeea16f3871d364097593b3222e9e3ec958445",
              "5f1a0dfb94df02ac6644b4211cf059826b2c6646d4d9680697fc7a0d975c01b1"),
    "weights": (0x692DF1, b"Kernel_WarpBlendingWeights", b"88", 4136, 3703,
                "87b16fcd23251198496a56b48492c23806d3410e6f6794183b77e197c83c512f",
                "a01423e2c34f9b4e9721b5d5211bd5721638e825788df45b27151d6dd0eab9e7"),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(accepted, error):
    if not accepted:
        raise ValueError(error)


def extract(library, kind_name):
    offset, kernel, param_size, cubin_size, ptx_size, cubin_sha, ptx_sha = PAYLOADS[kind_name]
    data = library.read_bytes()
    require(digest(data) == MODULE_SHA, "expected pinned Linux x86-64 DLSS-G 310.9.1 release library")
    magic, version, header, payload_size = struct.unpack_from("<IHHQ", data, offset)
    require((magic, version, header) == (0xBA55ED50, 1, 16), "unexpected fatbin header")
    pos = offset + header
    end = pos + payload_size
    require(end <= len(data), "truncated fatbin")
    cubin = ptx = None
    while pos + 64 <= end:
        kind, = struct.unpack_from("<H", data, pos)
        size, padded, compressed = struct.unpack_from("<IQI", data, pos + 4)
        arch, = struct.unpack_from("<I", data, pos + 28)
        expanded, = struct.unpack_from("<Q", data, pos + 56)
        start = pos + size
        pos = start + padded
        require(size >= 64 and padded and pos <= end, "invalid fatbin member")
        if (kind, arch) == (2, 89):
            require(compressed == 0 and padded == cubin_size, "unexpected linked cubin size")
            cubin = data[start:pos]
        if (kind, arch) == (1, 89):
            require(0 < compressed <= padded and expanded == ptx_size, "unexpected PTX size")
            decode = c.CDLL("liblz4.so.1").LZ4_decompress_safe
            decode.argtypes = (c.c_char_p, c.c_void_p, c.c_int, c.c_int)
            decode.restype = c.c_int
            text = c.create_string_buffer(expanded)
            require(decode(data[start:start + compressed], text, compressed, expanded) == expanded,
                    "PTX expansion failed")
            ptx = text.raw.rstrip(b"\0")
    require(pos == end and cubin is not None and ptx is not None, "missing Ada cubin or PTX")
    require(digest(cubin) == cubin_sha and digest(ptx) == ptx_sha, "unexpected payload hashes")
    require(b".target sm_89" in ptx and kernel + b"_param_0[" + param_size + b"]" in ptx, "unexpected PTX ABI")
    require(struct.unpack_from("<I", cubin, 48) == (0x590559,), "unexpected cubin architecture")
    return cubin, ptx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("cubin", type=Path)
    parser.add_argument("--ptx", type=Path)
    parser.add_argument("--kind", choices=PAYLOADS, default="blend")
    args = parser.parse_args()
    cubin, ptx = extract(args.library, args.kind)
    args.cubin.write_bytes(cubin)
    if args.ptx:
        args.ptx.write_bytes(ptx)
    print(f"library_sha256={MODULE_SHA} cubin_sha256={digest(cubin)} ptx_sha256={digest(ptx)}")


if __name__ == "__main__":
    main()
