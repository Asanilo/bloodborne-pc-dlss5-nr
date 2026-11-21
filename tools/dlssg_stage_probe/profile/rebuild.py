#!/usr/bin/env python3
"""Rebuild the fixed DLSS-G 310.9.1 1080p Quality numerical profile."""
import argparse
from collections import defaultdict
from contextlib import redirect_stdout
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import sys

LIBRARY_SHA = "aef4c911831b0381a6afa0164b743138e8571822646b6a4226e810a035ad678e"
CONTRACT_SHA = "878b92d2f4acbb968f8740733622025401627cacacc4827ad2ddb4558dab22d5"
PROFILE_SHA = "91756ce85e95b6dc17e90e490c4658f27a46725801a70beb4e285f23a6133cf3"
HEADER = b"\x7fELF\x02\x01\x01\x33\x07"
PREPARE_COUNT, PHASE_COUNT = 42, 35
PHASE_OFFSETS = {"Kernel_EstimateIntermMvecsScatter": 32, "k_initial_merge": 40, "k_central_block": 64}
APP_IMAGES = {"color": (1920, 1080, 37), "depth": (1280, 720, 130),
              "motion": (1280, 720, 83), "result": (1920, 1080, 37)}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_sdk(path):
    data = path.read_bytes()
    require(digest(data) == LIBRARY_SHA, "expected pinned Linux SDK 310.9.1 release library")
    return data


def cubins(data, names):
    pos = 0
    while (offset := data.find(HEADER, pos)) != -1:
        pos = offset + len(HEADER)
        if len(data) - offset < 64:
            continue
        arch, version = struct.unpack_from("<HI", data, offset + 18)
        ph, sh, flags, eh, phent, phnum, shent, shnum, strings = struct.unpack_from(
            "<QQIHHHHHH", data, offset + 32)
        if (arch, flags, eh, phent, shent) != (190, 0x590559, 64, 56, 64):
            continue
        length = max(ph + phnum * phent, sh + shnum * shent)
        if not 0 <= strings < shnum or length > len(data) - offset:
            continue
        binary = data[offset:offset + length]
        sections = [struct.unpack_from("<IIQQQQ", binary, sh + i * shent) for i in range(shnum)]
        _, _, _, _, start, size = sections[strings]
        section_names = binary[start:start + size]
        for sid, _, _, _, start, size in sections:
            title = section_names[sid:].split(b"\0", 1)[0].decode("ascii", errors="replace")
            if not title.startswith(".nv.info."):
                continue
            name = title.removeprefix(".nv.info.")
            if name not in names:
                continue
            metadata = binary[start:start + size]
            arguments = {}
            for match in re.finditer(rb"\x04\x17\x0c\x00", metadata):
                _, ordinal, position, code = struct.unpack_from("<IHHI", metadata, match.end())
                require(ordinal not in arguments, f"duplicate argument metadata for {name}")
                arguments[ordinal] = (position, code >> 18)
            require(arguments and list(sorted(arguments)) == list(range(len(arguments))),
                    f"missing argument metadata for {name}")
            fields = list(dict(sorted(arguments.items())).values())
            size_attr = re.search(rb"\x03\x19(..)", metadata, re.DOTALL)
            require(size_attr is not None, f"missing argument extent for {name}")
            size = int.from_bytes(size_attr.group(1), "little")
            require(all(0 < fsize <= size and off + fsize <= size for off, fsize in fields),
                    f"invalid argument metadata for {name}")
            yield dict(name=name, offset=offset, bytes=length, sha256=digest(binary),
                       arg_bytes=size, fields=fields, cuda_version=version)


def options(tokens):
    return dict(field.split("=", 1) for field in tokens)


def identifier(token, prefix):
    require(re.fullmatch(prefix + r"\d+", token), f"invalid symbolic {prefix} identifier")
    return int(token[1:])


def numbers(text, count):
    values = tuple(int(value) for value in text.split(","))
    require(len(values) == count, f"expected {count} values in {text}")
    return values


def add_unique(items, key, value):
    require(items.get(key, value) == value, f"conflicting symbolic resource {key}")
    items[key] = value


