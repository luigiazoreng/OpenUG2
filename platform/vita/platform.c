#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef OPENUG2_VITA
#include <psp2/ctrl.h>
#endif

static FILE *log_file;

void vita_log_open(void) {
    log_file = fopen(VITA_DATA_ROOT "/vita-bringup.log", "w");
    if (!log_file) log_file = fopen("ux0:data/openug2-bringup.log", "w");
}

void vita_log(const char *format, ...) {
    va_list args;
    va_start(args, format); vfprintf(stdout, format, args); va_end(args);
    if (log_file) {
        va_start(args, format); vfprintf(log_file, format, args); va_end(args);
        fflush(log_file);
    }
}

void vita_log_close(void) {
    if (log_file) fclose(log_file);
    log_file = NULL;
}

int vita_data_root_exists(void) {
    struct stat info;
    return stat(VITA_DATA_ROOT, &info) == 0 && S_ISDIR(info.st_mode);
}

static void open_controller(VitaPlatform *p) {
    for (int i = 0; i < SDL_NumJoysticks() && !p->controller; i++) {
        if (!SDL_IsGameController(i)) {
            /* Vita SDL's device 0 is the built-in pad. Only this platform
               backend supplies a fallback mapping; external pads use SDL's DB. */
            if (i != 0) continue;
            char guid[33], mapping[512];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
            snprintf(mapping, sizeof mapping,
                "%s,OpenUG2 Vita,a:b2,b:b1,x:b3,y:b0,back:b10,start:b11,"
                "leftshoulder:b4,rightshoulder:b5,leftx:a0,lefty:a1,"
                "rightx:a2,righty:a3,dpdown:b6,dpleft:b7,dpup:b8,dpright:b9,", guid);
            if (SDL_GameControllerAddMapping(mapping) < 0) {
                vita_log("controller mapping: %s\n", SDL_GetError()); continue;
            }
        }
        p->controller = SDL_GameControllerOpen(i);
    }
    if (p->controller) {
        SDL_version version;
        SDL_GetVersion(&version);
        vita_log("controller: %s\n", SDL_GameControllerName(p->controller));
        vita_log("SDL input version=%u.%u.%u\n", version.major, version.minor, version.patch);
        char *mapping = SDL_GameControllerMapping(p->controller);
        vita_log("SDL controller mapping=%s\n", mapping ? mapping : "missing");
        SDL_free(mapping);
    }
}

int vita_platform_open(VitaPlatform *p) {
    memset(p, 0, sizeof *p);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) < 0) {
        vita_log("SDL init: %s\n", SDL_GetError()); SDL_Quit(); return 0;
    }
    p->sdl_ready = 1;
    /* SDL owns vglInitExtended/SDL swap -> vglSwapBuffers. No manual init. */
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) < 0) {
        vita_log("GL attributes: %s\n", SDL_GetError()); goto fail;
    }
    p->window = SDL_CreateWindow("OpenUG2 Vita Bring-up", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, VITA_WIDTH, VITA_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!p->window) { vita_log("window: %s\n", SDL_GetError()); goto fail; }
    p->context = SDL_GL_CreateContext(p->window);
    if (!p->context) { vita_log("context: %s\n", SDL_GetError()); goto fail; }
    if (SDL_GL_SetSwapInterval(1) < 0) vita_log("vsync: %s\n", SDL_GetError());
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(p->window, &w, &h);
    vita_log("SDL driver=%s drawable=%dx%d\n", SDL_GetCurrentVideoDriver(), w, h);
    if (w != VITA_WIDTH || h != VITA_HEIGHT) { vita_log("unexpected drawable size\n"); goto fail; }
    open_controller(p);
    if (!p->controller) { vita_log("built-in controller unavailable: %s\n", SDL_GetError()); goto fail; }
    return 1;
fail:
    vita_platform_close(p); return 0;
}

