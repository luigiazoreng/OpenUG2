#include "platform.h"
#include "renderer_vita.h"
#include "gpu_probe.h"
#include "car_viewer.h"
#include "render.h"
#include "startup_guard.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _newlib_heap_size_user = 128 * 1024 * 1024;

#ifndef OPENUG2_BUILD_REVISION
#define OPENUG2_BUILD_REVISION "unknown"
#define OPENUG2_BUILD_SHA256 "unknown"
#endif

static int session_id(char id[25]) {
    FILE *file = fopen(VITA_DATA_ROOT "/diagnostic-session.txt", "r");
    char line[128] = {0};
    if (!file) return 0;
    int ok = fgets(line, sizeof line, file) != NULL;
    fclose(file);
    const char *prefix = "CAR SESSION run_id=";
    if (!ok || strncmp(line, prefix, strlen(prefix))) return 0;
    const char *value = line+strlen(prefix);
    for (int i = 0; i < 24; i++) if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return 0;
    if (value[24] != '\n' && value[24] != 0 && value[24] != '\r') return 0;
    memcpy(id, value, 24); id[24] = 0; return 1;
}

static char diagnostic_run_id[25];
static unsigned dumped_shaders;

static float clamp(float x, float lo, float hi) { return fmaxf(lo, fminf(hi, x)); }

/* Optional hook from the pinned private dependency. The payload is borrowed
   only for this call; source dumps are bounded and never used as shaders. */
void openug2_vitagl_trace(const char *phase, unsigned type, const char *source, unsigned bytes) {
    vita_log("GPU BACKEND phase=%s shader=%u bytes=%u\n", phase, type, bytes);
    if (!strcmp(phase, "translate_begin")) vita_startup_guard_stage(VITA_STARTUP_TRANSLATE);
    else if (!strcmp(phase, "compile_begin"))
        vita_startup_guard_stage(type == GL_VERTEX_SHADER ? VITA_STARTUP_VERTEX : VITA_STARTUP_FRAGMENT);
    else if (!strcmp(phase, "register_begin")) vita_startup_guard_stage(VITA_STARTUP_REGISTER);
    else if (!strcmp(phase, "link_begin")) vita_startup_guard_stage(VITA_STARTUP_LINK);
    unsigned bit = type == GL_VERTEX_SHADER ? 1u : 2u;
    if (source && bytes && bytes <= 65536 && !(dumped_shaders & bit)) {
        const char *path = type == GL_VERTEX_SHADER ? VITA_DATA_ROOT "/shader-vertex.cg" : VITA_DATA_ROOT "/shader-fragment.cg";
        FILE *file = fopen(path, "wb");
        int ok = file && fprintf(file, "// CAR SESSION run_id=%s\n", diagnostic_run_id) > 0 &&
            fwrite(source, 1, bytes, file) == bytes;
        if (ok) dumped_shaders |= bit;
        if (file) fclose(file);
        vita_log("GPU BACKEND source_dump=%s bytes=%u written=%d\n", path, bytes, ok);
    }
}

