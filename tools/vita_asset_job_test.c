/* Deterministic async retry test with the production job/input lifecycle. */
#include "../platform/vita/asset_job.h"
#include "../platform/vita/platform.h"
#include <stdio.h>
#include <string.h>

static SDL_sem *entered, *release_probe;
static int calls, checks, failures;
#define CHECK(condition, name) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL: %s\n", name); failures++; } } while (0)

/* Deliberately hold the CPU parser until the main thread releases it. */
int vita_asset_probe(const char *root, VitaAssetStats *stats, char *error, size_t size) {
    calls++;
    SDL_SemPost(entered);
    SDL_SemWait(release_probe);
    memset(stats, 0, sizeof *stats);
    if (!strcmp(root, "fail")) {
        snprintf(error, size, "synthetic failure");
        return 0;
    }
    stats->meshes = 101;
    error[0] = 0;
    return 1;
}

int main(void) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0) return 1;
    entered = SDL_CreateSemaphore(0); release_probe = SDL_CreateSemaphore(0);
    if (!entered || !release_probe) { SDL_Quit(); return 1; }
    int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (device < 0) { SDL_Quit(); return 1; }
    char guid[33], mapping[256];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device), guid, sizeof guid);
    snprintf(mapping, sizeof mapping, "%s,Async retry fixture,a:b2,back:b10,start:b11,", guid);
    CHECK(SDL_GameControllerAddMapping(mapping) >= 0, "fixture mapping");
    VitaPlatform platform = {0};
    platform.sdl_ready = 1;
    platform.controller = SDL_GameControllerOpen(device);
    if (!platform.controller) { SDL_Quit(); return 1; }
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(platform.controller);
    VitaInput input;
    vita_platform_poll(&platform, &input);
    VitaAssetJob job = {0};
    CHECK(vita_asset_job_start(&job, "success"), "start worker");
    CHECK(SDL_SemWaitTimeout(entered, 1000) == 0, "worker is inside parser");
    CHECK(!vita_asset_job_start(&job, "success"), "reject duplicate worker");
    CHECK(!vita_asset_job_take(&job), "pending job does not block take");
    CHECK(SDL_JoystickSetVirtualButton(joystick, 11, 1) == 0, "press Start during parse");
    SDL_JoystickUpdate(); vita_platform_poll(&platform, &input);
    CHECK(input.pause_pressed && !input.quit, "Start delivered while parser blocked");
    CHECK(SDL_JoystickSetVirtualButton(joystick, 10, 1) == 0, "press Select during parse");
    SDL_JoystickUpdate(); vita_platform_poll(&platform, &input);
    CHECK(input.quit && !input.pause_pressed, "exit chord delivered while parser blocked");
    SDL_SemPost(release_probe);
    vita_asset_job_wait(&job);
    CHECK(!job.thread && job.result == 1 && job.stats.meshes == 101, "join publishes success before teardown");
    CHECK(calls == 1, "one parser invocation");
    CHECK(!vita_asset_job_take(&job), "completed result consumed only once");

    CHECK(vita_asset_job_start(&job, "fail"), "restart job");
    CHECK(SDL_SemWaitTimeout(entered, 1000) == 0, "second worker entered");
    SDL_SemPost(release_probe);
    Uint64 deadline = SDL_GetTicks64() + 1000;
    int taken = 0;
    while (!taken && SDL_GetTicks64() < deadline) { taken = vita_asset_job_take(&job); SDL_Delay(1); }
    CHECK(taken && !job.thread, "nonblocking take consumes ready job");
    CHECK(job.result == 0 && !strcmp(job.error, "synthetic failure"), "parser error delivered");
    CHECK(calls == 2, "serialized second invocation");
    vita_asset_job_wait(&job); /* Also safely joins if the readiness check failed. */
    SDL_JoystickDetachVirtual(device);
    vita_platform_poll(&platform, &input);
    SDL_DestroySemaphore(entered); SDL_DestroySemaphore(release_probe);
    vita_platform_close(&platform);
    printf("Vita asset job host checks: %d passed, %d failed\n", checks - failures, failures);
    return failures ? 1 : 0;
}
