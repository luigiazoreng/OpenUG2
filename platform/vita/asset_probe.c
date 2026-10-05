#include "asset_probe.h"
#include "nfsu2.h"
#include <sys/stat.h>

int vita_asset_probe(const char *root, VitaAssetStats *stats, char *error, size_t error_size) {
    char path[1024];
    struct stat info;
    memset(stats, 0, sizeof *stats);
    int n = snprintf(path, sizeof path, "%s/CARS/240SX/GEOMETRY.BIN", root);
    if (n < 0 || (size_t)n >= sizeof path) {
        snprintf(error, error_size, "asset path too long"); return 0;
    }
    if (stat(path, &info) < 0 || !S_ISREG(info.st_mode) || info.st_size <= 0) {
        snprintf(error, error_size, "missing/empty asset: %s", path); return 0;
    }
    /* A bring-up limit, not a retail parser format constraint. Bound full-file
       heap reads until resident/file budgets have been measured on device. */
    if ((uint64_t)info.st_size > 32u * 1024u * 1024u) {
        snprintf(error, error_size, "asset exceeds 32 MiB bring-up read budget"); return 0;
    }
    long length = 0;
    unsigned char *data = n2_read_file(path, &length);
    if (!data || length != (long)info.st_size) {
        free(data); snprintf(error, error_size, "asset read failed: %s", path); return 0;
    }
    N2Scene scene = {0};
    n2_load_car(data, length, &scene, NULL, 0, NULL);
    stats->source_bytes = (size_t)length;
    stats->meshes = scene.count;
    stats->mesh_cpu_bytes = (size_t)scene.cap * sizeof(N2Mesh);
    for (int i = 0; i < scene.count; i++) {
        const N2Mesh *m = &scene.meshes[i];
        stats->vertices += (uint64_t)m->nverts;
        stats->indices += (uint64_t)m->nidx;
        stats->mesh_cpu_bytes += (size_t)m->nverts * (m->authored_normals ? 8 : 5) * sizeof(float);
        stats->mesh_cpu_bytes += (size_t)m->nidx * sizeof(uint16_t);
        if (m->vcol) stats->mesh_cpu_bytes += (size_t)m->nverts * 4;
        if (m->car_material) {
            int seen = 0;
            for (int j = 0; j < i; j++) if (scene.meshes[j].car_material == m->car_material) seen = 1;
            if (!seen) stats->material_hashes++;
        }
    }
    n2_free_scene(&scene); free(data);
    if (!stats->meshes || !stats->vertices || !stats->indices) {
        snprintf(error, error_size, "no stock-car geometry decoded"); return 0;
    }
    if (error_size) error[0] = 0;
    return 1;
}