def scalar(kind, value):
    if kind == "f":
        return 4, int.from_bytes(struct.pack("<f", float(value)), "little")
    size = {"i": 4, "h": 2, "b": 1}[kind]
    parsed = int(value)
    require(0 <= parsed < 2 ** (size * 8), "scalar does not fit its typed ABI field")
    return size, parsed


def dispatch(row, program, tensors, buffers, used, textures):
    size = int(row["size"])
    require(size == program["arg_bytes"], f"incorrect argument extent for {row['name']}")
    occupied = set()
    args = []
    uses_images = False
    for key, value in row.items():
        if not re.fullmatch(r"[pifbh]\d+", key):
            continue
        arg_type, offset = key[0], int(key[1:])
        target = 0
        if arg_type == "p":
            token = identifier(value, "T")
            kind, length, bits = "scalar", 8, 0
            if token:
                resource, underlying, metadata = tensors[token]
                target = underlying
                if resource == "B":
                    kind, bits = "address", int(metadata["offset"])
                    require(0 <= bits < int(buffers[target]["bytes"]) and int(buffers[target]["usage"]) & 0x20000,
                            "buffer argument does not refer to an addressable allocation")
                    used["B"].add(target)
                else:
                    kind, target = "descriptor", token
                    used["I"].add(underlying)
                    textures.add(token)
                    uses_images = True
        else:
            length, bits = scalar(arg_type, value)
            kind = "scalar"
            if offset == PHASE_OFFSETS.get(row["name"]):
                require(arg_type == "f" and bits == 0x3F000000, "stock phase argument is not the 2X midpoint")
                kind = "phase"
        region = set(range(offset, offset + length))
        require(offset + length <= size and not occupied & region,
                f"overlapping or out-of-range fields for {row['name']}")
        occupied |= region
        args.append((offset, kind, length, target, bits))
    abi = program["fields"]
    # CUDA represents outer structures as one packed parameter. Individual
    # observed ranges within the structure come from typed Ada PTX loads.
    if abi != [(0, size)]:
        real = {(offset, width) for offset, _, width, _, _ in args}
        require(real == set(abi), f"fields disagree with device metadata for {row['name']}")
    grid, block = numbers(row["grid"], 3), numbers(row["block"], 3)
    require(all(n > 0 for n in grid + block) and block[0] * block[1] * block[2] <= 1024, "invalid dispatch geometry")
    return size, grid, block, int(row["shared"]), args, uses_images


def movement(fields, buffers, used):
    _, name, first, *tail = fields
    letter = first[0]
    a = identifier(first, letter)
    used[letter].add(a)
    b = 0
    if name.startswith("copy"):
        b = identifier(tail.pop(0), letter)
        used[letter].add(b)
    spec = options(tail)
    allocation = lambda b: int(buffers[b]["bytes"])
    if name == "copy_buffer":
        so, do, size = (int(spec[k]) for k in ("src", "dst", "bytes"))
        require(0 <= so < so + size <= allocation(a) and 0 <= do < do + size <= allocation(b),
                "buffer upload exceeds allocation size")
        values = (so, do, size)
    elif name == "fill_buffer":
        start, size = int(spec["off"]), int(spec["bytes"])
        require(0 <= start < allocation(a) and (size == 2 ** 64 - 1 or 0 < size <= allocation(a) - start),
                "buffer fill exceeds allocation size")
        values = (start, size, int(spec["value"], 16))
    elif name == "clear_image":
        require(spec["mip"] == spec["layers"] == "0,1", "unsupported clear subresource range")
        values = tuple(int(word, 16) for word in spec["value"].split(","))
    else:
        raise ValueError(f"unsupported numerical resource operation {name}")
    return name, a, b, values


