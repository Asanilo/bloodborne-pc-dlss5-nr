# Workspace and branch guide

Factual layout as of 2026-10-26. For player-facing state and roadmap see
[project status](PROJECT_STATUS.zh-CN.md) and [roadmap](NRBRIDGE_ROADMAP.zh-CN.md).

## Repositories and branches

| Repository/reference | Purpose |
|---|---|
| bloodborne-pc-dlss5-nr: `nrbridge` | Default branch; current NRBridge/0.6 development and Preview source |
| bloodborne-pc-dlss5-nr: `master` | Retained fork mainline; not merged into NRBridge |
| bloodborne-pc-dlss5-nr: `m9/dlss-fg-dynamic` | FG independent line; **pending retirement** (`.worktree/fg`) |
| bloodborne-pc-dlss5-nr: `nr/temporal-stability` | NR temporal independent line; **pending retirement** (`.worktree/m5-nr`) |
| local only: `m10/nr-quality` | Unpushed NR quality experiment; kept to avoid losing work |
| `nrbridge/` | Standalone Helper/forwarder and canonical protocol source, frozen import from OptiScaler |
| `upstream` remote | Original bbport history; retained for future normal merges |

Branches merged into `nrbridge` were retired on 2026-10-26, locally and on the
Fork remote: `m2/dlss-fg`, `m4/dlss-mfg`, `m5/nr-dimensions`, `testing`. Their
FSR 3.1.6 path and original authorship remain in `nrbridge`. No force push or
history rewrite was involved. `master` is retained because it is not an ancestor.

Retirement order for the two remaining development lines: confirm no unique
commits or dirty files → archive the evidence that still matters → remove the
worktree and the branch reference → update this file.

In a multi-worktree checkout, keep the primary checkout and one active NRBridge
worktree (`.worktree/nrbridge`). A retired detached native-mouse worktree is no
longer required: its adapted implementation is in current source, and its
original candidate source/tests and unique evidence were verified privately.

## Source, development data and player installation

These are three separate trees; none should reference the private paths of another.

```text
<source>/                         # public source, tests, documentation
  .worktree/nrbridge/             # active development checkout
  out/  dist/                     # ignored build/dependency/package products
<development-data>/               # NR_DEV_ROOT / NRBRIDGE_HOME
  inputs/                         # prepared pinned inputs (source-deps, helper closure)
  localinputs/                    # private configs and pins
  seed/                           # private save seed
  build/{baselines,convergence,helper-runtime-model}/
<player-install>/                 # independent daily entry; not a source worktree
  CUSA03173/                      # game content
  runtime/current  runtime/previous
  data/{saves,nrbridge}/          # saves and runtime-private state
```

The local development data root is `/home/arinp/Code/nr-home`; the player
installation is `/home/arinp/bloodborne`. Defaults are derived from `$HOME`
rather than a hardcoded personal path, and every root can be overridden with
`NR_DEV_ROOT` / `NRBRIDGE_HOME` / `NR_SOURCE_DEPS_ROOT` / `NR_BUILD_ENV_ROOT` /
`BB_BUILD_DIR`. A relocated checkout still needs explicit preparation paths; an
old CMake cache or generated environment file does not become portable by moving it.

**Retired:** `/home/arinp/Code/nrbridge-dev` was deleted on 2026-10-26 after its
still-referenced inputs, private configurations, seed and Helper/forwarder build
outputs were migrated into `nr-home`, and every hardcoded default was pointed at
the new root. Nothing in the player installation references the old path.

The Helper subtree builds independently in this checkout; its compiler/SDK roots
and the online dependency root can be selected explicitly. See `nrbridge/README.md`.

Private defaults in Native resolve the actual UID home and fail closed on unsafe
or conflicting paths. Explicit daily runtime selection remains distinct from the
fixed historical PoC subtree; path cleanup grants no new GPU capability.

## Cleanup boundaries

Reproducible unpacked AppDirs can be removed after verifying the source image,
identity manifest and absence of active process/config dependencies. Unique
worktree source and tests must be retained before normal worktree removal.

Keep actual saves, credentials, selected model/GE dependencies, accepted baselines,
current/previous installed runtimes and required failure evidence. Build folders,
old runs and previous images are separate cleanup categories; do not infer safety
from a branch name or use a global `git clean -fdx`/`reset --hard`.

## Publication

English, Russian and Chinese READMEs describe the same Preview behavior and limits.
Public GitHub identities, upstream attribution and licenses stay intact. Local
paths/boot identifiers are redacted in publication copies; unmodified originals
remain in ignored private data. Existing Git history is not rewritten.
The installed runtime remains unchanged by documentation/portability source edits;
a future rebuild needs a new identity and appropriate acceptance.
