# OpenUG2 → PS Vita: audit and isolated bring-up

Audit date: 2026-10-05. Engine baseline: upstream `whoismept/OpenUG2`
`development`, `6c9dd091ee3297dea898c78764edf209657ce3d5`, also integrated
into `main` by `59c9263dd0299a6f41827caca6536005e4a32cf2`.
Work branch: `integration-development`. The original `vita-bringup` and issue #7
baseline remain preserved. No old Vita fork is used.

This baseline is pinned to a commit integrated into upstream `main`. Upstream
updates are reviewed and validated at planned integration points. Engine
contributions use separate issue branches and are checked against `development`
in a separate checkout before submitting a PR there. The branch policy is
documented in [ENGINE_WORKSTREAMS.md](ENGINE_WORKSTREAMS.md#branch-and-integration-policy).
Current integration validation is recorded in
[DEVELOPMENT_INTEGRATION.md](DEVELOPMENT_INTEGRATION.md); separate traffic and
warning fixes complete host validation of this baseline. The source inventory
now audits local integration `addbcbe`, including those fixes and the failed
texture-decode output cleanup. Physical Vita
acceptance remains pending. The initial bring-up
measurements below refer to `84ab8b3ff59bdbfad78a0bda24ac29dd52af9fc1`.

The product milestone remains a drivable 240SX in **one STREAML4RA region**,
with the existing WorldResident loader, original user-supplied PC data,
960×544 rendering at a target of 30 FPS and physics at 60 Hz. The first patch
is an isolated platform executable; it does not claim that milestone.

## Evidence and scope

The [complete API inventory](VITA_API_INVENTORY.md) records API, file, owning
function, source lines, Vita support, substitution and blocking status.
Regenerate the reviewed snapshot with
`python3 tools/vita_api_audit.py --revision 3f7536eba3ecfe6640e0115e57b0a48c49db53aa`.
The script's historical default remains available; this checked-in inventory
is pinned to the issue #14 review base so adding this bootstrap cannot change
the audited engine list.
It removes comments/literals and separately captures dynamically loaded GL
names. It includes conditional diagnostics inside production engine files;
it is a source superset, not a preprocessed list of calls executed each frame.

Production scope is the eleven `SRC` translation units in the root Makefile,
their shared headers and parser. Optional `OPENUG2_MENU`/`DEBUG_UI` first-party
code is inventoried separately. `third_party/imgui`, host Python tools, test
executables and preview applications are not production dependencies of Vita.
Build files, generated shaders and these optional paths were also inspected.

Counts: **82 direct GL names**, **five additional GL entry points loaded by
name**, **34 SDL names**, and **83 libc/POSIX/math/zlib names** in the engine
source superset. Optional menu/debug code adds fixed-function GL usage; do
not include those targets in the initial VPK.

Compatibility evidence is from SDK/source inspection and cross-compilation,
not from a physical Vita. Packages tested: VitaSDK core `2026.08.1-1`,
`sdl2_vitagl 2.32.8-1`, `vitaGL 0.0.0.r1488.g2bdbe89-1`, `vitaShaRK 1.7-1`,
`SceShaccCgExt 1.0.1-1`, `zlib 1.3.2-2` (hard-float `vita` world).
The SDK was installed outside the checkout for validation.

## Subsystem matrix

| Subsystem | Current implementation | Vita compatibility | Required work | Risk |
| --- | --- | --- | --- | --- |
| Build | host `sdl2-config`, `uname`, `-lGL`/`-lGLESv2`, xxd; one desktop output | no Vita cross/VPK target upstream | isolated VitaSDK CMake project called by `make vita`; use SDL imported static target | confirmed build blocker |
| Video/context | SDL window, GL context, 24-bit depth; GLES ES profile | requires SDL compiled with vitaGL backend | require `SDL_VIDEO_VITA_VGL`; compatibility 2.0 profile, one context owner, 960×544 | V0 device gate |
| Shaders | runtime GLSL 120/100, main and headlight programs; F3 GLSL 120 files | vitaGL translates GLSL through runtime compiler; exact programs untested | test small equivalent first, then unchanged main/headlight shader and limits | highest renderer gate |
| Renderer state | direct GL across render/main/hud/world/resident; uniform readback | `glGetUniformfv` is missing in tested vitaGL | retain owned state explicitly in small renderer patch | confirmed engine link blocker |
| Geometry | VBO/IBO, u16 indices, 36-byte interleaved world batches | entry points declared; indexed draws favorable | validate normalized U8 color, strides/offsets, state and driver lifetime | V1 gate; draw submission CPU |
| Textures | DXT1/3/5 preserved plus decoded RGB/alpha; RGBA fallback and mipmaps | vitaGL has compressed UBC texture path; dimensions/mips/alpha need device tests | measure source, decoded, GPU bytes and pool peaks; check extensions | high memory gate |
| Framebuffers | two 512² RGBA headlight maps + shared depth16 RBO | FBO entry points present, semantics untested | validate completeness, orientation, depth preservation, texture-unit restoration | medium; optional effect |
| Resources | `open/fstat/mmap/munmap`, directory discovery | `sys/mman.h` absent in tested SDK | preserve res_map/res_unmap ownership; add backend later; account for lost lazy paging | confirmed full-world blocker |
| Parser/file I/O | C99 header, `fopen/fread/fseek/ftell`, whole-file heap reads | libc APIs available | root `ux0:data/openug2`; check sizes/read errors, stack and allocation peaks | V2/storage gate |
| WorldResident | one SDL worker, atomics, join, main-thread incremental GL uploads | SDL implementations exist; no GL in worker | retain architecture, measure two residents/shared files/upload quotas | high stall/memory gate |
| Audio | SDL callback, 44100/S16SYS/mono/1024; decoded bank/sweeps or synth | Vita SDL driver exists; actual spec/latency untested | retain SDL, measure callback/conversion latency and heap; close device before freeing samples | medium |
| Timing | SDL ticks/performance counters; resident `clock_gettime` | monotonic clock is implemented in Vita newlib | keep APIs; verify errors/resolution and frame pacing | no API blocker |
| Physics/frame loop | one update per render iteration; fixed 1/60 constants and per-frame units | ARM-compatible arithmetic; cadence is coupled to rendering | later extract simulation tick and accumulator without changing equations | confirmed 30/60 design blocker |
| Input | keyboard drive/freecam; gamepad discovery and optional menu buttons | no normal keyboard; opening controller does not drive car | semantic controller input: stick, shoulders, Cross/Square/Triangle/Start | confirmed V5 blocker |
| AI/traffic | native C, nav routes, up to 16 traffic slots | no desktop-only APIs | retain existing quality controls; initial 4 traffic, later 2 racers; time AI/collision | CPU benchmark gate |
| HUD/diagnostics | textured HUD; provisional font draws a quad per lit pixel; ImGui GL2 optional | ordinary HUD GL path testable; ImGui is desktop dependency | log initially; avoid expensive diagnostic overlay skewing measurements | medium |

## Confirmed blockers versus unresolved tests

1. **Build/package path**: no upstream VitaSDK toolchain or SELF/VPK rules.
   The first patch supplies an independent target. Generic SDL2 for Vita can
   be GXM/Piglet/PVR; it is not sufficient to request OpenGL. Require the
   [sdl2_vitagl package](https://github.com/vitasdk/packages/blob/master/sdl2_vitagl/VITABUILD).
2. **File mapping**: `src/resource.c:res_map_file/res_unmap_file` require a
   header that the tested SDK lacks. A direct syntax probe fails on
   `#include <sys/mman.h>`. `res_map_file` promises lazy pages; replacing it
   with malloc+fread changes resident RAM behavior dramatically. Do not call
   that an equivalent optimization. Keep pairing and borrowed TPK pointers.
3. **Uniform readback**: `glGetUniformfv` is absent from both the installed
   vitaGL header and symbols. `render_world_glows`, `render_district_lights`
   and `render_wheel_mesh` use it to restore previous uniform values. The updated
   baseline also uses it in road reflections, car environment, rain and HUD.
   The minimal sustainable engine patch is a renderer state cache/setters for
   these values, not a stub returning zeros. Header availability of the other
   81 GL names does not prove matching semantics.
4. **Drive input**: `main` opens a controller but throttle/steering/handbrake
   use `SDL_GetKeyboardState`. Nitro and camera controls also need mapping.
   A backend mapping alone is not engine integration.
5. **Simulation cadence**: the loop has fixed per-frame movement/smoothing,
   counters and 1/60 suspension/audio steps. Swap interval 2 alone would slow
   simulation. Preserve a 60 Hz simulation tick before targeting 30 Hz draw.
6. **Vita launch/storage assumptions**: upstream defaults to a command-line
   data root/current directory, writable relative diagnostic outputs and a
   desktop window. Normal bubble launch must work without shell arguments.

Hardware gates remain: shader translation/compiler availability, original
texture mip/alpha rendering, FBO correctness, thread stack/peak memory,
storage throughput, shutdown, installation and actual frame times. They are
not proven incompatibilities. No on-device numbers have been invented.

## Renderer inventory and shader plan

GL families in production: program/shader compile/link/logs and attribute
bindings; scalar/vector/matrix uniforms; buffer creation/upload/deletion;
texture creation/compressed and uncompressed mip upload/filtering/deletion;
FBO/RBO creation/attachment/completeness/deletion; indexed draws and attribute
arrays; depth/cull/blend/color state; viewport/clear; readback/error/limit
queries. There are no engine VAO, compute, SSBO or instancing requirements.
`glPolygonMode` is a desktop diagnostic excluded by `N2_GLES`.

`src/render.c` owns GLSL `VS`/`FS` and `headlight_shadow_init`; the fragment
shader has dynamic material branches, samplers, matrices/arrays, pow/fog and
headlight shadow code. A simple shader success does not validate this program.
The existing compile helper logs failure but still returns a shader handle;
`render_program` does not check link status and retains shader objects. Harden
this generically when integrating the engine renderer. Do not silently switch
to Cg or strip effects before measuring the GLSL route.

`src/world_mesh.c:world_debug_program_load` tries relative external files then
xxd-embedded `world_debug_120.vert/.frag`. They stay GLSL 120 even in `make gles`;
the F3 path is a separate compatibility test, not essential to V0/V1.
GL is also exposed through `render.h` and called in main, hud, world and
resident. A complete opaque-handle renderer conversion would be large; defer it.

The [vitaGL source](https://github.com/Rinnegatamante/vitaGL/blob/2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8/source/custom_shaders.c)
contains the GLSL translation/compiler path. Its runtime compiler needs the
user's console setup described by the
[vitaGL project](https://github.com/Rinnegatamante/vitaGL/blob/master/README.md).
No compiler module is distributed here. If runtime compilation fails, first
record the full compile/link log; investigate precompiled shaders in the
backend only after that evidence.

The [SDL vitaGL backend](https://github.com/vitasdk/packages/blob/master/sdl2_vitagl/vitagl-backend.patch)
calls `vglInitExtended` when loading GL and `vglSwapBuffers` on SDL swap.
**Do not initialize or swap vitaGL twice.** Its current unload path does not
expose a global teardown call; the isolated platform closes its GL objects,
context/window and SDL, then returns from the process, which reclaims library
pools. Review this ownership again if upgrading SDL. No SDL_Renderer is
created alongside this context.

## Texture, framebuffer and memory audit

`nfsu2.h:N2Tex` can simultaneously own RGB (3 B/pixel), alpha (1 B/pixel),
compressed source blocks and temporary codec output. Keeping DXT does not
currently eliminate decoded CPU planes. Supported parser paths include DXT1,
DXT3, DXT5, paletted formats and compressed archive/texture codecs. The engine
upload chooses S3TC from extension detection in `main`, otherwise RGB/RGBA
plus `glGenerateMipmap`. RGBA interleaving adds another 4 B/pixel temporary.
Record base and mip bytes separately; a complete square chain adds about 1/3.
DXT1→RGBA base expansion is 8×, DXT3/5→RGBA is 4×. Small blocks/padding and
driver alignment change exact allocation sizes.

The tested [vitaGL texture implementation](https://github.com/Rinnegatamante/vitaGL/blob/2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8/source/textures.c)
routes DXT1/3/5 to UBC formats. Preserve the extension gate and validate actual
assets, non-square/NPOT sizes, repeat, short/full mip chains and alpha.
Do not pre-decompress every texture or invent a conversion cache now.

Existing extension assumptions:

- `GL_EXT_texture_compression_s3tc`/S3TC alternatives: compressed-upload gate.
- `GL_EXT_texture_filter_anisotropic`: maximum/filter query, default fallback 1×.
- `GL_EXT_timer_query`: `glGenQueries`, `glBeginQuery`, `glEndQuery`,
  `glGetQueryObjectuiv`, `glGetQueryObjectui64vEXT`, all loaded through SDL.
  These optional timers use `GL_TIME_ELAPSED`; API names/occlusion queries
  alone do not establish elapsed-time support.

Headlight targets nominally allocate two RGBA8888 512² images (2 MiB) and one
depth16 image (0.5 MiB), before pitch/alignment/GXM render-target costs.
Main restores framebuffer/renderbuffer, viewport, state and texture units
after this pass. `GL_DEPTH_COMPONENT16` is used; depth textures are not required.
Screenshots allocate two W×H×3 buffers plus PNG raw/compressed intermediates.
Disable captures in normal device measurements.

Large/dynamic allocation sites needing measurement:

| Site | Allocation/lifetime | Concern |
| --- | --- | --- |
| `n2_read_file`, `n2_load_car*` | full file + decoded mesh arrays, realloc scene | file bytes and decoded bytes overlap; error/size checks upstream are weak |
| `world_load_ex`, `WInstLibrary` build | regional/shared streams, prototypes, placed meshes, dedup/bboxes/nav/contact grids | loss of lazy mappings; libraries and placed data coexist |
| `world_resident_prepare`, activation | active + candidate resident plus shared caches, collision arrays | report peak before swap, not only final resident |
| `upload_scene` | up to 32 B/vertex CPU position/UV/normal + color/index arrays, temporary 12 B/vertex normals, GPU copies | per-mesh upload overhead and duplicated normals |
| `batch_emit` | `nv*36` vertex bytes + `ni*2` indices + per-mesh temporary normals | quota counts batches, not a strict upload millisecond deadline |
| `n2_dxt*`, TPK decode/upload | compressed + decoded RGB/alpha + RGBA staging + GPU mip storage | measure each plane and actual allocator pools |
| `audio_load_*` | whole bank/sweep plus decoded PCM | reserve budget after renderer/resident measurements |
| `main` static arrays | `LodGrp[24576]`, source mesh scratch[4096], hit arrays[4096/8192], key tables[16384] | BSS remains committed even if path is dormant |
| `main` stack; parser recursion | route arrays such as `rda_nav_path[4096]`, nested walkers and local scratch | compile stack-usage reports; do not assume desktop stack size |

512 MiB main RAM/128 MiB CDRAM are hardware capacities, not an application
allocation budget. vitaGL can use several memory pools and RAM fallback;
estimate texture/buffer bytes separately from real pool free/used values.
Do not add them as disjoint hardware totals or label estimates peak VRAM.

## Filesystem, threads, audio and timer assumptions

Keep `ux0:data/openug2/CARS`, `TRACKS`, `GLOBAL`, etc., with original names and
case. Format paths can contain retail backslashes, but engine open paths are
assembled with `/`. Do not `chdir` globally or alter parsers to prepend Vita
paths. The VPK has no retail FILE directives. Generated logs/caches belong in
the user's data directory, not `app0:`. Missing data must produce a diagnostic;
the bootstrap does not create a fake valid data root.

`resource`, `audio`, `ai` enumerate with `opendir/readdir/closedir` and
`scandir/alphasort`; enumeration
order is not guaranteed, and original upper-case filename filters matter.
`getenv` probes are development diagnostics, not a usable Vita settings UI.
Check fopen/fseek/ftell/fread failures and 32-bit `long` boundaries. Format
helpers use explicit byte reads/memcpy widely; float/math code still needs ARM
alignment and finite-value validation. C99 does not imply ARM runtime proof.

SDL workers/atomics/join have native Vita implementations. Preserve the single
loader guard because parser/world helpers have shared static scratch/state.
All GL uploads must remain on the context thread. Joins can block shutdown,
so measure pending preparation and failure/cancel behavior, and destroy shared
resources only after workers finish. Later inspect stack use and use SDL's
stack-size configuration if needed. Avoid introducing native threads now.

Audio owns its SDL callback and sample buffers; stop/close it before releasing
samples. The specified 44100 Hz/mono format may require driver conversion;
test underruns, latency and callback/main-thread shared state on hardware.
The initial bootstrap does not load music, banks or cinematics.

SDL performance counter uses process time at microsecond resolution on Vita.
[Vita newlib implements CLOCK_MONOTONIC](https://github.com/vitasdk/newlib/blob/vita/newlib/libc/sys/vita/clock.c).
There is no need to replace resident timers merely to compile the port.

## Smallest build/platform patch

Keep the root desktop source list intact. Add `make vita` as a wrapper around
an isolated `platform/vita/CMakeLists.txt` using VitaSDK's toolchain and
`vita_create_self`/`vita_create_vpk`. Query host SDL lazily so building Vita
does not require a host SDL development installation. VPK title id:
`OPENUG200` (development default, configurable); output under `build/vita`.

The isolated program initializes SDL video/events/joystick, requests the
vitaGL compatibility context, creates one checked GLSL program and draws a
synthetic textured triangle from VBO/IBO with `GL_UNSIGNED_SHORT`. It opens
the supplied 240SX GEOMETRY.BIN through the production parser and reports
selected stock mesh/vertex/index totals and distinct nonzero material hashes.
These are counts after parser stock/LOD selection, not every object in the file.
It reports missing/bad data independently of renderer success. This keeps
V0/V1 usable before the user installs retail data, and V2 independently testable.

It exposes semantic controller state for future integration. Start toggles
pause, Select+Start requests orderly exit, Cross retries the parser. Steering,
throttle, brake/reverse, handbrake, nitro/confirm and camera fields are mapped
but do not control a vehicle yet. The bootstrap has no physics/world loop.

Profiling is compiled on by default (`VITA_PROFILE=ON`): log FPS, CPU submission
time, paced frame time, draw calls, visible meshes, active textures, estimated
GPU resource bytes, free vitaGL pool bytes and parser load time/counts.
Resident build/upload and traffic are explicitly inactive; GPU execution time
and process-wide RAM usage are unavailable, not fabricated zero measurements.
`--vita-profile`, `--parser-only`, `--frames N` are bring-up switches. Bubble
launch needs none. Lower resolutions and engine quality changes are deferred.

Files for this patch:

- `Makefile`: isolated Vita wrapper, lazy host SDL queries and Linux C99 feature
  visibility (`_DEFAULT_SOURCE` for M_PI/scandir/alphasort).
- `platform/vita/CMakeLists.txt`: cross build/dependency guard/SELF/VPK rules.
- `platform/vita/main.c`, `platform.c/.h`: SDL/context lifetime, data root,
  logging, input and platform diagnostics.
- `platform/vita/renderer_vita.c/.h`: bounded shader/VBO/IBO/texture probe.
- `platform/vita/asset_probe.c/.h`: production car parser with bounded file read.
- `docs/VITA_PORT.md`, `docs/VITA_API_INVENTORY.md`, `tools/vita_api_audit.py`.
- `README.md`, `.gitignore`: discoverability and Vita artifact exclusion.
- `src/debugui.cpp`: expose GL extension prototypes for the Linux debug build;
  this matches the existing renderer header and changes no rendering behavior.

Next small engine integration patches, not implemented by this bootstrap:
`src/resource.c/.h` (mapping backend), `src/render.h` (single backend header),
`src/render.c` (owned uniform state and compile/link failures), `src/main.c`
(platform lifetime/input/simulation cadence/profile), possibly
`src/world_mesh.c` and debug shaders (diagnostic shader variant). Preserve
`physics.c`, asset layouts and WorldResident behavior; add counters later in
`world.c`, `world_resident.c/.h`, `render.c` at measured ownership boundaries.

## Build and validation

```sh
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
# Install matching SDK-world packages: sdl2_vitagl, vitaGL, vitaShaRK,
# SceShaccCgExt, libmathneon, taihen, zlib (SDL package carries dependencies).
make vita
# Optional diagnostic configuration, in its own CMake cache:
make vita VITA_PROFILE=OFF VITA_TITLEID=OPENUG200
```

`make vita` builds a **bring-up VPK**, not the desktop main loop or a playable
port. Use the SDK SDL2 CMake package so its Vita stub link dependencies are
preserved. The final link uses the C++ driver for vitaGL's GLSL translator;
all project source files remain C99. Shader-library dependencies are grouped
to resolve static link order. A plain SDL2 package is rejected at configure
time. Build output never goes into `nfsu2`, and `make`, `make gles`, `make debug`
retain their roles.

Initial bring-up validation (2026-10-05, engine `84ab8b3`):

- `make vita`: passed with the package versions above. Output:
  `build/vita/openug2-vita.vpk`, **1,148,702 bytes**. ELF is ARM32 little-endian,
  EABI5 hard-float. ZIP CRC check passed; its only entries are `eboot.bin`
  and `sce_sys/param.sfo`. This proves packaging, not installation.
- Profiling ON and a separate `VITA_PROFILE=OFF` build passed. Missing SDK,
  SDL without the vitaGL macro and invalid title ID fail with diagnostics.
- `make`, `make gles`, `make debug`, `make normal`: passed with extracted host
  SDL 2.30 development headers and the installed desktop SDL runtime. Linux
  feature/prototype fixes above were needed for existing upstream build issues.
  No retail-data desktop visual comparison was available.
- Existing targets passed: `car-material-test` (349 assertions),
  `world-group-test`, `world-instance-test`, `world-resident-test`,
  `world-render-test`, `district-collision-test`, `world-texture-test`,
  `wheel-render-test`, `headlight-render-test`, `resolution-cli-test`,
  `world-cli-test`.
- A temporary host harness reused the existing synthetic car fixture builder
  to run `vita_asset_probe` under AddressSanitizer/UndefinedBehaviorSanitizer:
  missing/empty file, one stock mesh (3 vertices/3 indices/1 material hash),
  truncated header and >32 MiB sparse file passed. No retail bytes were used.
- ARM `-fstack-usage -O2` reports 2,400 bytes for `n2_car_chunk` and 1,400 for
  `vita_asset_probe`, excluding callees/recursive ancestors. This is a static
  observation, not a measured peak-stack guarantee.
- All direct GL names except `glGetUniformfv` are declared and exported by
  the tested vitaGL archive. `git diff --check` passed.

At the original bootstrap validation, no physical device or install channel
was available. The user subsequently connected a Vita; Linux transport setup
and device execution are tracked in [VITA_DEVICE_TESTING.md](VITA_DEVICE_TESTING.md).
A first physical run has now verified installation, 960×544 display, the
indexed RGBA triangle, stock 240SX parser counts and orderly shutdown.
Controls/repeated runs and the remaining V1 shader/DXT/FBO probes are pending;
the full car/world renderer remains deferred. SDK/host headers and
validation logs are temporary files outside Git; the SDK remains available
for this workspace at `/tmp/openug2-vitasdk` until temporary files are removed.

The user subsequently supplied two PC CD images outside the repository.
Their data was assembled at `../nfsu2-data/game`; local provenance/checksums
and validation logs live under `../nfsu2-data/reports`. The host build of the
Vita parser probe decoded the original stock 240SX successfully. A GL-free
L4RA instance audit passed with finite coordinates and supported ROAD spawn;
the original GLOBALB material suite passed 396 checks. The desktop renderer
also produced a 960×544 240SX/L4RA capture outside Git. These are host checks,
not evidence of Vita runtime compatibility. See the external data folder's
README for commands and copying its contents to `ux0:data/openug2`.

## Device smoke test

For Linux USB/FTP deployment, checksums and log collection, follow
[VITA_DEVICE_TESTING.md](VITA_DEVICE_TESTING.md). The physical gates below
are updated only from collected console evidence.

1. Install `build/vita/openug2-vita.vpk` on the user's unlocked Vita, with the
   runtime compiler configured as described by vitaGL. Do not put it in Git.
2. Supply the user's PC data at `ux0:data/openug2` with original filenames;
   V2 needs `CARS/240SX/GEOMETRY.BIN`. The bootstrap uses a 32 MiB file-read
   guard and 128 MiB newlib heap reservation only for this diagnostic phase.
3. Launch the bubble. Inspect the textured orange/white triangle at 960×544.
   Green background means the stock parser succeeded; red means missing or
   failed geometry. Neither color means the car was rendered.
4. Read `ux0:data/openug2/vita-bringup.log`; with no root it falls back to
   `ux0:data/openug2-bringup.log`. Save compile/link errors and reported counts.
   Compare the parser with the host only using the same original data.
5. Exercise stick/shoulders/Cross/Square/Triangle input, Start pause and
   Select+Start exit. Confirm process exit and relaunch repeatedly, including
   absent data. Report crashes and pool/frame measurements before integrating
   the full car/world renderer. Installation and launch through VitaShell /
   LiveArea are manual; the Linux tool automates transfer and evidence collection.

## Ordered acceptance gates

| Gate | Required evidence | Current status |
| --- | --- | --- |
| V0 | VPK installs, SDL/vitaGL display and orderly exit on real Vita | first physical run verified; repeated runs pending |
| V1 | textured indexed triangle; then main shader, DXT and FBO probes | triangle visually verified on Vita; main shader/DXT/FBO deferred |
| V2 | own 240SX data opens and parser aggregate counts match desktop | physical stock parser matches host on SHA-256 verified input |
| V3 | full real 240SX mesh/material/texture/camera draw | deferred |
| V4 | one L4RA static resident; load/RAM/GPU/draw measurements | deferred |
| V5 | controls/60 Hz physics/collision; 30 Hz draw | deferred |
| V6 | existing moving resident; swap/failure/shutdown and worst hitch | deferred |
| V7 | four traffic cars, then two racers, scale using explicit settings | deferred |

Log resident CPU build/upload milliseconds, worst frame, active/candidate RAM,
GPU allocations, AI/collision time and draw calls at each applicable gate.
Optimize only from those measurements. Do not reduce resolution/traffic or
remove streaming in common code silently. Cache preprocessing, career/front
end, retail fidelity and a full renderer abstraction remain outside this phase.

No EA code, executable, models, textures, banks, saves or compiler modules are
included. Only synthetic probe data and original clean-room code belong in Git.
