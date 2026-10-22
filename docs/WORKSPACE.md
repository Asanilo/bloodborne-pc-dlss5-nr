# Workspace and branch guide

## Repositories and branches

| Repository/reference | Purpose |
|---|---|
| bloodborne-pc-dlss5-nr: `nrbridge` | Current NRBridge/0.6 development and Preview source |
| bloodborne-pc-dlss5-nr: `master` | Retained fork mainline; not automatically switched to NRBridge |
| bloodborne-pc-dlss5-nr: `testing` | Retained upstream-testing reference |
| `nrbridge/` | Standalone Helper/forwarder and canonical protocol source, frozen import from OptiScaler |
| `upstream` remote | Original bbport history; retained for future normal merges |

There are three retained local/Fork Native branches. Upstream 8185b88 and
gameplay 7265d3b are ancestors of the NRBridge branch. No force push or history
rewrite is part of workspace cleanup. The default branch is `nrbridge`; the earlier development branch name has been retired.

In a multi-worktree checkout, keep the primary checkout and one active NRBridge
worktree (`.worktree/nrbridge`). The retired detached native-mouse worktree is no
longer required: its adapted implementation is in current source, and its original
candidate source/tests and unique evidence were verified and retained privately.
No archive branches/tags were added for this retirement.

## Upstream Helper reference snapshot (before migration)

The Helper repository is inspected separately:13 local branches and2 worktrees.
Its current `dev/nrbridge-helper-convergence-20261019` is2 commits ahead of
`linux-nr`; both worktrees are clean. The CI branch and historical PoC branches
may carry independent work. They are preserved in this Native cleanup, rather
than being deleted based on their names. No Helper ref, source, protocol or
installed artifact is changed here.

## Separate source, development data and player installation

```text
<source>/                     # public source, tests, documentation
  .worktree/nrbridge/          # optional active development checkout
  out/                        # ignored build/dependency/test products
  dist/                       # ignored local packages
<development-data>/           # NR_DEV_ROOT / NRBRIDGE_HOME as explicitly selected
  inputs/                     # prepared pinned inputs
  localinputs/                # private configs and pins
  build/baselines/            # accepted immutable Native identities
  runs/                       # private profiles, progress and acceptance evidence
<player-install>/             # independent daily entry; not a source worktree
  packages/
  runtime/current
  data/nrbridge/
```

Build defaults are derived from the user home or checkout rather than a personal
username. Use explicit preparation paths for a relocated checkout; an old CMake
cache or generated environment file is not made portable merely by moving it.
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
