#ifndef OPENUG2_VITA_PLATFORM_H
#define OPENUG2_VITA_PLATFORM_H

#include <SDL.h>
#include <stddef.h>

#define VITA_DATA_ROOT "ux0:data/openug2"
#define VITA_WIDTH 960
#define VITA_HEIGHT 544

typedef struct {
    float steering, throttle, brake;
    int nitro_confirm, handbrake, camera_pressed, pause_pressed, confirm_pressed;
    int quit;
} VitaInput;

typedef struct {
    SDL_Window *window;
    SDL_GLContext context;
    SDL_GameController *controller;
    Uint32 previous_buttons;
    int sdl_ready;
} VitaPlatform;

void vita_log_open(void);
void vita_log(const char *format, ...);
void vita_log_close(void);
int vita_data_root_exists(void);
int vita_platform_open(VitaPlatform *p);
void vita_platform_poll(VitaPlatform *p, VitaInput *input);
void vita_platform_close(VitaPlatform *p);

#endif