def sections(frames, buffers, used):
    programs, copies = set(), {}
    sequences = {}
    phases = {}
    calls = {}
    for frame, events in frames.items():
        launches, fills, clears = [], [], []
        for kind, *fields in events:
            if kind == "GRAPH":
                row = options(fields)
                launches.append(row)
                continue
            name, a, b, values, *_ = movement(fields, buffers, used)
            if name == "copy_buffer":
                require(frame == 0, "unexpected model upload in a steady frame")
                copies[b] = (a, *values)
            elif name == "fill_buffer":
                fills.append((len(launches), a, *values))
            elif name == "clear_image":
                clears.append((len(launches), a, values))
            else:
                raise ValueError(f"the player sequence needs an implementation for {name}")
        calls[frame] = launches
        if frame == 0:
            require(launches[-1]["name"] == "Kernel_Copy4Channel",
                          "reset should end in a copy from the endpoint to the result")
            launches = launches[:-1]
        else:
            require(len(launches) == PREPARE_COUNT + PHASE_COUNT
                          and launches[PREPARE_COUNT - 1]["name"] == "Kernel_DL1Net_Output"
                          and launches[PREPARE_COUNT]["name"] == "Kernel_EstimateIntermMvecsScatter",
                          "unexpected preparation and phase split")
            phases[frame] = (launches[PREPARE_COUNT:],
                             [(index - PREPARE_COUNT, *values) for index, *values in fills
                              if index >= PREPARE_COUNT],
                             [(index - PREPARE_COUNT, *values) for index, *values in clears
                              if index >= PREPARE_COUNT])
            require(len(phases[frame][1]) == 2 and not phases[frame][2],
                          "phase sequence needs precisely the two scatter-buffer fills")
            launches, fills, clears = (launches[:PREPARE_COUNT], [f for f in fills if f[0] < PREPARE_COUNT],
                                       [c for c in clears if c[0] < PREPARE_COUNT])
        sequences[frame] = launches, fills, clears
        programs.update((row["name"], row["sha"]) for row in launches)
        if frame:
            programs.update((row["name"], row["sha"]) for row in phases[frame][0])
    return copies, programs, calls, sequences, phases


def preserve_pair_motion(phases, images, tensors, high_water):
    """Give blending weights their own image so later phases see the same motion."""
    scatter, weights, blend = phases[1][0][:3]
    require((scatter["name"], weights["name"], blend["name"]) ==
                  ("Kernel_EstimateIntermMvecsScatter", "Kernel_WarpBlendingWeights", "Kernel_BlendCandidatesFused"),
                  "unexpected start of the numerical phase")
    motion_field, weight_out, weight_in = (identifier(row[field], "T")
                                           for row, field in ((scatter, "p8"), (weights, "p48"), (blend, "p40")))
    motion, write, read = (tensors[token] for token in (motion_field, weight_out, weight_in))
    require(motion[:2] == write[:2] == read[:2] and motion[0] == "I"
                  and write[2]["kind"] == "3" and read[2]["kind"] == "1",
                  "could not verify the stock motion/weight image alias")
    weight_image = high_water[0] + 1
    images[weight_image] = images[motion[1]].copy()
    store, sample = high_water[1] + 1, high_water[1] + 2
    tensors[store], tensors[sample] = ("I", weight_image, write[2]), ("I", weight_image, read[2])
    for launches, _, _ in phases.values():
        scatter, weights, blend = launches[:3]
        require((scatter["p8"], weights["p48"], blend["p40"]) ==
                      (f"T{motion_field}", f"T{weight_out}", f"T{weight_in}"),
                      "pair variants have different motion/weight aliases")
        weights["p48"] = f"T{store}"
        blend["p40"] = f"T{sample}"


def cpp_array(items):
    return "{" + ", ".join(str(item) for item in items) + "}"


def cpp_source(source):
    offset, size, sha = source
    return cpp_array((offset, size, json.dumps(sha)))


def emit_array(name, kind, rows):
    print(f"constexpr std::array<{kind}, {len(rows)}> {name}{{{{")
    for row in rows:
        print(f"    {row},")
    print("}};")


def cpp_float(value):
    return repr(float(value)) + "f"


def texture_cpp(token, item, images):
    _, image, meta = item
    photo = images[image]
    require(meta["extent"] == photo["extent"] and meta["image_format"] == photo["fmt"]
            and meta["view_format"] == photo["fmt"] and meta["usage"] == photo["usage"],
            "image descriptor disagrees with its underlying allocation")
    kind = int(meta["kind"])
    require(kind in (1, 3) and meta["bits"] == "64", "unsupported image descriptor kind or width")
    if kind == 1:
        require(meta["flags"] == "0" and meta["compare"] == "0,0" and meta["anisotropy"] == "0,1",
                "unimplemented sampled image options")
        address = (*numbers(meta["addr"], 2), int(meta["waddr"]))
        filters = numbers(meta["minmag"], 2)
        lod = [cpp_float(meta[field]) for field in ("lo", "hi", "biaslod")]
    else:
        address, filters, lod = (0, 0, 0), (0, 0), ("0.f", "0.f", "0.f")
    return cpp_array((token, image, meta["view_format"], kind, meta["bits"],
                      cpp_array(numbers(meta["swizzle"], 4)), cpp_array(address),
                      *filters, meta.get("mipmode", 0), meta.get("border", 0), *lod,
                      "true" if meta.get("unnormalized") == "1" else "false"))


