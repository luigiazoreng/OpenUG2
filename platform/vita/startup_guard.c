#include "startup_guard.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef OPENUG2_STARTUP_GUARD_TEST
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#endif

static SceUID guard_thread = -1, guard_log = -1;
static unsigned guard_running, guard_generation, guard_stage;

static const char *stage_name(unsigned stage) {
    static const char *const names[] = {"unknown", "context", "gpu_suite",
        "translation", "vertex_compile", "fragment_compile", "gxm_register",
        "link", "car_load"};
    return stage < sizeof names / sizeof *names ? names[stage] : names[0];
}

static void guard_record(const char *reason, unsigned stage, uint64_t elapsed) {
    char line[192];
    int n = snprintf(line, sizeof line, "STARTUP %s stage=%s elapsed_ms=%llu\n",
        reason, stage_name(stage), (unsigned long long)(elapsed / 1000));
    if (guard_log >= 0 && n > 0 && n < (int)sizeof line)
        sceIoWrite(guard_log, line, (SceSize)n);
}

static int monitor(SceSize argc, void *argv) {
    (void)argc; (void)argv;
    uint64_t begin = sceKernelGetSystemTimeWide(), phase_begin = begin;
    unsigned previous = __atomic_load_n(&guard_generation, __ATOMIC_ACQUIRE);
    while (__atomic_load_n(&guard_running, __ATOMIC_ACQUIRE)) {
        uint64_t now = sceKernelGetSystemTimeWide();
        unsigned generation = __atomic_load_n(&guard_generation, __ATOMIC_ACQUIRE);
        unsigned stage = __atomic_load_n(&guard_stage, __ATOMIC_ACQUIRE);
        if (generation != previous) { phase_begin = now; previous = generation; }
        SceCtrlData pad = {0};
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0 &&
            (pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_START)) == (SCE_CTRL_SELECT | SCE_CTRL_START)) {
            guard_record("EXIT_SELECT_START", stage, now - begin);
            sceKernelExitProcess(0); return 0;
        }
        if (now - phase_begin >= UINT64_C(120000000) || now - begin >= UINT64_C(300000000)) {
            guard_record("TIMEOUT", stage, now - begin);
            /* A compiler in an unknown state cannot safely be cancelled and
               retried. Terminate the process; do not free its live GL objects. */
            sceKernelExitProcess(1); return 0;
        }
        sceKernelDelayThread(50000);
    }
    return 0;
}

int vita_startup_guard_start(const char *run_id) {
    if (guard_thread >= 0 || !run_id || strlen(run_id) != 24) return 0;
    for (int i = 0; i < 24; i++)
        if (!((run_id[i] >= '0' && run_id[i] <= '9') || (run_id[i] >= 'a' && run_id[i] <= 'f'))) return 0;
    guard_log = sceIoOpen("ux0:data/openug2/startup-guard.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (guard_log < 0) return 0;
    char session[64];
    int bytes = snprintf(session, sizeof session, "STARTUP SESSION run_id=%s\n", run_id);
    if (sceIoWrite(guard_log, session, (SceSize)bytes) != bytes) {
        sceIoClose(guard_log); guard_log = -1; return 0;
    }
    __atomic_store_n(&guard_stage, VITA_STARTUP_CONTEXT, __ATOMIC_RELEASE);
    __atomic_store_n(&guard_generation, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&guard_running, 1, __ATOMIC_RELEASE);
    guard_thread = sceKernelCreateThread("openug2_startup_guard", monitor, 0x10000100, 0x4000, 0, 0, NULL);
    if (guard_thread < 0 || sceKernelStartThread(guard_thread, 0, NULL) < 0) {
        if (guard_thread >= 0) sceKernelDeleteThread(guard_thread);
        guard_thread = -1;
        __atomic_store_n(&guard_running, 0, __ATOMIC_RELEASE);
        sceIoClose(guard_log); guard_log = -1; return 0;
    }
    guard_record("BEGIN", VITA_STARTUP_CONTEXT, 0);
    return 1;
}

void vita_startup_guard_stage(enum VitaStartupStage stage) {
    __atomic_store_n(&guard_stage, stage, __ATOMIC_RELEASE);
    __atomic_add_fetch(&guard_generation, 1, __ATOMIC_RELEASE);
    guard_record("PHASE", stage, 0);
}

void vita_startup_guard_stop(void) {
    if (guard_thread >= 0) {
        __atomic_store_n(&guard_running, 0, __ATOMIC_RELEASE);
        sceKernelWaitThreadEnd(guard_thread, NULL, NULL);
        sceKernelDeleteThread(guard_thread); guard_thread = -1;
        guard_record("END", __atomic_load_n(&guard_stage, __ATOMIC_ACQUIRE), 0);
    }
    if (guard_log >= 0) { sceIoClose(guard_log); guard_log = -1; }
}
