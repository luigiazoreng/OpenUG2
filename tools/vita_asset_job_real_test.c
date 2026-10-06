/* Optional host test: actual stock 240SX parser in the production retry job. */
#include "../platform/vita/asset_job.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s PC_DATA_ROOT\n", argv[0]); return 2; }
    if (SDL_Init(SDL_INIT_TIMER) < 0) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    VitaAssetJob job = {0};
    int failed = 0;
    /* Repeat the actual decoder to exercise reuse/free, without concurrency. */
    for (int i = 0; i < 3 && !failed; i++) {
        if (!vita_asset_job_start(&job, argv[1])) { fprintf(stderr, "%s\n", SDL_GetError()); failed = 1; break; }
        Uint64 deadline = SDL_GetTicks64() + 30000;
        unsigned long polls = 0;
        int ready = 0;
        while (!(ready = vita_asset_job_take(&job)) && SDL_GetTicks64() < deadline) {
            polls++; SDL_Delay(1);
        }
        if (!ready) { vita_asset_job_wait(&job); fprintf(stderr, "retry deadline exceeded\n"); failed = 1; break; }
        if (!job.result || job.stats.meshes != 101 || job.stats.vertices != 30571 ||
            job.stats.indices != 18237 || job.stats.material_hashes != 24 || job.stats.source_bytes != 7675600) {
            fprintf(stderr, "stock 240SX mismatch: %s\n", job.error); failed = 1;
        }
        printf("stock 240SX worker run=%d meshes=%d vertices=%llu indices=%llu material_hashes=%d "
               "source_bytes=%lu load_ms=%.3f main_polls=%lu\n", i + 1, job.stats.meshes,
               (unsigned long long)job.stats.vertices, (unsigned long long)job.stats.indices,
               job.stats.material_hashes, (unsigned long)job.stats.source_bytes, job.load_ms, polls);
    }
    vita_asset_job_wait(&job);
    SDL_Quit();
    return failed;
}