def numerical_entry(package, name):
    if package.startswith(HEADER):
        return list(cubins(package, {name}))
    magic, version, header, length = struct.unpack_from("<IHHQ", package)
    require((magic, version, header, length) == (0xBA55ED50, 1, 16, len(package) - 16),
            f"unexpected original CUDA container for {name}")
    cursor = header
    entries = []
    while cursor < len(package):
        kind, = struct.unpack_from("<H", package, cursor)
        member, padded, compressed = struct.unpack_from("<IQI", package, cursor + 4)
        arch, = struct.unpack_from("<I", package, cursor + 28)
        end = cursor + member + padded
        require(member >= 64 and cursor < end <= len(package), "bad CUDA package member range")
        if (kind, arch, compressed) == (2, 89, 0):
            entries.extend(cubins(package[cursor + member:end], {name}))
        cursor = end
    return entries


def original_programs(blob, active, module_ranges):
    programs = {}
    for name, sha in sorted(active):
        offset, size = module_ranges[sha]
        package = memoryview(blob)[offset:offset + size]
        require(len(package) == size and digest(package) == sha, f"SDK program mismatch for {name}")
        entries = numerical_entry(bytes(package), name)
        require(len(entries) == 1, f"expected one original Ada numerical entry for {name}")
        programs[name, sha] = (offset, size, sha), entries[0]
    return programs


def original_parameters(blob, layers, steady, copies, tensors, buffers):
    assets = {}
    for stage, name, cubin_sha, bindings in layers:
        call = steady[stage]
        require(name == call["name"] and (not cubin_sha or cubin_sha == call["sha"]),
                f"SDK convolution mismatch at stage {stage}")
        for field, (offset, size, sha) in bindings.items():
            kind, destination, binding = tensors[identifier(call[field], "T")]
            require(kind == "B" and int(binding["offset"]) == 0 and int(buffers[destination]["bytes"]) == size,
                    f"coefficient allocation differs at stage {stage}")
            source, src_offset, dst_offset, length = copies[destination]
            require((src_offset, dst_offset, length) == (0, 0, size) and int(buffers[source]["bytes"]) == size,
                    f"coefficient upload differs at stage {stage}")
            require(digest(memoryview(blob)[offset:offset + size]) == sha, f"SDK parameter mismatch at stage {stage}")
            add_unique(assets, destination, (offset, size, sha))
    require(len(assets) == len(copies) == 67, "expected 67 original model uploads")
    return assets


