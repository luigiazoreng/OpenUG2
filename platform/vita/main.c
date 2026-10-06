#include "platform.h"
#include "renderer_vita.h"
#include "asset_probe.h"
#include "asset_job.h"
#include "gpu_probe.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Reserve a known bootstrap heap for the production parser. This is not a
   tuned full-engine RAM budget; profile that before integrating residents. */
int _newlib_heap_size_user = 128 * 1024 * 1024;

static void log_probe(int ok, const VitaAssetStats *stats, const char *error, double ms) {
    if (ok) {
        vita_log("240SX stock parser: meshes=%d vertices=%llu indices=%llu material_hashes=%d "
                 "source_bytes=%lu mesh_cpu_bytes=%lu load_ms=%.3f\n",
            stats->meshes, (unsigned long long)stats->vertices, (unsigned long long)stats->indices,
            stats->material_hashes, (unsigned long)stats->source_bytes, (unsigned long)stats->mesh_cpu_bytes, ms);
    } else vita_log("240SX parser: %s (%.3f ms)\n", error, ms);
}

static int probe_asset(void) {
    VitaAssetStats stats;
    char error[1200];
    Uint64 begin = SDL_GetPerformanceCounter();
    int ok = vita_asset_probe(VITA_DATA_ROOT, &stats, error, sizeof error);
    double ms = (SDL_GetPerformanceCounter() - begin) * 1000.0 / SDL_GetPerformanceFrequency();
    log_probe(ok, &stats, error, ms);
    return ok;
}

int main(int argc, char **argv) {
    int profile = OPENUG2_VITA_PROFILE, parser_only = 0;
    unsigned long frame_limit = 0;
    vita_log_open();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--vita-profile")) profile = 1;
        else if (!strcmp(argv[i], "--parser-only")) parser_only = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end; const char *value = argv[++i];
            errno = 0;
            frame_limit = strtoul(value, &end, 10);
            if (!*value || *value == '-' || *end || errno == ERANGE || frame_limit == 0) {
                vita_log("--frames requires a positive integer\n"); vita_log_close(); return 2;
            }
        } else {
            vita_log("usage: --vita-profile --parser-only --frames N\n"); vita_log_close(); return 2;
        }
    }
    vita_log("OpenUG2 isolated Vita bring-up; data_root=%s exists=%d\n", VITA_DATA_ROOT, vita_data_root_exists());
    if (parser_only) {
        if (SDL_Init(SDL_INIT_TIMER) < 0) { vita_log("timer init: %s\n", SDL_GetError()); SDL_Quit(); vita_log_close(); return 1; }
        int ok = probe_asset(); SDL_Quit(); vita_log_close(); return ok ? 0 : 1;
    }
    VitaPlatform platform;
    if (!vita_platform_open(&platform)) { vita_log_close(); return 1; }
#if OPENUG2_VITA_GPU_PROBES
    vita_renderer_log_pools();
    int gpu_ok = vita_gpu_probe_run();
    vita_renderer_log_pools();
    if (!gpu_ok) {
        vita_log("GPU probes failed; collect diagnostics before accepting this renderer\n");
        vita_platform_close(&platform); vita_log_close(); return 1;
    }
#endif
    VitaRenderer *renderer = vita_renderer_create();
    if (!renderer) { vita_platform_close(&platform); vita_log_close(); return 1; }
    int asset_ok = probe_asset(), paused = 0, status = 0;
    VitaAssetJob retry = {0};
    vita_log("Start=pause (blue background) Cross=retry parser (white strip while loading) Select+Start=exit; green background=asset parsed, red=asset unavailable\n");
    vita_log("world/physics/audio inactive; resident timings and traffic inactive; GPU ms/process RAM unavailable\n");
    Uint64 frequency = SDL_GetPerformanceFrequency(), report_start = SDL_GetPerformanceCounter();
    unsigned long frames = 0, report_frames = 0;
    double submit_total = 0, frame_total = 0;
    while (!frame_limit || frames < frame_limit) {
        Uint64 begin = SDL_GetPerformanceCounter();
        VitaInput input;
        vita_platform_poll(&platform, &input);
        if (input.quit) break;
        if (input.pause_pressed) { paused = !paused; vita_log("paused=%d\n", paused); }
        if (vita_asset_job_take(&retry)) {
            asset_ok = retry.result;
            log_probe(asset_ok, &retry.stats, retry.error, retry.load_ms);
            vita_log("asset retry completed: paused=%d\n", paused);
        }
        if (input.confirm_pressed) {
            if (retry.thread) vita_log("asset retry ignored: already running\n");
            else if (vita_asset_job_start(&retry, VITA_DATA_ROOT)) vita_log("asset retry started: paused=%d\n", paused);
            else vita_log("asset retry thread failed: %s\n", SDL_GetError());
        }
        if (input.camera_pressed) vita_log("camera action mapped; no car camera in bootstrap\n");
        if (!vita_renderer_draw(renderer, asset_ok, paused, retry.thread != NULL)) { status = 1; break; }
        Uint64 submitted = SDL_GetPerformanceCounter();
        SDL_GL_SwapWindow(platform.window);
        /* Pace this rendering probe at 30 Hz; there is no physics tick here. */
        Uint64 spent = SDL_GetPerformanceCounter() - begin;
        if (spent < frequency / 30) SDL_Delay((Uint32)((frequency / 30 - spent) * 1000 / frequency));
        Uint64 end = SDL_GetPerformanceCounter();
        frames++; report_frames++;
        submit_total += (submitted - begin) * 1000.0 / frequency;
        frame_total += (end - begin) * 1000.0 / frequency;
        if (profile && end - report_start >= frequency) {
            vita_log("fps=%.2f cpu_submit_ms=%.3f frame_ms=%.3f draw_calls=1 visible_meshes=1 "
                "active_textures=1 estimated_gpu_resource_bytes=%lu steer=%.2f throttle=%.0f "
                "brake=%.0f handbrake=%d nitro_confirm=%d\n",
                report_frames * (double)frequency / (end - report_start), submit_total / report_frames,
                frame_total / report_frames, (unsigned long)vita_renderer_estimated_bytes(), input.steering,
                input.throttle, input.brake, input.handbrake, input.nitro_confirm);
            vita_renderer_log_pools();
            report_frames = 0; submit_total = 0; frame_total = 0; report_start = SDL_GetPerformanceCounter();
        }
    }
    /* A pending CPU probe must finish before SDL/thread services and log close.
       No asynchronous access survives platform teardown. */
    if (retry.thread) {
        vita_log("shutdown: waiting for asset retry\n");
        vita_asset_job_wait(&retry);
        log_probe(retry.result, &retry.stats, retry.error, retry.load_ms);
    }
    vita_renderer_destroy(renderer);
    vita_platform_close(&platform);
    vita_log("shutdown complete: frames=%lu status=%d\n", frames, status);
    vita_log_close();
    return status;
}
