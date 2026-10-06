#ifndef OPENUG2_VITA_ASSET_JOB_H
#define OPENUG2_VITA_ASSET_JOB_H

#include "asset_probe.h"
#include <SDL.h>

/* One CPU-only retry at a time. root remains valid until take/wait completes.
   All GL, logging and job lifecycle operations stay on the main thread. */
typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t complete;
    const char *root;
    VitaAssetStats stats;
    char error[1200];
    double load_ms;
    int result;
} VitaAssetJob;

int vita_asset_job_start(VitaAssetJob *job, const char *root);
int vita_asset_job_take(VitaAssetJob *job);
void vita_asset_job_wait(VitaAssetJob *job);

#endif
