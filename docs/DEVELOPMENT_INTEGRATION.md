# Development baseline integration — 2026-10-05

Branch: `integration-development`.

**Status: candidate integrated and checked, not homologated.** The existing
traffic-contact regression fails in both this branch and a clean upstream
checkout. Do not promote this candidate into `vita-bringup` or begin the next
queue item until that failure is resolved and the affected checks pass.
Existing compiler warnings also prevent a claim of warning-free acceptance.

## Provenance and isolation

- Previous engine baseline: `84ab8b3ff59bdbfad78a0bda24ac29dd52af9fc1`.
- Previous local Vita work: `8315923`, retained on `vita-bringup`.
- Pinned upstream `development`: `6c9dd091ee3297dea898c78764edf209657ce3d5`.
- Upstream `main` already incorporates that development commit through
  `59c9263dd0299a6f41827caca6536005e4a32cf2`; their trees were identical at fetch.
- Candidate merge: `9eea351`, parents `8315923` and `6c9dd09`.
- Issue #7 remains on `issue-7-stream-coverage` at `7e95be5` with its previous
  engine, executable, fixed-pose measurements and local captures.

The merge had no textual conflicts. Relative to the pinned upstream source,
the candidate changes only `src/debugui.cpp` by defining
`GL_GLEXT_PROTOTYPES` before the SDL OpenGL header on Linux. The existing
Makefile compatibility fixes and isolated Vita target remain. Production
parser, world, physics, AI and renderer implementations match upstream.

Public issue updates are deferred until the relevant slice is complete,
validated and homologated. This integration publishes no comment or PR.
Subsequent issues each use their own branch and run sequentially.

## Changes reviewed

The upstream delta spans 28 files, including:

- renderer: wet materials/lights/reflections, car shadow/environment resources,
  rain, conservative visibility and related state management;
- traffic: route/body clearance, contact relaxation, density and heatmap
  diagnostics;
- ground/physics: reachable rising-ramp selection, supported attitude limits,
  and tall retaining-wall contact at actual body height;
- resident uploads: obstacle collection in bounded source-ordered slices;
- car attributes: mirrored source wheel-pair validation, including long/wide
  traffic vehicles;
- new or expanded material, ride, traffic, light, wheel and weather regressions.

The Vita API inventory was regenerated for the pinned commit: 82 direct GL
names, five additional GL functions loaded by name, 34 SDL names and 83
libc/POSIX/math/zlib names. Eight GL dependencies were added since the previous
baseline: `glClearStencil`, `glCopyTexImage2D`, `glCopyTexSubImage2D`,
`glDrawArrays`, `glStencilFunc`, `glStencilMask`, `glStencilOp` and
`glVertexAttrib3f`. All eight are declared/exported by the installed vitaGL;
`glGetUniformfv` remains the only missing direct engine GL dependency.
Header/symbol availability does not prove device compatibility. The #14
resource-ownership classification and proposed renderer C API are next work,
not implemented by this baseline integration.

## Builds and regressions

Host: Linux x86_64, GCC 13.3.0. SDL headers were staged outside the
repository; the installed desktop SDL2 runtime was used. Exact argv, logs and
memory observations remain external:

```text
/home/luigi/Documents/luigi/nfsu2-data/reports/development-integration-2026-10-05/
```

| Check | Result | Notes |
|---|---|---|
| Normal build | PASS | 186 warnings; same messages as pristine development |
| Debug build | PASS | 187 warnings |
| GLES build | PASS | 190 warnings |
| Isolated Vita VPK | PASS | Zero warnings; CRC and package contents checked |
| Car material | PASS | 351 assertions, zero failures |
| World group / instance / resident / render | PASS | Existing synthetic fixtures |
| District collision / ground motion | PASS | Includes upstream ramp/wall cases |
| Traffic heatmap | PASS | Direction, layers, density, decay and bounds |
| AI drive/contact | **FAIL** | Same assertion on pristine upstream |
| World texture / wheel render / wheel reload | PASS | Includes rollback/repeated swaps |
| Light state / headlight / weather / HUD | PASS | GL-state, effects and layout regressions |
| World / resolution / AI drive CLI | PASS | Argument validation and failure paths |
| Vita parser host probe | PASS | ASan/UBSan; synthetic data and missing/truncated/oversized files |
| L4RA/L4RB reference comparison | PASS | Same-pose instance/resident counts and byte-identical captures |

Fifteen of sixteen selected synthetic regression targets passed; all three
selected CLI targets passed. The other failures are not hidden by filtering
or weaker assertions. The failed AI target was separately reproduced against
pristine `development`; a temporary diagnostic added case parameters without
changing the assertion or production code.

Normal builds in the candidate and clean reference each emitted 186 warnings,
with identical warning-message multisets. This establishes no new normal-build
warning from integration, not zero-warning acceptance. The clean reference
needed the same `_DEFAULT_SOURCE` compiler feature macro already supplied by
the Vita branch's Linux Makefile fix.

