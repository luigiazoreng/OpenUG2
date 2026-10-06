#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "nfsu2.h"
#include "resource.h"
#include "car_scene.h"

#define CAR_SCENE_MAGIC UINT32_C(0x4353434e)
#define CAR_SCENE_TOTAL_BUDGET (UINT32_C(32) * UINT32_C(1048576))
#define CAR_SCENE_FILE_BUDGET (UINT32_C(16) * UINT32_C(1048576))

static void scene_error(char *error, size_t cap, const char *path,
                        const char *reason) {
    if (error && cap) snprintf(error, cap, "%s: %s", path ? path : "car scene", reason);
}

static int scene_path(char *out, size_t cap, const char *root,
                      const char *suffix, char *error, size_t errorcap) {
    int n = snprintf(out, cap, "%s/%s", root, suffix);
    if (n < 0 || (size_t)n >= cap) {
        scene_error(error, errorcap, suffix, "path is too long");
        return 0;
    }
    return 1;
}

static int contains_key(const uint32_t *keys, int count, uint32_t key) {
    for (int i = 0; i < count; i++) if (keys[i] == key) return 1;
    return 0;
}

static int add_referenced_key(CarScene *scene, uint32_t key) {
    if (!key || contains_key(scene->texture_keys, scene->texture_key_count, key)) return 1;
    if (scene->texture_key_count >= CAR_SCENE_TEXTURE_KEY_CAP) return 0;
    scene->texture_keys[scene->texture_key_count++] = key;
    return 1;
}

static int mesh_memory_bytes(const N2Scene *scene, uint32_t *out) {
    uint64_t bytes = (uint64_t)(scene->cap > 0 ? scene->cap : 0) * sizeof(N2Mesh);
    for (int i = 0; i < scene->count; i++) {
        const N2Mesh *mesh = scene->meshes + i;
        uint64_t stride = mesh->authored_normals ? 8u : 5u;
        bytes += (uint64_t)(mesh->nverts > 0 ? mesh->nverts : 0) * stride * sizeof(float);
        bytes += (uint64_t)(mesh->nidx > 0 ? mesh->nidx : 0) * sizeof(uint16_t);
        if (mesh->vcol)
            bytes += (uint64_t)(mesh->nverts > 0 ? mesh->nverts : 0) * 4u;
        if (bytes > UINT32_MAX) return 0;
    }
    *out = (uint32_t)bytes;
    return 1;
}

static int source_byte_sum(long geometry, long textures, long global,
                           uint32_t *out) {
    if (geometry <= 0 || textures <= 0 || global <= 0) return 0;
    uint64_t bytes = (uint64_t)geometry + (uint64_t)textures + (uint64_t)global;
    if (bytes > UINT32_MAX) return 0;
    *out = (uint32_t)bytes;
    return 1;
}

static void free_decoded(N2Tex *tex) {
    free(tex->rgb);
    free(tex->alpha);
    free(tex->dxt);
    memset(tex, 0, sizeof *tex);
}

int car_scene_decode(const CarScene *scene, uint32_t texkey, N2Tex *out) {
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    if (!scene || scene->initialized != CAR_SCENE_MAGIC || !texkey ||
        !scene->texture_file.data || !scene->global_file.data ||
        !contains_key(scene->texture_keys, scene->texture_key_count, texkey)) return 0;
    if (n2_load_car_tex_by_key(scene->texture_file.data,
                               scene->texture_file.len, texkey, out)) return 1;
    if (n2_tpk_decode(scene->global_file.data, scene->global_file.len,
                      scene->global_tpk, texkey, out)) return 1;
    memset(out, 0, sizeof *out);
    return 0;
}

void car_scene_release_files(CarScene *scene) {
    if (!scene || scene->initialized != CAR_SCENE_MAGIC) return;
    free(scene->global_tpk.blk);
    memset(&scene->global_tpk, 0, sizeof scene->global_tpk);
    res_file_close(&scene->texture_file);
    res_file_close(&scene->global_file);
}

void car_scene_free(CarScene *scene) {
    if (!scene) return;
    if (scene->initialized == CAR_SCENE_MAGIC) {
        n2_free_scene(&scene->scene);
        car_scene_release_files(scene);
    }
    memset(scene, 0, sizeof *scene);
}

