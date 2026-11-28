# Rebuilding the 1080p Quality profile

Run the generator with Python 3.9 or later and the unmodified NVIDIA DLSS SDK 310.9.1 Linux x86-64 release feature library. Set `DLSSG_SDK_LIBRARY` to the library’s local path and `CHECKOUT` to the checkout containing the committed profile.

```sh
python3 rebuild.py "$DLSSG_SDK_LIBRARY" > profile1080.generated.hpp
sha256sum profile1080.generated.hpp
cmp profile1080.generated.hpp "$CHECKOUT/gpu/upscale/native_mfg/profile1080.generated.hpp"
```

The expected SHA-256 is `91756ce85e95b6dc17e90e490c4658f27a46725801a70beb4e285f23a6133cf3`. The reference commit is `3b49926b02bbe868a24e2f0f667ed1b4c093fa11`.

`contract1080.json.gz` contains an allowlisted subset of the stock 1080p synthetic capture. It keeps the reset and first two steady source frames, symbolic resource bindings, allocation descriptions, and the required SDK program and parameter ranges. Its provenance records the full stock capture and parameter manifest hashes. The generator also checks the supplied SDK library, its original programs and coefficient ranges, and the generated profile.

The generator separates the motion field from the blending weights that reused its image in the stock single-phase schedule. This lets every generated phase read the same endpoint motion. The package contains source metadata only and runs on the CPU with the Python standard library.

## Output geometry and render resolution

The direct executor uses the same programs, weights and phase schedule across the
profiled outputs. `extent_geometry.json.gz` records the output dimensions, changes
to the 1080p template, and the source trace SHA-256 for each stock observation.
The per-output values share columns wherever all captured outputs agree. At context
creation the executor applies the output row and computes the depth and motion
parameters from the real texture sizes.

From this directory, verify the checked-in C++ table on the CPU:

```sh
python3 emit_geometry.py extent_geometry.json.gz ../../../gpu/upscale/native_mfg/profile1080.generated.hpp \
    --check ../../../gpu/upscale/native_mfg/extent_geometry.generated.hpp
```