int main(int argc, char **argv) {
    unsigned long frame_limit = 0;
    if (!vita_log_open_at(VITA_DATA_ROOT "/vita-car-viewer.log")) {
        fprintf(stderr, "Cannot open car viewer log; stage the prepared bundle in ux0:data/openug2\n"); return 1;
    }
    char run_id[25];
    if (!session_id(run_id)) {
        vita_log("CAR ERROR diagnostic-session.txt missing or invalid; deploy the prepared bundle\n"); vita_log_close(); return 1;
    }
    vita_log("CAR SESSION run_id=%s\n", run_id);
    vita_log("CAR BUILD revision=%s source_sha256=%s\n", OPENUG2_BUILD_REVISION, OPENUG2_BUILD_SHA256);
    render_set_diagnostic_logger(vita_log);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i+1 < argc) {
            char *end; const char *value = argv[++i]; errno = 0;
            frame_limit = strtoul(value, &end, 10);
            if (!*value || *value == '-' || *end || errno || !frame_limit) {
                vita_log("CAR ERROR --frames requires a positive integer\n"); vita_log_close(); return 2;
            }
        } else { vita_log("CAR ERROR usage: --frames N\n"); vita_log_close(); return 2; }
    }
    memcpy(diagnostic_run_id, run_id, sizeof diagnostic_run_id);
    if (!vita_startup_guard_start(run_id)) {
        vita_log("CAR ERROR startup monitor unavailable\n"); vita_log_close(); return 1;
    }
    VitaPlatform platform;
    if (!vita_platform_open(&platform)) { vita_startup_guard_stop(); vita_log_close(); return 1; }
    vita_renderer_log_pools();
    vita_startup_guard_stage(VITA_STARTUP_GPU);
    int status = !vita_gpu_probe_run();
    vita_renderer_log_pools();
    if (status) {
        vita_startup_guard_stop();
        vita_log("CAR ERROR GPU suite failed; car loading was not started\n");
        vita_platform_close(&platform);
        vita_log("CAR SHUTDOWN frames=0 status=1\n");
        vita_log_close(); return 1;
    }
    char error[1200] = {0};
    Uint64 load_start = SDL_GetPerformanceCounter();
    vita_startup_guard_stage(VITA_STARTUP_CAR);
    CarViewer *viewer = car_viewer_create(VITA_DATA_ROOT, error, sizeof error);
    vita_startup_guard_stop();
    if (!viewer) {
        vita_log("CAR ERROR %s\n", error); vita_platform_close(&platform); vita_log_close(); return 1;
    }
    vita_log("CAR LOAD elapsed_ms=%.3f\n", (SDL_GetPerformanceCounter()-load_start)*1000.0/SDL_GetPerformanceFrequency());
    vita_log("CAR CONTROLS left_stick=orbit/elevation right_stick_y=zoom X=paint square=lights triangle=camera_preset START=pause SELECT+START=exit\n");
    vita_log("CAR SCOPE static_stock_model world=inactive physics=inactive audio=inactive GPU_ms=unavailable process_RAM_peak=unavailable\n");
    vita_renderer_log_pools();
    float yaw = -.6f, pitch = .28f, distance = 7;
    int paused = 0, lights = 0, paint = 0, preset = 0;
    unsigned long frames = 0, sampled = 0;
    double cpu_total = 0, frame_total = 0;
    Uint64 frequency = SDL_GetPerformanceFrequency(), report_start = SDL_GetPerformanceCounter(), previous = report_start;
    while (!frame_limit || frames < frame_limit) {
        Uint64 begin = SDL_GetPerformanceCounter();
        float dt = clamp((float)((begin-previous)/(double)frequency), 0, .1f); previous = begin;
        VitaInput input; vita_platform_poll(&platform, &input);
        if (input.quit) { vita_log("CAR EXIT select_start_or_termination=1\n"); break; }
        if (input.pause_pressed) { paused = !paused; vita_log("CAR CONTROL paused=%d\n", paused); }
        if (input.confirm_pressed) { paint = (paint+1)&3; vita_log("CAR CONTROL paint=%d\n", paint); }
        if (input.handbrake_pressed) { lights = !lights; vita_log("CAR CONTROL lights=%d\n", lights); }
        if (input.camera_pressed) {
            const float views[4] = {-.6f,2.5f,1.5707963f,-1.5707963f};
            preset = (preset+1)&3; yaw = views[preset]; pitch = .28f; distance = 7;
            vita_log("CAR CONTROL camera_preset=%d\n", preset);
        }
        if (!paused) {
            yaw += input.steering * dt * 1.5f;
            pitch = clamp(pitch + input.camera_pitch * dt, .06f, 1.15f);
            distance = clamp(distance + input.camera_zoom * dt * 3, 4.5f, 10);
        }
        if (!car_viewer_draw(viewer, yaw, pitch, distance, paint, lights, paused, VITA_WIDTH, VITA_HEIGHT)) {
            vita_log("CAR ERROR draw failed\n"); status = 1; break;
        }
        Uint64 submitted = SDL_GetPerformanceCounter(); SDL_GL_SwapWindow(platform.window);
        Uint64 spent = SDL_GetPerformanceCounter()-begin;
        if (spent < frequency/30) SDL_Delay((Uint32)((frequency/30-spent)*1000/frequency));
        Uint64 end = SDL_GetPerformanceCounter(); frames++; sampled++;
        cpu_total += (submitted-begin)*1000.0/frequency; frame_total += (end-begin)*1000.0/frequency;
        if (end-report_start >= frequency) {
            CarViewerStats stats; car_viewer_stats(viewer, &stats);
            vita_log("CAR PERF fps=%.2f cpu_submit_ms=%.3f frame_ms=%.3f draws=%u meshes=%u textures=%u gpu_estimated_bytes=%lu yaw=%.3f pitch=%.3f distance=%.3f paused=%d paint=%d lights=%d\n",
                sampled*(double)frequency/(end-report_start), cpu_total/sampled, frame_total/sampled,
                stats.draws, stats.meshes, stats.textures, (unsigned long)stats.gpu_estimated_bytes,
                yaw, pitch, distance, paused, paint, lights);
            vita_renderer_log_pools();
            sampled = 0; cpu_total = frame_total = 0; report_start = end;
        }
    }
    car_viewer_destroy(viewer); vita_renderer_log_pools(); vita_platform_close(&platform);
    vita_log("CAR SHUTDOWN frames=%lu status=%d\n", frames, status); vita_log_close(); return status;
}