int car_scene_load(const char *root, CarScene *out,
                   char *error, size_t errorcap) {
    char geometry_path[1024], texture_path[1024], global_path[1024];
    char local_error[256];
    ResFile geometry_file = {0};
    uint32_t lookup_keys[CAR_SCENE_TEXTURE_KEY_CAP];
    int lookup_count = 0;
    int ok = 0;

    if (error && errorcap) error[0] = 0;
    if (!root || !out) {
        scene_error(error, errorcap, root, "invalid argument");
        return 0;
    }
    /* Owners record the address of out->budget; never build a shallow temporary
       CarScene and copy it to the caller. The caller passes a zeroed output. */
    if (out->initialized || out->scene.meshes || out->scene.count ||
        out->texture_file.data || out->global_file.data || out->budget.live) {
        scene_error(error, errorcap, root, "output scene is not empty");
        return 0;
    }
    if (!res_budget_init(&out->budget, CAR_SCENE_TOTAL_BUDGET, CAR_SCENE_FILE_BUDGET)) {
        scene_error(error, errorcap, root, "could not initialize file budget");
        return 0;
    }
    out->initialized = CAR_SCENE_MAGIC;
    out->stock_wheel = -1;
    if (!scene_path(geometry_path, sizeof geometry_path, root,
                    "CARS/240SX/GEOMETRY.BIN", error, errorcap) ||
        !scene_path(texture_path, sizeof texture_path, root,
                    "CARS/240SX/TEXTURES.BIN", error, errorcap) ||
        !scene_path(global_path, sizeof global_path, root,
                    "GLOBAL/GLOBALB.BUN", error, errorcap)) goto fail;

    if (!res_file_open(geometry_path, &out->budget, &geometry_file,
                       local_error, sizeof local_error)) {
        scene_error(error, errorcap, geometry_path, local_error);
        goto fail;
    }
    if (!res_file_open(texture_path, &out->budget, &out->texture_file,
                       local_error, sizeof local_error)) {
        scene_error(error, errorcap, texture_path, local_error);
        goto fail;
    }
    if (!res_file_open(global_path, &out->budget, &out->global_file,
                       local_error, sizeof local_error)) {
        scene_error(error, errorcap, global_path, local_error);
        goto fail;
    }
    if (!source_byte_sum(geometry_file.len, out->texture_file.len,
                         out->global_file.len, &out->source_bytes)) {
        scene_error(error, errorcap, root, "source byte count overflows");
        goto fail;
    }

    out->global_tpk = n2_tpk_open(out->global_file.data, out->global_file.len);
    if (!out->global_tpk.blk || out->global_tpk.nblk <= 0) {
        scene_error(error, errorcap, global_path, "GLOBALB has no indexed texture pack");
        goto fail;
    }
    int local_count = n2_car_tex_keys(out->texture_file.data,
                                      out->texture_file.len,
                                      lookup_keys, CAR_SCENE_TEXTURE_KEY_CAP);
    if (local_count <= 0) {
        scene_error(error, errorcap, texture_path, "no local car texture entries");
        goto fail;
    }
    lookup_count = local_count;
    lookup_count += n2_tpk_keys(out->global_file.data, out->global_tpk,
                                lookup_keys + lookup_count,
                                CAR_SCENE_TEXTURE_KEY_CAP - lookup_count);

    if (n2_load_car_colored(geometry_file.data, geometry_file.len, &out->scene,
                            lookup_keys, lookup_count, NULL, 0) <= 0) {
        scene_error(error, errorcap, geometry_path, "parser emitted no car meshes");
        goto fail;
    }
    if (!n2_global_wheel_attr(out->global_file.data, out->global_file.len,
                              "240SX", &out->wheel)) {
        scene_error(error, errorcap, global_path,
                    "240SX factory axle/track record is missing or invalid");
        goto fail;
    }
    out->stock_wheel = n2_car_prepare_wheels(&out->scene);
    if (out->stock_wheel < 0 || out->stock_wheel >= out->scene.count ||
        out->scene.meshes[out->stock_wheel].cat != N2_CAR_TIRE ||
        out->scene.meshes[out->stock_wheel].car_mount != N2_MOUNT_WHEEL) {
        scene_error(error, errorcap, geometry_path, "authored stock wheel tier is missing");
        goto fail;
    }
    /* These are the existing engine's car-box ratios for diagnostic profile
       fields. The actual scene axle and track placement above must come from
       GLOBALB; no wheel-position fallback is applied here. */
    n2_car_profile(&out->scene, "240SX", 0.639f, 0.597f, 0.760f,
                   n2_car_brake_radius(geometry_file.data, 0, geometry_file.len),
                   &out->profile);

    for (int i = 0; i < out->scene.count; i++) {
        uint32_t key = out->scene.meshes[i].texkey;
        if (!key) continue; /* authored material-only surfaces have no diffuse */
        if (!contains_key(lookup_keys, lookup_count, key)) {
            snprintf(local_error, sizeof local_error,
                     "referenced texture key %08x is absent from both TPKs", key);
            scene_error(error, errorcap, geometry_path, local_error);
            goto fail;
        }
        if (!add_referenced_key(out, key)) {
            scene_error(error, errorcap, geometry_path, "too many distinct referenced textures");
            goto fail;
        }
    }
    if (out->texture_key_count <= 0) {
        scene_error(error, errorcap, geometry_path, "car has no referenced texture keys");
        goto fail;
    }
    /* Validate every referenced texture now. No silent white/dark substitute
       hides a broken retail key; the renderer can decode these same keys later. */
    for (int i = 0; i < out->texture_key_count; i++) {
        N2Tex tex = {0};
        if (!car_scene_decode(out, out->texture_keys[i], &tex)) {
            snprintf(local_error, sizeof local_error,
                     "referenced texture key %08x cannot be decoded",
                     out->texture_keys[i]);
            scene_error(error, errorcap, texture_path, local_error);
            free_decoded(&tex);
            goto fail;
        }
        free_decoded(&tex);
    }
    if (!mesh_memory_bytes(&out->scene, &out->cpu_mesh_bytes)) {
        scene_error(error, errorcap, geometry_path, "CPU mesh byte count overflows");
        goto fail;
    }
    res_file_close(&geometry_file);
    ok = 1;

fail:
    res_file_close(&geometry_file);
    if (!ok) car_scene_free(out);
    return ok;
}
