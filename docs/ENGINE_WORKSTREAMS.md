# Engine contributions alongside the Vita port

Reviewed upstream's [open issues](https://github.com/whoismept/OpenUG2/issues)
on 2026-10-05. The initial coverage baseline remains
`84ab8b3ff59bdbfad78a0bda24ac29dd52af9fc1`; the integration branch now uses
`development` commit `6c9dd091ee3297dea898c78764edf209657ce3d5`.
The dependencies and file boundaries below are our proposed coordination plan,
not upstream assignments. Engine contribution PRs target upstream's
`development` branch, as requested in [#5](https://github.com/whoismept/OpenUG2/issues/5).

## Branch and integration policy

The Vita port uses a pinned commit already integrated into upstream `main`.
The `integration-development` branch combines the existing bring-up with
`6c9dd091ee3297dea898c78764edf209657ce3d5`, also integrated into `main` by
`59c9263dd0299a6f41827caca6536005e4a32cf2`. The original `vita-bringup` branch
and the issue #7 checkout retain their earlier baseline.
This candidate has not been homologated: the inherited `ai-drive-test` failure
is documented in [DEVELOPMENT_INTEGRATION.md](DEVELOPMENT_INTEGRATION.md).
Update that baseline deliberately, after reviewing the upstream changes and
passing the relevant builds and regressions. Record the new SHA and refresh
the portability inventory when the baseline changes.

Keep one branch per engine issue. Validate each contribution against a named
`development` commit in a separate checkout before opening its PR, resolving
conflicts there and recording the tested SHA. The PR destination is
`development`; the roadmap does not mandate that every feature branch originate
there. Integrate accepted engine changes into the Vita branch at a planned
baseline update.

Preserve coverage reports as evidence for the engine/input version that produced
them. A changed engine baseline requires a new report directory and fresh
measurements. Builds and data tests run sequentially with the established RAM
limits and host-memory reserve.

## Independent first slices

Execution is sequential: finish and validate one slice before starting the
next. Each issue gets its own branch. Public issue updates are written only
after the slice is finished, validated and homologated, with remaining limits
stated explicitly. No new issue announcement accompanies this integration.

The agreed queue is:

1. Integrate and validate the pinned `development` baseline, preserving #7
   evidence. Validation found an inherited traffic-contact failure; promotion
   remains pending. See [DEVELOPMENT_INTEGRATION.md](DEVELOPMENT_INTEGRATION.md).
2. Issue #14: current GL call/ownership inventory and a small proposed C API.
3. Separate Vita slices: owned uniform state, then a budgeted resource backend.
4. Issue #8: focused synthetic collision fixtures.

| Workstream | Upstream issue | Independent deliverable | Files / boundary | Integration dependency |
|---|---|---|---|---|
| Region measurements — **first focus** | [#7](https://github.com/whoismept/OpenUG2/issues/7) | Repeatable eight-region coverage report, commands, supported poses and explicit gaps | `tools/stream_coverage.py`, documentation; generated evidence outside Git | Second independent machine before upstream acceptance |
| Renderer contract | [#14](https://github.com/whoismept/OpenUG2/issues/14) | Classify existing GL calls and ownership; propose a small C interface | Renderer design document; reuse `VITA_API_INVENTORY.md` | Agree API before changing `render.c/.h`; Vita consumes it later |
| Vehicle data and deterministic handling measurements | [#9](https://github.com/whoismept/OpenUG2/issues/9) | Cross-car field evidence and fixed-input comparison, initially without tuning gameplay | Pure probes/fixtures, `physics.c/.h`, relevant format notes | Live handling changes require world contact and a separate 60 Hz simulation clock |
| Collision fixtures | [#8](https://github.com/whoismept/OpenUG2/issues/8) | Synthetic slope/seam/rotated-building failures with exact source attribution | Collision tests first; agree before shared `physics.c` edits | Live route coverage uses #7 poses and the active resident scene |
| Event measurements | [#10](https://github.com/whoismept/OpenUG2/issues/10) | Authored event/start/gate coverage table before adding opponents | Event audit tools and documentation | New AI contact behavior depends on #8/#9 contracts |
| Save research — later | [#12](https://github.com/whoismept/OpenUG2/issues/12) | Read-only structural probes and synthetic corrupt/truncated cases | Separate parser/tool; no live career integration | Internal career model before importing progression |
| Front end design — later | [#13](https://github.com/whoismept/OpenUG2/issues/13) | Navigation contract and versioned settings model | Separate UI/settings design; avoid `main.c` integration initially | Proven event/unlock fields for career behavior |
| Device bring-up | Local [Vita plan](VITA_PORT.md); related [#11](https://github.com/whoismept/OpenUG2/issues/11) | Install existing VPK; verify context, parser, controls and shutdown | `platform/vita/`, Vita build target | Physical Vita; #11's ARM Linux/PortMaster target is a separate platform |

These slices can progress concurrently because they collect evidence or establish
contracts before changing shared orchestration. Renderer/world/physics integration
is coordinated work, particularly in `src/main.c`. Use one branch per slice and
avoid combining a platform bootstrap with an unrelated engine correction.

[#6](https://github.com/whoismept/OpenUG2/issues/6) remains upstream's highest
priority map-fidelity investigation. It overlaps material parsing, instance
assembly and drawing: attribute the production batch first, then write the
smallest fixture. The coverage measurements help select reproducible views;
they do not identify the black-sheet cause by themselves.

## First focus: issue #7

Start with the existing production diagnostics and the user's external PC data
root. Do not change parser, renderer, scenery policy or resident radius to obtain
a nicer result. Keep each STREAM separate and use the stock 240SX at 960x544.

The local runner discovers a supported pose, freezes its printed XY coordinates,
runs instance and resident audits twice, and captures one frame with the existing
texture diagnostic. It records commands, binary/source identity, asset hashes,
platform and GL driver. Compare structural counters and support; elapsed times
are measurements rather than determinism criteria.

The `issue-7-stream-coverage` branch owns `docs/STREAM_COVERAGE.md` and the
collector; its separate local checkout is `../OpenUG2-issue7`. See that document
for commands and the eight-region machine A matrix, including source/emitted
counts and provisional capture review. Its status stays **draft** until another
independent machine reproduces the rows. Same-machine repetitions, another build
and a Vita emulator do not satisfy that requirement. Any suspected defect needs
a focused reproduction before it is treated as an engine bug.

The investigation is registered in [issue #7](https://github.com/whoismept/OpenUG2/issues/7#issuecomment-6002313415).
No upstream PR has been opened. Second-machine reproduction remains pending.
