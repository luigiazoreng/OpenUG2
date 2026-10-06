# First renderer design slice: host evidence

The first #14 slice is an inventory and C API proposal. No backend boundary has
been implemented yet. Source compiled for this round is `3f7536e`; subsequent
changes in this branch are documentation, the inventory generator and a header
comment. The original `homologation-host-2026-10-05` tag remains unchanged.

Private evidence: `/home/luigi/Documents/luigi/nfsu2-data/reports/renderer-contract-2026-10-05/`.
The executable scripts in that directory record exact argv, return codes,
warning counts and cgroup memory measurements. Captures use the pinned #7
collector in `../OpenUG2-issue7` at `7e95be5` and the preserved upstream
`6c9dd09` reference. Retail data stays outside Git and the VPK.

- Normal, debug and GLES: PASS, zero compiler warnings. All engine C files use
  `-Wall -Wextra -Werror`. Debug also freshly compiles the C++ wrapper/ImGui
  objects with the repository's existing C++ flags; no C++ `-Werror` claim.
- Fixed L4RA and L4RB: instance/resident counters equal and PNG bytes identical
  to the upstream reference, stock 240SX at 960x544, heading zero.
- L4RA PNG SHA-256: `82a8948a979fb539937f812f32fff66c713ef6e3aba493c7d2017aedd402bade`.
- L4RB PNG SHA-256: `44bfedab83c5a757fdf7d27046552803fb139f780e3b9a5449d9fc93cbae01e2`.
- Source/capture driver is the original desktop graphical session. The initial
  Xvfb attempt differed; its logs/image are retained in `xvfb-attempt/` and are
  not accepted as conformance evidence against that original-driver reference.
- Vita bootstrap rebuilt in a new `build/vita-review-clean` directory, all five
  application C translation units compiled, `CFLAGS=-Werror`, target `-Wall
  -Wextra`, no warnings. VitaSDK `/tmp/openug2-vitasdk`, one build job.
- VPK CRC, exact members (`eboot.bin`, `sce_sys/param.sfo`), title `OPENUG200`
  and SHA-256 verified by `validate-builds-resume.py`, with an explicit log and
  JSON. 1,150,311 bytes, SHA-256
  `7b665a2d0d8966e716747a827f8e567350f745f60d6bfb86c8f9c9944bc95476`.
  This artifact closes the clean-build evidence gap; it was not installed or
  physically homologated in this round.

Build cgroups: 512 MiB maximum, zero swap, 4 GiB host reserve before starting.
Observed build peaks: normal 457.3, debug 463.6, GLES 446.9, Vita 176.6 MiB.
The six real-data comparisons ran sequentially under 1,536 MiB (peak 1,113.6),
with a separate 1,024 MiB RSS limit and 4 GiB host reserve in the collector.
A comparison that fails a memory gate must stop; these limits are not a Vita
memory budget.

The design's future migration/config fallbacks remain a proposal. This round
preserves existing runtime behavior, not the implementation of every proposed
API or a claim of Vulkan/DX12 support. No issue comment, PR or push was made.
