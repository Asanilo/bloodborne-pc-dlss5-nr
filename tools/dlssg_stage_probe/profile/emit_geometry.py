#!/usr/bin/env python3
"""Verify a normalized stock geometry contract and emit its C++ tables."""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import sys


CONTRACT_SHA256 = "00030f8dc087ecab58d24b6b97c7f66b8e3b4b3604475a0024775071e7f8d5cd"


def emit(table, source_hash, output):
    def put(*args):
        print(*args, file=output)

    def fields(name, kind, values):
        put(f"inline constexpr std::array<{kind}, {len(values)}> {name}{{{{")
        for value in values:
            put(f"    {{{', '.join(map(str, value))}}},")
        put("}};\n")

    put("// Generated from typed stock NGX observations. Contains no SDK payload.")
    put(f"// Normalized contract SHA-256: {source_hash}")
    put("#pragma once\n\n#include <array>\n#include <cstdint>\n#include <span>\n")
    put("namespace player_core::extent_geometry {\n")
    put("""struct BufferExtent { uint16_t id, column; };
struct ImageExtent { uint16_t id, width_column, height_column; };
struct DispatchGrid { uint16_t dispatch; std::array<uint16_t, 3> columns; };
struct DispatchArgument { uint16_t dispatch, offset, column; };
struct DispatchAddress { uint16_t dispatch, offset, buffer; };
struct DispatchTexture { uint16_t dispatch, offset, texture; };
struct OutputGeometry {
    uint32_t width, height;
    std::span<const uint32_t> values;
    std::span<const DispatchAddress> addresses;
    std::span<const uint16_t> skipped;
    std::span<const DispatchTexture> textures;
};
""")
    fields("buffers", "BufferExtent", table["buffers"])
    fields("images", "ImageExtent", table["images"])
    fields("grids", "DispatchGrid", [(dispatch, "{" + ", ".join(map(str, cols)) + "}")
                                    for dispatch, *cols in table["grids"]])
    fields("arguments", "DispatchArgument", table["arguments"])
    for index, (addresses, skipped, textures) in enumerate(table["launch_variants"]):
        fields(f"addresses_{index}", "DispatchAddress", addresses)
        fields(f"skipped_{index}", "uint16_t", [(dispatch,) for dispatch in skipped])
        fields(f"textures_{index}", "DispatchTexture", textures)
    entries = []
    for width, height, binding, values in table["outputs"]:
        name = f"o{width}x{height}"
        put(f"inline constexpr std::array<uint32_t, {len(values)}> {name}{{{{")
        for start in range(0, len(values), 10):
            put("    " + ", ".join(f"{v}U" for v in values[start:start + 10]) + ",")
        put("}};\n")
        entries.append((width, height, name, f"addresses_{binding}", f"skipped_{binding}", f"textures_{binding}"))
    fields("outputs", "OutputGeometry", entries)
    put("} // namespace player_core::extent_geometry")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("contract", type=Path)
    parser.add_argument("template", type=Path)
    parser.add_argument("--check", type=Path, help="require a byte-identical generated header")
    args = parser.parse_args()
    data = gzip.decompress(args.contract.read_bytes())
    if hashlib.sha256(data).hexdigest() != CONTRACT_SHA256:
        parser.error("expected the audited stock geometry contract")
    table = json.loads(data)
    if hashlib.sha256(args.template.read_bytes()).hexdigest() != table["template_sha256"]:
        parser.error("1080p template differs from the captured baseline")
    rendered = io.StringIO()
    emit(table, CONTRACT_SHA256, rendered)
    if args.check:
        if rendered.getvalue() != args.check.read_text():
            parser.error("generated header differs")
    else:
        sys.stdout.write(rendered.getvalue())


if __name__ == "__main__":
    main()
