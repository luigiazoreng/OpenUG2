# Production shader and synthetic GPU probes

Branch `vita-gpu-probes` links the existing production `render.c` and the
reviewed uniform mirror, without the world/game loop. Probes use tiny synthetic
textures and an 8x8 color/depth framebuffer. No retail assets are embedded.

`tools/vita_gpu_probe_test.c` runs the same implementation on a host GL/GLES
context. Required checks: production shader link, array upload and indexed
array update, depth occlusion, RGBA alpha, DXT1/3/5 decoded fallback, DXT1
transparent selectors, explicit mip-level sampling and mip generation.
Native S3TC upload/readback is tested separately when the extension is advertised;
unsupported native compression does not turn its fallback check into a failure.
Readbacks allow eight channel values of rounding error; no pixel is accepted
just because `glGetError` returns zero. All probe objects are destroyed.

Private logs: `/home/luigi/Documents/luigi/nfsu2-data/reports/vita-gpu-probes-2026-10-06/`.
The host final run passes all fifteen probes in both desktop GL and GLES.
Each guarded build/run has a 512 MiB cgroup cap, zero swap and 4 GiB reserve.
These are host results. Console results remain pending.

## Pinned vitaGL dependency

Installed SDK package: `vitaGL-0.0.0.r1488.g2bdbe89-1`. The default archive lacks
indexed-uniform compliance. A separate archive uses exact source
`2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8`, the package recipe's `NO_DEBUG=1`
and additional `SAFE_UNIFORMS=1`. The installed SDK library/header are unchanged.
Archive path: `/tmp/openug2-vitagl-safe-uniforms/stage/arm-vita-eabi/lib/libvitaGL.a`.
SHA-256: `fc34e03f8e6ffc4f465228139ec22aa31365496148a9d89106d4508648bd9e6c`.
Source, recipe, build argv, disassembly, hashes and a recoverable dependency
bundle are under `nfsu2-data/reports/vitagl-safe-uniforms-2026-10-06/`.

The target uses a library search directory before the installed SDK so SDL and
engine calls resolve through the same archive. The selected path must name
`libvitaGL.a`; do not mix the two libraries. Build example:

```sh
VITASDK=/tmp/openug2-vitasdk PATH=/tmp/openug2-vitasdk/bin:$PATH \
CFLAGS=-Werror CXXFLAGS=-Werror CMAKE_BUILD_PARALLEL_LEVEL=1 \
make -j1 vita VITA_GPU_PROBES=ON VITA_BUILD_DIR=build/vita-gpu-fresh \
VITA_GL_LIBRARY=/tmp/openug2-vitagl-safe-uniforms/stage/arm-vita-eabi/lib/libvitaGL.a
```

In this pinned library, strict locations encode `(program-1, uniform, offset)`.
Fragment uniform zero in program 1 encodes location zero, which its setters
ignore. The private Vita platform therefore reserves one unlinked program slot
until context teardown; production/probe programs cannot acquire that slot.
The context owns and releases this guard. No synthetic location or driver
readback stub is introduced in common code. Refer to vitaGL's
[uniform implementation](https://github.com/Rinnegatamante/vitaGL/blob/2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8/source/custom_shaders.c)
and [SAFE_UNIFORMS build option](https://github.com/Rinnegatamante/vitaGL/blob/2bdbe895ddc6a7e410a9ea55a8f53d8173c61ad8/Makefile).

## Other integration details

Main shader compile/link failures now stop with diagnostics and release shader
objects instead of attaching failed shaders or retaining compiled objects for
process lifetime. Successful shader sources, draw ordering and arithmetic are
unchanged. Optional dithering state is preserved only when its GL header
exposes `GL_DITHER`; vitaGL does not. Host GL/GLES retain their previous path.
The CPU font lookup exposes its existing immutable bitmap so the static viewer
can batch its caption; existing engine text drawing is unchanged.

The first fresh Vita compile exposed the missing dithering constant and is
retained as a failed log. The next fresh directory compiled all probe and engine
translation units with zero warnings. Later inspection found that the SDK
toolchain replaces environment `CFLAGS`; requesting `-Werror` there did not
enforce it. CMake now sets `-Werror` directly on the target. The stock-car
viewer final build confirms that flag with zero warnings. Its final
package is built again from its complete source in its own fresh directory.
Do not install the intermediate probe VPK as the item-5 car package.
