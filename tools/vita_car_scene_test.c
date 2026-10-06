#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "car_scene.h"

#define EXPECT_GEOMETRY_BYTES 7675600L
#define EXPECT_TEXTURE_BYTES 940480L
#define EXPECT_GLOBAL_BYTES 8008064L
#define EXPECT_MESH_COUNT 101
#define EXPECT_VERTEX_COUNT 30699
#define EXPECT_INDEX_COUNT 18615
#define EXPECT_TEXTURE_KEYS 7
#define EXPECT_MATERIAL_HASHES 24
#define TEST_FILE_LIMIT (UINT32_C(16) * UINT32_C(1048576))

static void path_join(char *out, size_t cap, const char *a, const char *b) {
    int n = snprintf(out, cap, "%s/%s", a, b);
    assert(n >= 0 && (size_t)n < cap);
}

static void assert_empty(const CarScene *scene) {
    assert(!scene->initialized && !scene->scene.meshes && !scene->scene.count &&
           !scene->texture_file.data && !scene->global_file.data &&
           !scene->budget.limit && !scene->budget.used && !scene->budget.live &&
           !scene->texture_key_count && !scene->cpu_mesh_bytes && !scene->source_bytes);
}

static void test_failure_fixtures(void) {
    char base[] = "/tmp/openug2-car-scene-errors-XXXXXX";
    assert(mkdtemp(base));
    char root[512], cars[512], car[512], geometry[512];
    path_join(root, sizeof root, base, "missing");
    assert(mkdir(root, 0700) == 0);
    CarScene scene = {0}; char error[512];
    assert(!car_scene_load(root, &scene, error, sizeof error));
    assert(strstr(error, "GEOMETRY.BIN") && strstr(error, "open failed"));
    assert_empty(&scene);
    assert(rmdir(root) == 0);

    path_join(root, sizeof root, base, "oversized");
    path_join(cars, sizeof cars, root, "CARS");
    path_join(car, sizeof car, cars, "240SX");
    assert(mkdir(root, 0700) == 0 && mkdir(cars, 0700) == 0 && mkdir(car, 0700) == 0);
    path_join(geometry, sizeof geometry, car, "GEOMETRY.BIN");
    int fd = creat(geometry, 0600); assert(fd >= 0);
    assert(ftruncate(fd, (off_t)TEST_FILE_LIMIT + 1) == 0);
    assert(close(fd) == 0);
    assert(!car_scene_load(root, &scene, error, sizeof error));
    assert(strstr(error, "per-file limit") && strstr(error, "GEOMETRY.BIN"));
    assert_empty(&scene);
    assert(unlink(geometry) == 0 && rmdir(car) == 0 && rmdir(cars) == 0 && rmdir(root) == 0);
    assert(rmdir(base) == 0);
    puts("vita_car_scene_test: PASS (missing-file and over-limit transactions)");
}