def render(blob, contract):
    frames = dict(enumerate(contract["frames"]))
    require(contract["version"] == 1 and
            [sum(row[0] == "GRAPH" for row in events) for events in frames.values()] == [10, 77, 77],
            "expected one reset and two steady synthetic source frames")
    buffers, images = ({int(key): value for key, value in contract["allocations"][kind].items()}
                        for kind in ("B", "I"))
    tensors = {int(key): value for key, value in contract["bindings"].items()}
    used = defaultdict(set)
    copies, active, calls, sequences, phases = sections(frames, buffers, used)
    preserve_pair_motion(phases, images, tensors, contract["high_water"])
    assets = original_parameters(blob, contract["weights"], calls[2], copies, tensors, buffers)
    staging = {row[0] for row in copies.values()}
    used["B"].difference_update(staging)
    programs = original_programs(blob, active, contract["modules"])
    program_ids = {key: index for index, key in enumerate(programs)}
    texture_ids, textured_programs = set(), set()
    dispatches, dispatch_ids, sequence_ids = [], {}, {}
    for name, (launches, fills, clears) in (
        ("seed", sequences[0]), ("pair_first", sequences[1]), ("pair_second", sequences[2]),
        ("phase_first", phases[1]), ("phase_second", phases[2]),
    ):
        references = []
        for row in launches:
            key = row["name"], row["sha"]
            size, grid, block, shared, fields, uses_images = dispatch(
                row, programs[key][1], tensors, buffers, used, texture_ids)
            if uses_images:
                textured_programs.add(key)
            entry = program_ids[key], size, grid, block, shared, tuple(fields)
            if entry not in dispatch_ids:
                dispatch_ids[entry] = len(dispatches)
                dispatches.append(entry)
            references.append(dispatch_ids[entry])
        sequence_ids[name] = references, fills, clears
    require(not staging & used["B"], "model upload staging referenced by a numerical stage")
    require(all(row["name"] not in PHASE_OFFSETS for frame in (1, 2) for row in sequences[frame][0]),
            "an endpoint stage unexpectedly reads phase")
    actual = {images[im]["role"]: (*numbers(images[im]["extent"], 3)[:2], int(images[im]["fmt"]))
              for im in used["I"] if images[im]["role"] != "internal"}
    require(actual == APP_IMAGES, "external image formats or extents changed")

    print("// Fixed stock DLSS-G numerical profile. Contains no SDK payload.")
    print("namespace player_core::stock_1080p_quality {")
    emit_array("programs", "ProgramSpec", [
        cpp_array((json.dumps(name), cpp_source(programs[name, sha][0]),
                   str((name, sha) in textured_programs).lower())) for name, sha in programs
    ])
    emit_array("buffers", "BufferSpec", [
        cpp_array((buf, buffers[buf]["bytes"], buffers[buf]["usage"], cpp_source(assets.get(buf, (0, 0, "")))))
        for buf in sorted(used["B"])
    ])
    emit_array("images", "ImageSpec", [
        cpp_array((im, images[im]["fmt"], cpp_array(numbers(images[im]["extent"], 3)),
                   images[im]["usage"], "ImageRole::" + images[im]["role"]))
        for im in sorted(used["I"])
    ])
    emit_array("textures", "TextureSpec", [texture_cpp(token, tensors[token], images) for token in sorted(texture_ids)])
    for index, (_, _, _, _, _, fields) in enumerate(dispatches):
        emit_array(f"args_{index}", "Argument", [
            cpp_array((offset, f"ArgumentKind::{kind}", size, target, f"{bits}ull"))
            for offset, kind, size, target, bits in fields
        ])
    emit_array("dispatches", "Dispatch", [
        cpp_array((program, size, cpp_array(grid), cpp_array(block), shared, f"args_{index}"))
        for index, (program, size, grid, block, shared, _) in enumerate(dispatches)
    ])
    for name, (references, fills, clears) in sequence_ids.items():
        emit_array(f"{name}_dispatches", "uint16_t", references)
        emit_array(f"{name}_fills", "BufferFill", [
            cpp_array((index, buf, f"{start}ull", f"{size}ull", value)) for index, buf, start, size, value in fills
        ])
        emit_array(f"{name}_clears", "ImageClear", [
            cpp_array((index, image, cpp_array(values))) for index, image, values in clears
        ])
        print(f"constexpr Sequence {name}{{{name}_dispatches, {name}_fills, {name}_clears}};")
    print("constexpr Profile definition{programs, buffers, images, textures, dispatches, seed,")
    print("                             {pair_first, pair_second}, {phase_first, phase_second}};")
    print("} // namespace player_core::stock_1080p_quality")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path, help="NVIDIA Linux x86-64 release libnvidia-ngx-dlssg.so.310.9.1")
    args = parser.parse_args()
    original = read_sdk(args.sdk)
    contract = gzip.decompress(Path(__file__).with_name("contract1080.json.gz").read_bytes())
    require(digest(contract) == CONTRACT_SHA, "synthetic numerical contract identity changed")
    output = io.StringIO()
    with redirect_stdout(output):
        render(original, json.loads(contract))
    generated = output.getvalue().encode("ascii")
    require(digest(generated) == PROFILE_SHA, "rebuilt profile is not the committed byte sequence")
    sys.stdout.buffer.write(generated)


if __name__ == "__main__":
    main()
