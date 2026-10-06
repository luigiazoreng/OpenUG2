# CPU car scene loader

`platform/vita/car_scene.c` loads the original stock 240SX from
`CARS/240SX/GEOMETRY.BIN`, `CARS/240SX/TEXTURES.BIN` and
`GLOBAL/GLOBALB.BUN`. It passes the real local and global texture-key catalogues
to the shared `n2_load_car_colored` parser, then prepares every authored wheel
slice with `n2_car_prepare_wheels`. Axle and track coordinates must come from
the validated 240SX GLOBALB record; the loader fails if that source is absent.
There is no track/map load and no OpenGL, SDL, or Vita header dependency.

Create `CarScene` as `{0}` and keep it at a stable address while loaded. Its
budget is embedded in the scene, and the owned `ResFile` allocations refer to
that exact budget object. `car_scene_load` fills it transactionally and leaves
it zero on failure. The budget permits 32 MiB of simultaneous file buffers and
16 MiB per file. The geometry file closes after parsing, wheel preparation,
profile measurement, and mesh accounting. The local and GLOBALB texture source
buffers plus the GLOBALB TPK index remain until all needed textures have been
uploaded; call `car_scene_release_files` then. CPU meshes stay resident for
drawing and glass sorting until `car_scene_free`.

`texture_keys` contains unique nonzero keys referenced by the parsed scene.
`car_scene_decode` searches the car pack first and then the indexed GLOBALB
pack. The loader verifies every referenced key exists in one of those packs
and decodes each once before returning, so a broken retail key is an explicit
load failure instead of an invisible renderer fallback. `source_bytes` is the
sum of the three original file lengths. `cpu_mesh_bytes` includes the allocated
mesh table and each CPU vertex/normal/color/index array; decoded texture memory
and GPU allocations are outside that counter and the file-buffer budget.

The GL-free host test parses the real local files with the combined texture-key
catalogue, checks 101 meshes, 30,699 vertices, 18,615 indices, and 24 distinct
material hashes, verifies GLOBALB axle/track data, decodes all 7 distinct
referenced texture keys (4 local and 3 GLOBALB), checks release accounting,
and exercises missing-file and over-limit transactional failures. The counts
include the key-aware parser path used by the viewer; the probe path without a
texture-key catalogue reports different vertex/index totals. Its fixture has
no renderer or display path.
