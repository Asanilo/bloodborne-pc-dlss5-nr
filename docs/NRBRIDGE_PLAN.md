# NRBridge development scope

This public plan describes the current architecture and remaining work. Original
machine-specific planning/acceptance records are preserved in private development
data. They are not rewritten into new passes. See [Preview status](NRBRIDGE_PREVIEW_STATUS.md)
for the reference runtime, and [workspace guide](WORKSPACE.md) for repository layout.

## Current architecture

- Native owns game translation, current-frame rendering, SR/presentation and
  selection/retirement of persistent visual history.
- The independent Helper owns model execution, Encode/Evaluate/Resolve, its Feature
  and shared-resource retirement. A versioned, SHA-checked export connects the repos.
- A managed daily runner owns admission, exact Native/Helper identity, private
  profiles and proven normal close. The full launcher delegates to that runner.
- Player settings/INI are passed explicitly. Save locks/backups are shared with
  the original launcher; successful normal retirement permits a verified sync.
- Native mouse uses one upstream camera path with local epoch/mailbox, cancellation
  and state protection. Joystick emulation remains available.

## Development priorities

The0.6 integration and initial playable Preview are complete within limited
acceptance. Next work should follow reproducible player feedback:

1. Real community login/messages/bloodstains/phantoms, then bounded network-exit checks.
2. Additional gameplay, lock-on/free camera, controller and DLC coverage.
3. Longer sessions and recovery cases without weakening actual GPU final-use checks.
4. Distribution portability and dependency preparation beyond the current private image.

NR flicker, valid-history drops and bright trails remain known issues. No automatic
model upgrade, FG project, or repeated wholesale upstream merge is implied.

## Change and evidence policy

Use small feature commits. Keep Native and Helper independently updateable.
Ordinary CPU/build work can proceed independently; GPU ownership/lifecycle changes
need focused review and real acceptance. Compile success cannot replace GPU proof.
A current-boot unsafe context blocks another run, including from another data root.

Do not delete genuine saves, unique source/tests, pinned accepted artifacts or
necessary failure evidence as build-cache cleanup. Preserve original evidence
before retiring worktrees. Publication copies can redact private paths, but must
retain conclusions and distinguish source-only edits from tested runtime binaries.