void vita_platform_poll(VitaPlatform *p, VitaInput *input) {
    memset(input, 0, sizeof *input);
    Uint32 event_pressed = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT || event.type == SDL_APP_TERMINATING) input->quit = 1;
        if (event.type == SDL_CONTROLLERDEVICEREMOVED && p->controller &&
            event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p->controller))) {
            SDL_GameControllerClose(p->controller); p->controller = NULL;
            p->previous_buttons = 0;
        }
        if (event.type == SDL_CONTROLLERDEVICEADDED && !p->controller) open_controller(p);
        if ((event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) &&
            p->controller && event.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p->controller)) &&
            event.cbutton.button < SDL_CONTROLLER_BUTTON_MAX) {
            const char *name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)event.cbutton.button);
            vita_log("input event: button=%s state=%s\n", name ? name : "unknown",
                event.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
            if (event.type == SDL_CONTROLLERBUTTONDOWN) event_pressed |= 1u << event.cbutton.button;
        }
    }
#ifdef OPENUG2_VITA
    /* Observe hardware independently of SDL without changing sampling mode or
       substituting the controller backend. Only button transitions are logged. */
    SceCtrlData native;
    int native_result = sceCtrlPeekBufferPositive(0, &native, 1);
    if (native_result > 0) {
        if (!p->native_input_logged || native.buttons != p->previous_native_buttons) {
            vita_log("input native: buttons=0x%08lx start=%d select=%d lx=%u ly=%u\n",
                (unsigned long)native.buttons, !!(native.buttons & SCE_CTRL_START),
                !!(native.buttons & SCE_CTRL_SELECT), (unsigned)native.lx, (unsigned)native.ly);
            p->previous_native_buttons = native.buttons;
            p->native_input_logged = 1;
        }
    } else if (!p->native_input_logged) {
        vita_log("input native: read_result=%d\n", native_result);
        p->native_input_logged = 1;
    }
#endif
    if (!p->controller) return;
    Sint16 axis = SDL_GameControllerGetAxis(p->controller, SDL_CONTROLLER_AXIS_LEFTX);
    float steering = axis < 0 ? axis / 32768.0f : axis / 32767.0f;
    const float deadzone = 0.15f;
    if (steering > deadzone) steering = (steering - deadzone) / (1.0f - deadzone);
    else if (steering < -deadzone) steering = (steering + deadzone) / (1.0f - deadzone);
    else steering = 0;
    input->steering = -steering; /* Engine: left is positive. */
    Uint32 buttons = 0;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
        if (SDL_GameControllerGetButton(p->controller, (SDL_GameControllerButton)b)) buttons |= 1u << b;
    /* A down/up pair can be drained by the same poll. Preserve its press edge
       even if the final held state is already released. */
    Uint32 pressed = (buttons & ~p->previous_buttons) | event_pressed;
#define HELD(b) ((buttons & (1u << SDL_CONTROLLER_BUTTON_##b)) != 0)
#define PRESSED(b) ((pressed & (1u << SDL_CONTROLLER_BUTTON_##b)) != 0)
    input->throttle = HELD(RIGHTSHOULDER) ? 1.0f : 0.0f;
    input->brake = HELD(LEFTSHOULDER) ? 1.0f : 0.0f;
    input->nitro_confirm = HELD(A);
    input->handbrake = HELD(X);
    input->camera_pressed = PRESSED(Y);
    input->confirm_pressed = PRESSED(A);
    input->quit |= HELD(BACK) && HELD(START);
    input->pause_pressed = PRESSED(START) && !input->quit;
    if (buttons != p->previous_buttons || event_pressed) {
        vita_log("input SDL: buttons=0x%08lx start=%d select=%d pause_pressed=%d quit=%d\n",
            (unsigned long)buttons, HELD(START), HELD(BACK), input->pause_pressed, input->quit);
    }
#undef HELD
#undef PRESSED
    p->previous_buttons = buttons;
}

void vita_platform_close(VitaPlatform *p) {
    if (p->controller) SDL_GameControllerClose(p->controller);
    if (p->context) SDL_GL_DeleteContext(p->context);
    if (p->window) SDL_DestroyWindow(p->window);
    if (p->sdl_ready) SDL_Quit();
    /* Tested vitaGL exposes no global teardown API. Process exit reclaims
       its pools. Application GL objects must be freed before this function. */
    memset(p, 0, sizeof *p);
}
