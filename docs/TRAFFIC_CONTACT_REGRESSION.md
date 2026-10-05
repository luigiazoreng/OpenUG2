# Rotated traffic queue: wall-contact precision

Branch: `fix-traffic-contact-regression`, based on upstream `development`
`6c9dd091ee3297dea898c78764edf209657ce3d5`.

## Failure and fix

On Linux x86_64 with GCC 13.3.0, the original `ai-drive-test` fails its
unchanged `1e-5` tangential-velocity assertion in a wall-backed queue of 15
cars at `2 * .71f` radians, with a heavy car and a 3.2 m longitudinal gap.
Car 14 retains 0.0299867578 rather than 0.03 velocity along the wall.

The wall feature finder clips triangles to the car's height band, then
forms a radial normal from the closest wall/body points. Keeping those
world-space points in float introduces a small normal error. Repeated
removal of inward velocity then also removes part of the tangential velocity.
Clipped endpoints require the same precision as original triangle edges.

The fix retains double precision through height clipping, closest-point
selection and radial-normal construction, converting the final contact to
float. Mesh storage, public contact structures and vehicle state remain float.
There is no new persistent allocation. Existing contact filters, source
triangle IDs, winding fallback and radial normals at real corners remain.

The failing queue is invoked first in `ai-drive-test` so its parameters are
explicit. All existing combinations and momentum, energy, separation,
support and wall checks remain, with their original tolerances. A district
fixture additionally exercises 1,000 repeated contacts for three rotations
and both windings, plus radial normals at wall endpoints. That district
fixture also passes the original physics; the traffic queue supplies the
fail-before/pass-after regression evidence.

## Validation

| Check | Result |
|---|---|
| Original upstream AI/contact suite | FAIL at tangential-velocity assertion |
| Updated AI/contact suite | PASS, original tolerances |
| District collision and ground motion | PASS |
| World instance and resident fixtures | PASS |
| AI/contact suite with ASan/UBSan and leak detection | PASS |
| District suite with ASan/UBSan and leak detection | PASS |
| Physics translation unit with VitaSDK ARM compiler | PASS |

With installed SDL2 development headers, run `make -j1 ai-drive-test
district-collision-test ground-motion-test world-instance-test
world-resident-test`. The local checkout uses staged headers outside Git:

```sh
make -j1 ai-drive-test district-collision-test ground-motion-test \
  world-instance-test world-resident-test \
  'CFLAGS=-O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-function' \
  'SDL_CFLAGS=-I/tmp/openug2-vita-reference/desktop-sdl/usr/include/SDL2 -I/tmp/openug2-vita-reference/desktop-sdl/usr/include/x86_64-linux-gnu -D_REENTRANT' \
  'SDL_LIBS=-l:libSDL2-2.0.so.0'
```

Logs, temporary diagnostic sources, exact sanitizer/compiler argv and memory
records are retained outside the repository in
`/home/luigi/Documents/luigi/nfsu2-data/reports/traffic-contact-regression-2026-10-05/`.
The diagnostic variants are not production changes.

All phases ran sequentially under kernel memory caps of 512 MiB (768 MiB
for the AI sanitizer build), with swap disabled and a 4096 MiB host reserve.
The sanitizer phases sampled at most 306.3 MiB of cgroup memory and at least
7653.2 MiB of host available memory. These are sampled observations.

ARM compilation establishes source compatibility, not physical Vita runtime
or CPU performance. The extra double arithmetic must be timed on the console.
Inherited compiler warnings remain a separate acceptance task; this change
does not establish warning-free builds. No public issue update or PR has
been published for this change.
