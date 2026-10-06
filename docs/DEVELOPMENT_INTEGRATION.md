# Development baseline integration — 2026-10-05

Branch: `integration-development`.

**Status: host baseline validation complete.** The inherited traffic-contact
failure and compiler warnings are resolved in separate changes. Full normal,
debug and GLES builds, 17 synthetic targets and three CLI targets pass with
zero warnings. L4RA/L4RB measurements and captures match the preserved upstream
reference. Physical Vita execution and issue #7's second machine remain
separate acceptance checks; this result does not certify either.

## Provenance and isolation

- Previous engine baseline: `84ab8b3ff59bdbfad78a0bda24ac29dd52af9fc1`.
- Previous local Vita work: `8315923`, retained on `vita-bringup`.
- Pinned upstream `development`: `6c9dd091ee3297dea898c78764edf209657ce3d5`.
- Upstream `main` already incorporates that development commit through
  `59c9263dd0299a6f41827caca6536005e4a32cf2`; their trees were identical at fetch.
- Candidate merge: `9eea351`, parents `8315923` and `6c9dd09`.
- Traffic fix: `2a84368`, integrated as `37aa070`.
- Warning/bounds fix: `762631c`, integrated as `622a199`.
- Issue #7 remains on `issue-7-stream-coverage` at `7e95be5` with its previous
  engine, executable, fixed-pose measurements and local captures.

The original merge had no textual conflicts. At that stage, relative to the
pinned upstream source, the candidate changed only `src/debugui.cpp` by defining
`GL_GLEXT_PROTOTYPES` before the SDL OpenGL header on Linux. The existing
Makefile compatibility fixes and isolated Vita target remain. Production
parser, world, physics, AI and renderer implementations matched upstream.
The subsequent wall-contact precision fix changes `src/physics.c` in isolation.
The warning cleanup adds bounded path/offset checks and diagnostic fixes, with
whitespace-only changes in AI, audio and the already fixed physics. Its renderer
changes split existing statements and mark unused GLES fallback arguments;
the GL rendering path is preserved. Both fixes originate in branches based
on the pinned engine without mixing the Vita bootstrap into those changes.

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

## Initial builds and regressions (before the contact fix)

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

The observed deviation is approximately 0.0000132422. This was the invariant
failure in the initial compiler/host configuration. The subsequent investigation
identified rounding in wall clipping and closest-point normal construction.
The fix below retains the tolerance and momentum, energy, separation and
wall-blocking checks. Initial failure logs are preserved as historical evidence.

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

## Focused wall-contact fix (before the warning cleanup)

The separate `fix-traffic-contact-regression` branch is based directly on
`6c9dd09`. Its commit `2a84368` was cherry-picked into this candidate as
`37aa070`. See [TRAFFIC_CONTACT_REGRESSION.md](TRAFFIC_CONTACT_REGRESSION.md)
for the cause, implementation and reproduction commands.

Height clipping and closest-point calculations now retain double precision
until the contact normal is formed. Public contact structures, source geometry
and vehicle state remain float. The explicit 15-car case fails with the original
physics and passes with the change, using the original `1e-5` tolerance.
The full traffic suite also passes. Additional district checks cover repeated
rotated-wall contacts, both windings and radial normals at actual endpoints.

The focused branch passed district, ground, instance and resident regressions,
ASan/UBSan traffic and district runs with leak detection, and compilation of
the physics translation unit using VitaSDK's ARM compiler. Physical Vita
execution and the CPU cost of double arithmetic remain unmeasured.

After cherry-picking, normal, debug and GLES builds and all five affected
regression targets passed. Normal and GLES warning-message multisets match
the initial reference (186 and 190 warnings respectively). Debug rebuilt only
the changed physics object and emitted 17 inherited warnings; this was an
incremental build, not a new full-build warning count.

Fresh validation uses a separate external directory:

```text
/home/luigi/Documents/luigi/nfsu2-data/reports/traffic-contact-integration-2026-10-05/
```

Both regions were measured again at the frozen poses. All six engine commands
passed; instance/resident fields match the preserved upstream reference and
the PNG captures remain byte-identical, with the SHA-256 values recorded above.
The data-input fingerprint is unchanged. The 121 historical coverage files,
issue #7 branch and its executable were verified unchanged again. The working
executable was restored to the newly built normal profile after debug/GLES.

