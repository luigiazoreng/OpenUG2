#ifndef OPENUG2_VITA_ASSET_PROBE_H
#define OPENUG2_VITA_ASSET_PROBE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t source_bytes, mesh_cpu_bytes;
    uint64_t vertices, indices;
    int meshes, material_hashes;
} VitaAssetStats;

/* Runs the actual stock-car parser. No GL, world, audio or retained assets.
   Returns zero and a diagnostic on missing/oversized/empty geometry. */
int vita_asset_probe(const char *root, VitaAssetStats *stats, char *error, size_t error_size);

#endif
