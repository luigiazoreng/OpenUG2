# Engine contributions alongside the Vita port

Reviewed upstream's [open issues](https://github.com/whoismept/OpenUG2/issues)
on 2026-10-05 against local baseline
`84ab8b3ff59bdbfad78a0bda24ac29dd52af9fc1`.
The dependencies and file boundaries below are our proposed coordination plan,
not upstream assignments. Work is staged for upstream's `development` branch.

## Independent first slices

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

No upstream issue comment, assignment or PR has been posted from this workspace.
Coordinate a claim in #7 when publishing the prepared contribution.