static void test_real_car(const char *root) {
    char geometry[1024], textures[1024], global[1024];
    path_join(geometry, sizeof geometry, root, "CARS/240SX/GEOMETRY.BIN");
    path_join(textures, sizeof textures, root, "CARS/240SX/TEXTURES.BIN");
    path_join(global, sizeof global, root, "GLOBAL/GLOBALB.BUN");
    struct stat st_geometry, st_textures, st_global;
    assert(stat(geometry, &st_geometry) == 0 && st_geometry.st_size == EXPECT_GEOMETRY_BYTES);
    assert(stat(textures, &st_textures) == 0 && st_textures.st_size == EXPECT_TEXTURE_BYTES);
    assert(stat(global, &st_global) == 0 && st_global.st_size == EXPECT_GLOBAL_BYTES);

    CarScene scene = {0}; char error[512];
    if (!car_scene_load(root, &scene, error, sizeof error)) {
        fprintf(stderr, "car_scene_load failed: %s\n", error);
        abort();
    }
    uint64_t vertices = 0, indices = 0;
    int tire_slices = 0, wheel_mounted_slices = 0, front_brake_slices = 0,
        rear_brake_slices = 0, material_hashes = 0;
    for (int i = 0; i < scene.scene.count; i++) {
        const N2Mesh *mesh = scene.scene.meshes + i;
        vertices += (uint64_t)mesh->nverts;
        indices += (uint64_t)mesh->nidx;
        if (mesh->cat == N2_CAR_TIRE) tire_slices++;
        if (mesh->car_mount == N2_MOUNT_WHEEL) wheel_mounted_slices++;
        if (mesh->car_mount == N2_MOUNT_FRONT_BRAKE) front_brake_slices++;
        if (mesh->car_mount == N2_MOUNT_REAR_BRAKE) rear_brake_slices++;
        if (mesh->car_material) {
            int seen = 0;
            for (int j = 0; j < i; j++)
                if (scene.scene.meshes[j].car_material == mesh->car_material) seen = 1;
            if (!seen) material_hashes++;
        }
    }
    assert(scene.scene.count == EXPECT_MESH_COUNT);
    assert(vertices == EXPECT_VERTEX_COUNT && indices == EXPECT_INDEX_COUNT);
    assert(scene.texture_key_count == EXPECT_TEXTURE_KEYS);
    assert(material_hashes == EXPECT_MATERIAL_HASHES);
    assert(scene.stock_wheel >= 0 && scene.stock_wheel < scene.scene.count);
    assert(scene.scene.meshes[scene.stock_wheel].cat == N2_CAR_TIRE);
    assert(scene.scene.meshes[scene.stock_wheel].car_mount == N2_MOUNT_WHEEL);
    assert(tire_slices > 0 && wheel_mounted_slices > 0 &&
           front_brake_slices > 0 && rear_brake_slices > 0);
    assert(scene.profile.has_tire && scene.profile.wheel_r > 0.1f && scene.profile.wheel_w > 0.1f);
    assert(scene.wheel.front_axle > 0.3f && scene.wheel.rear_axle < -0.3f &&
           scene.wheel.front_track > 0.8f && scene.wheel.rear_track > 0.8f);
    assert(scene.source_bytes == (uint32_t)(EXPECT_GEOMETRY_BYTES + EXPECT_TEXTURE_BYTES + EXPECT_GLOBAL_BYTES));
    assert(scene.cpu_mesh_bytes > 0);

    ResBudgetStats resident;
    res_budget_snapshot(&scene.budget, &resident);
    assert(resident.live == 2 && resident.used > EXPECT_TEXTURE_BYTES + EXPECT_GLOBAL_BYTES &&
           resident.used < resident.limit && resident.per_file == TEST_FILE_LIMIT);

    int local_textures = 0, global_textures = 0;
    for (int i = 0; i < scene.texture_key_count; i++) {
        N2Tex tex = {0};
        if (n2_load_car_tex_by_key(scene.texture_file.data, scene.texture_file.len,
                                   scene.texture_keys[i], &tex)) {
            local_textures++;
        } else {
            assert(car_scene_decode(&scene, scene.texture_keys[i], &tex));
            global_textures++;
        }
        assert(tex.w > 0 && tex.h > 0 && tex.rgb);
        free(tex.rgb); free(tex.alpha); free(tex.dxt);
    }
    assert(local_textures > 0 && global_textures > 0);

    printf("240SX: meshes=%d vertices=%llu indices=%llu material_hashes=%d referenced_textures=%d decoded_local=%d decoded_GLOBALB=%d\n",
           scene.scene.count, (unsigned long long)vertices,
           (unsigned long long)indices, material_hashes, scene.texture_key_count,
           local_textures, global_textures);
    printf("240SX: source_bytes=%u cpu_mesh_bytes=%u stock_wheel=%d wheel_slices=%d brake_slices=%d/%d\n",
           scene.source_bytes, scene.cpu_mesh_bytes, scene.stock_wheel,
           wheel_mounted_slices, front_brake_slices, rear_brake_slices);
    printf("240SX GLOBALB stance: front_axle=%g rear_axle=%g front_track=%g rear_track=%g; tyre radius=%g width=%g\n",
           scene.wheel.front_axle, scene.wheel.rear_axle,
           scene.wheel.front_track, scene.wheel.rear_track,
           scene.profile.wheel_r, scene.profile.wheel_w);

    car_scene_release_files(&scene);
    res_budget_snapshot(&scene.budget, &resident);
    assert(resident.used == 0 && resident.live == 0 && scene.scene.count == EXPECT_MESH_COUNT);
    N2Tex after_release = {0};
    assert(!car_scene_decode(&scene, scene.texture_keys[0], &after_release));
    car_scene_free(&scene);
    assert_empty(&scene);
    puts("vita_car_scene_test: PASS (real stock 240SX CPU parse and texture decode)");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s DATAROOT\n", argv[0]);
        return 2;
    }
    test_failure_fixtures();
    test_real_car(argv[1]);
    return 0;
}