The repeat used the same sequential controls: 512 MiB for normal/GLES/tests,
768 MiB for debug and 2048 MiB for the real-data scope, with swap disabled and
a 4096 MiB host reserve. Build/test scopes sampled at most 453.5 MiB and at
least 6472.6 MiB host available memory. The real-data scope sampled 1124.1 MiB
and at least 7055.0 MiB host available memory, with the additional 1024 MiB
process-group RSS guard. Exact observations are in the external reports.

At that stage, compiler warnings remained the acceptance task. That historical
evidence is preserved; the following validation resolves the warning gate.

## Warning cleanup and final host validation

The `fix-compiler-warnings` branch contains commit `762631c`, based on
`2a84368`. It was cherry-picked into this integration as `622a199` without
conflicts. [COMPILER_WARNINGS.md](COMPILER_WARNINGS.md) records the changes and
reproduction commands.

The final code was checked with `-Wall -Wextra -Werror` and the existing
unused-static-parser-function exception. No new warning category was disabled.
Debug rebuilt all C and C++ objects, including ImGui and its backends.

| Final check | Result |
|---|---|
| Full normal / debug / GLES builds | PASS / PASS / PASS; zero warnings |
| Synthetic regression targets | 17/17 PASS; zero warnings |
| Car-material/parser assertions | 359 passed, zero failed |
| World / resolution / AI CLI targets | 3/3 PASS |
| Resource boundary fixture, ASan/UBSan with leak detection | PASS; fails its terminator assertion with original resource code |
| Full car-material/parser suite, ASan/UBSan with leak detection | PASS |
| Physics / world-instance Vita ARM translation units | PASS / PASS with warnings treated as errors |
| Isolated Vita bootstrap VPK | PASS; zero warnings, package CRC/contents checked |
| L4RA/L4RB comparison to preserved upstream reference | PASS; counts equal, captures byte-identical |

The circuit-name fix includes the terminator for a name exactly 255 bytes long
and rejects longer paths. The ARM compiler also exposed signed/unsigned offset
arithmetic: texture bounds now use remaining file bytes, and mesh auditing
rejects oversized chunks before payload access. New fixtures cover these cases
and the signed-long chunk boundary. Leak detection found fixture-owned texture
block tables; all 17 tables used by those tests are now freed after decoding.

The source API inventory was refreshed for local integration revision
`622a1996cbb982fe81fb3afe90572dccb716c242`. It has 82 direct GL, five indirect GL,
34 SDL and 82 libc/POSIX/math/zlib names in production. `strncpy` was removed
from that last set. The only missing direct vitaGL dependency remains
`glGetUniformfv`. This is still an API inventory, not the #14 ownership/API
design deliverable.

Final evidence is isolated from both previous validation runs:

```text
/home/luigi/Documents/luigi/nfsu2-data/reports/compiler-warnings-2026-10-05/
/home/luigi/Documents/luigi/nfsu2-data/reports/compiler-warnings-integration-2026-10-05/
```

Normal/GLES/tests used 512 MiB scope caps, full debug 768 MiB, with swap disabled
and a 4096 MiB host reserve. Across the 21 final build/test phases, sampled
scope memory peaked at 462.9 MiB and host available memory stayed above
8184.0 MiB. The six real-data commands used the established 2048 MiB total
scope and 1024 MiB process-group RSS caps: the scope sampled 1123.4 MiB and
host available memory stayed above 7975.0 MiB. Input fingerprints and both
PNG hashes still match the values above; the normal executable was restored.
The isolated VPK was also rebuilt under a 512 MiB cap without warnings; its
only entries are `eboot.bin` and `sce_sys/param.sfo`. All 121 historical coverage
files, the issue #7 branch/executable and the original Vita branch were verified
unchanged again. The refreshed API inventory reproduces exactly from its
recorded revision.

This completes host validation of the pinned development integration plus
the two focused fixes. The original Vita and issue #7 branches retain their
previous commits and evidence. Issue #14 is the next queue item and remains
unstarted. No public comment or PR accompanies this validation.
