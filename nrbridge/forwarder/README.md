# NRBridge forwarder source closure

This target builds only the unchanged production forwarder with the original shutdown shim. `shutdown_bridge.cpp` and `parameter_abi.h` preserve the original bytes from the untracked `poc/nr_runtime` files; `source-provenance.json` records their hashes, original worktree/branch and recipe source. The original eleven untracked files remain in place. The parameter ABI header is retained for the subsequent Helper migration; this DLL does not compile it.

From this repository (set `NR_LLVM_ROOT` or pass `--llvm-root`):

```bash
python3 nrbridge/forwarder/build.py --check-only
python3 nrbridge/forwarder/build.py
```

The script verifies the two preserved sources, the production source and the locked LLVM MinGW wrapper before writing outputs. The production source must match SHA `6e355af2305bf9584dbfc1b8701ffd9e9bc0341ee32d79ee8976403f81841b5a`, identical between the f527 Helper base and the original runtime worktree. A drift is rejected, without updating the lock or patching a source.

The original command uses `-std=c++17 -static -shared`, the production `.cpp`, shutdown `.cpp` and `-ld3d12`; this target changes only the source/output paths. It discovers and hashes the compiler's actual transitive header closure, frontend/linker binaries and persistent link objects/libraries. Ambient include/library/compiler/cache overrides are removed, and temporary compiler files stay inside the new output directory. The manifest includes the exact command, source identity, compiler version, complete discovered file identities, export inspector identity and PE import report. It does not invoke Wine or a GPU.

Outputs stay under `$NR_DEV_ROOT/build/convergence/forwarder/<input-digest>/` (default: `out/nr-dev/`). Existing complete outputs are verified and reused; incomplete or altered outputs are preserved and rejected. The generated DLL must match all 26 names and ordinals captured from the current forwarder, including `nr_poc_vk_shutdown`. This verifies the exported interface, without asserting binary or GPU equivalence. The current Helper, forwarder DLL, frozen manifest and runtime pins remain selected and unchanged; this recipe does not install its output.

The runtime-model entry/frame migration, versioned protocol export and content-addressed Helper build are complete; see `../README.md` and `../build.py`. The old DX12/preflight/audit build branches are not part of this target. Historical binary outputs were retired in the current-only cleanup; their old paths are provenance, not currently deployed rollback artifacts.