The VPK contains only `eboot.bin` and `sce_sys/param.sfo`; no retail data is
included. It remains the isolated bootstrap, not the full engine. Physical
Vita execution, installation and performance are unverified.

## Reproducible traffic failure

Both builds fail at `tools/ai_drive_test.c:58`:

```c
assert(fabsf(-c->vel[0]*sn+c->vel[1]*co-.03f)<1e-5f);
```

The first failing local case has:

| Parameter | Value |
|---|---|
| Cars | 15 |
| Angle | 1.41999996 rad |
| Contact order | 0 |
| Heavy-car case | 1 |
| Wall-backed case | 1 |
| Longitudinal gap | 3.20000005 m |
| Inspected car | 14 |
| Expected lateral velocity | 0.03 |
| Observed lateral velocity | 0.0299867578 |
| Allowed absolute deviation | Less than 0.00001 |

The observed deviation is approximately 0.0000132422. This is an invariant
failure in the tested compiler/host configuration; its production cause has
not been established. Do not enlarge the tolerance or disable the test to
approve the baseline. Trace the contact normal, wall response and accumulated
correction before proposing a focused fix, retaining the momentum, energy,
separation and wall-blocking checks.

With ordinary installed SDL2 development headers, reproduce with
`make -j1 ai-drive-test`. The exact guarded local invocation was:

```sh
python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py \
  make -j1 ai-drive-test \
  'SDL_CFLAGS=-I/tmp/openug2-vita-reference/desktop-sdl/usr/include/SDL2 -I/tmp/openug2-vita-reference/desktop-sdl/usr/include/x86_64-linux-gnu -D_REENTRANT' \
  'SDL_LIBS=-l:libSDL2-2.0.so.0'
```

The clean reference at `/tmp/openug2-development-6c9dd09` used the additional
command-line value
`CFLAGS=-O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function`.
Both original failing logs, the diagnostic source and diagnostic output are
retained externally. No upstream issue has been opened or commented on for it.

## Same-pose comparison and preserved coverage

Stock 240SX, 960x544, heading 0 and a 1400 m radius were held constant. A clean
upstream discovery found each pose; the coordinates were frozen for the
candidate/reference audits and one-frame texture-audit captures. All 14 engine
commands exited zero. Elapsed resident-loader timings were excluded from count
comparison. This compares integration against the same upstream version on
one host; it is not the second independent machine required by issue #7.

| Region | Fixed XYZ | Instance / resident / PNG comparison | PNG SHA-256 |
|---|---|---|---|
| STREAML4RA | (-13.872,40.967,8.270) | PASS / PASS / byte-identical | `82a8948a979fb539937f812f32fff66c713ef6e3aba493c7d2017aedd402bade` |
| STREAML4RB | (490.582,396.154,-3.143) | PASS / PASS / byte-identical | `44bfedab83c5a757fdf7d27046552803fb139f780e3b9a5449d9fc93cbae01e2` |

The new captures differ from the older issue #7 frames, which belong to the
older renderer. They are saved in the new integration directory. The new
input fingerprint matches the earlier geometry/material input inventory:
`7f6d7feb8540bdc99358fb187b2926d7e5c50169f26c68ae463e3a9471372046`.

A bounded SHA-256 manifest records all 121 files in the two earlier coverage
report directories, including matrices, logs and captures. The final check
confirms they are unchanged and that the issue #7 branch still points to
`7e95be5`. No rows or acceptance claims have been carried into this new version
silently.

The comparison wrapper initially failed while finalizing metadata because of
an incorrect helper name, after all engine commands and comparisons passed.
Metadata was completed with the corrected helper without repeating the loads;
the initial runner log remains preserved.

## Host memory controls and remaining gate

All builds and tests ran sequentially. Normal/GLES/Vita/reference builds and
small tests used a 512 MiB kernel cgroup limit; debug used 768 MiB. Scope swap
was disabled. A monitor required the budget plus 4096 MiB available before each
phase and stopped work if host available RAM dropped below 4096 MiB. Each phase
had a 300-second outer timeout.

Real-data comparison used a 2048 MiB total scope cap, no swap, a sampled 1024
MiB child-group RSS ceiling, a 4096 MiB host reserve, and a 180-second timeout
per engine command. Sampled total scope usage peaked at 1135.0 MiB; maximum
observed child RSS was 576.3 MiB. Available host memory stayed above 8622.0 MiB
through that comparison. Small-test scope accounting reached the 512 MiB cap
without terminating a test; this is kernel-accounted memory, not process RSS.
Sampled peaks are observations, not guarantees about exact maximum allocation.

The native traffic-contact test must pass before baseline homologation. Preserve
this candidate and its evidence while resolving the failure in a focused change.
Issue #14 and later queue items remain unstarted. No issue update or PR is
published from this validation run.
