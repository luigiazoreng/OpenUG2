#ifndef OPENUG2_VITA_STARTUP_GUARD_H
#define OPENUG2_VITA_STARTUP_GUARD_H

enum VitaStartupStage {
    VITA_STARTUP_CONTEXT = 1, VITA_STARTUP_GPU, VITA_STARTUP_TRANSLATE,
    VITA_STARTUP_VERTEX, VITA_STARTUP_FRAGMENT, VITA_STARTUP_REGISTER,
    VITA_STARTUP_LINK, VITA_STARTUP_CAR
};
/* Startup-only native monitor; it never calls SDL, GL or the compiler. */
int vita_startup_guard_start(const char *run_id);
void vita_startup_guard_stage(enum VitaStartupStage stage);
void vita_startup_guard_stop(void);

#endif
