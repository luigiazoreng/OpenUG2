#include "asset_job.h"
#include <string.h>

static int retry_worker(void *data) {
    VitaAssetJob *job = data;
    Uint64 begin = SDL_GetPerformanceCounter();
    job->result = vita_asset_probe(job->root, &job->stats, job->error, sizeof job->error);
    job->load_ms = (SDL_GetPerformanceCounter() - begin) * 1000.0 / SDL_GetPerformanceFrequency();
    /* Publish the CPU result after the parser has freed scene/file buffers. */
    SDL_AtomicSet(&job->complete, 1);
    return job->result;
}

int vita_asset_job_start(VitaAssetJob *job, const char *root) {
    if (job->thread) return 0;
    memset(&job->stats, 0, sizeof job->stats);
    job->error[0] = 0;
    job->root = root;
    job->result = 0;
    job->load_ms = 0;
    SDL_AtomicSet(&job->complete, 0);
    /* Explicit stack budget; no additional copies of geometry or GPU state. */
    job->thread = SDL_CreateThreadWithStackSize(retry_worker, "240SX retry", 128u * 1024u, job);
    return job->thread != NULL;
}

void vita_asset_job_wait(VitaAssetJob *job) {
    if (job->thread) {
        SDL_WaitThread(job->thread, NULL);
        job->thread = NULL;
    }
}

int vita_asset_job_take(VitaAssetJob *job) {
    if (!job->thread || !SDL_AtomicGet(&job->complete)) return 0;
    vita_asset_job_wait(job);
    return 1;
}
