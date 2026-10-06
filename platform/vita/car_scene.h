#ifndef OPENUG2_VITA_CAR_SCENE_H
#define OPENUG2_VITA_CAR_SCENE_H

#include <stddef.h>
#include <stdint.h>

#include "nfsu2.h"
#include "resource.h"

enum { CAR_SCENE_TEXTURE_KEY_CAP = 2048 };

/* CPU-only stock vehicle data. This is an owning, non-copyable value: its
 * ResFile owners point at `budget` in this same struct until released. Start
 * it as {0}; do not move/copy it while loaded. */
typedef struct {
    N2Scene scene;
    N2CarProfile profile;
    N2WheelAttr wheel;
    int stock_wheel;
    uint32_t texture_keys[CAR_SCENE_TEXTURE_KEY_CAP]; /* unique, referenced */
    int texture_key_count;
    uint32_t cpu_mesh_bytes; /* mesh table, vertex/color/normal/index arrays */
    uint32_t source_bytes;   /* source file lengths: geometry + local + global */
    ResBudget budget;
    ResFile texture_file;
    ResFile global_file;
    N2Tpk global_tpk;
    uint32_t initialized;
} CarScene;

/* Loads original 240SX GEOMETRY.BIN, TEXTURES.BIN and GLOBALB.BUN using an
 * explicit 32 MiB / 16 MiB file budget. Requires GLOBALB's exact wheel anchors.
 * `out` must be zero-initialized. On failure, all owners and parsed meshes are
 * released and `out` returns to zero. */
int car_scene_load(const char *root, CarScene *out,
                   char *error, size_t errorcap);

/* Decode one referenced texture by key. Caller owns the N2Tex planes on
 * success and frees rgb/alpha/dxt. Checks the local car pack first, then the
 * indexed GLOBALB TPK. Returns false and leaves `out` empty on failure. */
int car_scene_decode(const CarScene *scene, uint32_t texkey, N2Tex *out);

/* Call after the renderer has uploaded every needed texture. The parsed meshes
 * and metadata remain alive for drawing/sorting. */
void car_scene_release_files(CarScene *scene);
void car_scene_free(CarScene *scene);

#endif
