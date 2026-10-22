# Native / Helper development

Player entry and feature status: [project README](../../README.md) and
[Preview acceptance](../../docs/NRBRIDGE_PREVIEW_STATUS.md). This page is a build
and CPU-development guide, not permission to launch historical GPU experiments.

Native and standalone Helper/forwarder sources live in this repository.
The frozen import and dependency options are documented in [nrbridge](../../nrbridge/README.md). Protocol export identity is checked before building;
Native must not invent a second Helper protocol.

## Prepare explicit inputs

These are example paths, not bundled dependencies or an automatic fresh-machine
installer. Choose canonical absolute directories on your own system:

```bash
export NR_DEV_ROOT="$HOME/.local/share/bbport-nrbridge-dev"
export NRBRIDGE_HOME="$NR_DEV_ROOT"
export NR_HELPER_ROOT="/path/to/this-checkout"  # optional; local subtree is default
export NR_SOURCE_DEPS_ROOT="/path/to/prepared/source-deps"
export NR_BUILD_ENV_ROOT="/path/to/prepared/build-env"
export BB_ONLINE_DEPS_ROOT="/path/to/prepared/online-deps"
export BB_BUILD_DIR="$NR_DEV_ROOT/build/native"
```

- `NR_SOURCE_DEPS_ROOT` contains patched `fsr-vulkan`, `imgui`, `LibAtrac9` and their
  complete `*.content.sha256` inventories. The wrapper verifies files and applied
  patches before build side effects; arbitrary updated dependencies are rejected.
- `NR_BUILD_ENV_ROOT` contains `bbport-env.sh`, `root/usr` and `dlss-sdk`. The
  environment exports must select those actual directories; moving an old script
  does not fix its absolute exports. Standard local default: this checkout's `out/deps`.
- `BB_ONLINE_DEPS_ROOT` contains `root/usr` and `bbport-online-inputs.json` with
  checked JSON/miniupnpc and online-module inputs. Native online builds use its manifest.
- Helper owns its compiler/SDK arguments and build manifest. The local subtree is
  the default; `--helper-root` or `NR_HELPER_ROOT` can select an explicit checkout.
- Game, saves, Wine/GE, model and NGX selections are private runtime inputs. Do not
  copy proprietary files or account state into Git or a distributable image.

Build-time root precedence is `NR_DEV_ROOT`, then `NRBRIDGE_HOME`, then a
home-relative development default. Runtime sealing and Runner use
`NRBRIDGE_HOME`; set both variables to the same dedicated directory for this flow.

## Build

From the Native checkout:

```bash
bash scripts/dev-build.sh --test
# Runner can use --native-build "$PWD/out" with the generated provenance.
python3 tools/nr_dev/prepare_helper.py --helper-root "$NR_HELPER_ROOT"
python3 "$NR_HELPER_ROOT/nrbridge/forwarder/build.py"
```

Use `nrbridge/README.md` for the Helper compiler/SDK options.
`prepare_helper.py --legacy-rebuild` is an isolated frozen-closure reproduction,
not the normal Helper build. It never overwrites an accepted baseline.

The Native wrapper records source HEAD/diff, build inputs, actual environment
exports and artifact SHA. Runner accepts its verified build layout or immutable
`build/baselines/<full-commit>`. A source edit, successful compile and actual GPU
acceptance are three separate facts. Keep existing baseline bytes unchanged.

## CPU regression

```bash
python3 -m unittest discover -s tools/nr_dev/tests -p 'test_*.py'
python3 -m unittest discover -s tests -p 'test_nrbridge*.py'
python3 -m unittest discover -s "$NR_HELPER_ROOT/nrbridge/tests" -p 'test_*.py'
```

Tests use temporary save/runtime fixtures; the native-default tests link a test-only
passwd wrapper and never write a real historical PoC directory. Do not run old
GPU launch scripts as CPU verification. See [workspace guide](../../docs/WORKSPACE.md)
for private evidence and cleanup boundaries.

## Runtime and safety

The full launcher passes settings/INI/data paths to `play.py` and Runner. The selected
save folder is backed up/locked, copied privately, and synced only after proven
normal retirement. The AppImage runtime uses the same lifecycle and identity checks.
For normal play use the launcher, not a diagnostic seed/continue command.

`prepare_runtime_inputs.py --draft DRAFT --output "$NRBRIDGE_HOME/localinputs/NAME.json"`
seals a filled-in private config and separate pins; it refuses overwrites and unsafe
paths. `--check CONFIG` is read-only and starts no GPU/Wine/account operation.

Admission requires the exclusive project GPU lock, actual current-context safety,
matching binaries/config/pins and no current-boot HOLD. Per-job timeout, actual
Native/Helper GPU final use, FD/model retirement and ordinary-close grace remain.
A reboot does not retroactively retire a historical failed session. Do not forge
CONSUMED, clear HOLD or relax checks to make another run start.

Legacy probe paths remain isolated from daily runtime roots. Source-only path
privacy changes do not update an installed binary; a future rebuild needs its own
identity and appropriate validation. No new GPU validation is implied here.

## M1 source-built experiment

After the Native and standalone Helper builds, fill and seal
`runtime-inputs.template.json` with your own game, GE/Wine, model, forwarder,
Helper build manifest and NGX driver/library paths. For preset A/B use schema
`nr-local-runtime-inputs-v2` and `nr_preset: "A"` or `"B"`; select the stable model
in a separate sealed config for rollback. The template contains no dependencies.

```bash
export NRBRIDGE_HOME=/absolute/private/runtime-root
python3 tools/nr_dev/prepare_runtime_inputs.py --draft /absolute/private/draft.json \
  --output "$NRBRIDGE_HOME/localinputs/preset-B.json"
bash tools/nr_dev/play-m1.sh "$NRBRIDGE_HOME/localinputs/preset-B.json" --fg
```

Omit `--fg` for NR + SR; add `--sr-only` for SR + FG. FG defaults off and remains
Experimental. F8 controls NR; Delete exposes the FG switch for an explicit FG
launch. The full launcher has a separate default-off HUDless FG switch, requiring
a matching rebuilt M1 runtime. No script updates `runtime/current`.
[Mode selection, known vegetation drift and validation](../../docs/NR_SR_FG_INTEGRATION.zh-CN.md).
