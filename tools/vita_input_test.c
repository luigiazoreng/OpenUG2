/* Host SDL virtual-controller integration test of the actual Vita poll path. */
#include "../platform/vita/platform.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(condition, name) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL: %s (%s)\n", name, #condition); failures++; } } while (0)

static void button(SDL_Joystick *joystick, int index, int held) {
    CHECK(SDL_JoystickSetVirtualButton(joystick, index, held) == 0, "set virtual button");
    SDL_JoystickUpdate();
}

static void axis(SDL_Joystick *joystick, int index, Sint16 value, const char *name) {
    CHECK(SDL_JoystickSetVirtualAxis(joystick, index, value) == 0, name);
    SDL_JoystickUpdate();
}

int main(void) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL init: %s\n", SDL_GetError()); return 1;
    }
    int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (device < 0) { fprintf(stderr, "attach: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    char guid[33], mapping[512];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device), guid, sizeof guid);
    snprintf(mapping, sizeof mapping,
        "%s,Vita poll fixture,a:b2,b:b1,x:b3,y:b0,back:b10,start:b11,"
        "leftshoulder:b4,rightshoulder:b5,leftx:a0,lefty:a1,rightx:a2,righty:a3,", guid);
    CHECK(SDL_GameControllerAddMapping(mapping) >= 0, "add fixture mapping");
    VitaPlatform platform = {0};
    platform.sdl_ready = 1;
    platform.controller = SDL_GameControllerOpen(device);
    if (!platform.controller) { fprintf(stderr, "open: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(platform.controller);
    VitaInput input;
    vita_platform_poll(&platform, &input);
    CHECK(!input.pause_pressed && !input.quit, "initial state");

    button(joystick, 11, 1);
    vita_platform_poll(&platform, &input);
    CHECK(input.pause_pressed && !input.quit, "Start press");
    vita_platform_poll(&platform, &input);
    CHECK(!input.pause_pressed, "held Start does not retrigger");
    button(joystick, 11, 0);
    vita_platform_poll(&platform, &input);
    CHECK(!input.pause_pressed, "Start release");

    /* Square maps to SDL X: one edge on press, no repeated edge while held. */
    button(joystick, 3, 1);
    vita_platform_poll(&platform, &input);
    CHECK(input.handbrake && input.handbrake_pressed, "Square press edge and held state");
    vita_platform_poll(&platform, &input);
    CHECK(input.handbrake && !input.handbrake_pressed, "held Square does not retrigger");
    button(joystick, 3, 0);
    vita_platform_poll(&platform, &input);
    CHECK(!input.handbrake && !input.handbrake_pressed, "Square release clears edge and held state");

    /* Both events queued before a poll, final held state released. This is the
       regression: polling held state alone loses a real, brief press. */
    SDL_Event down = {0}, up = {0};
    down.type = SDL_CONTROLLERBUTTONDOWN;
    down.cbutton.which = SDL_JoystickInstanceID(joystick);
    down.cbutton.button = SDL_CONTROLLER_BUTTON_START;
    down.cbutton.state = SDL_PRESSED;
    up = down; up.type = SDL_CONTROLLERBUTTONUP; up.cbutton.state = SDL_RELEASED;
    CHECK(SDL_PushEvent(&down) == 1 && SDL_PushEvent(&up) == 1, "queue short Start pulse");
    vita_platform_poll(&platform, &input);
    CHECK(input.pause_pressed && !input.quit, "short Start pulse is preserved");
    vita_platform_poll(&platform, &input);
    CHECK(!input.pause_pressed, "short pulse does not stick");

    down.cbutton.button = SDL_CONTROLLER_BUTTON_X;
    up.cbutton.button = SDL_CONTROLLER_BUTTON_X;
    CHECK(SDL_PushEvent(&down) == 1 && SDL_PushEvent(&up) == 1, "queue short Square pulse");
    vita_platform_poll(&platform, &input);
    CHECK(input.handbrake_pressed && !input.handbrake, "short Square pulse is preserved");
    vita_platform_poll(&platform, &input);
    CHECK(!input.handbrake_pressed, "short Square pulse does not retrigger");

    down.cbutton.button = SDL_CONTROLLER_BUTTON_START;
    up.cbutton.button = SDL_CONTROLLER_BUTTON_START;
    down.cbutton.which += 999; up.cbutton.which = down.cbutton.which;
    CHECK(SDL_PushEvent(&down) == 1 && SDL_PushEvent(&up) == 1, "queue foreign controller events");
    vita_platform_poll(&platform, &input);
    CHECK(!input.pause_pressed, "foreign controller ignored");

    button(joystick, 4, 1); button(joystick, 5, 1); button(joystick, 3, 1); button(joystick, 2, 1); button(joystick, 0, 1);
    axis(joystick, 0, -32768, "set left stick");
    vita_platform_poll(&platform, &input);
    CHECK(input.brake == 1 && input.throttle == 1 && input.handbrake && input.nitro_confirm, "held action buttons");
    CHECK(input.camera_pressed && input.confirm_pressed, "Triangle/Cross press edges");
    CHECK(fabsf(input.steering - 1.0f) < 0.0001f, "left steering positive");
    axis(joystick, 0, 32767, "set right stick");
    vita_platform_poll(&platform, &input);
    CHECK(fabsf(input.steering + 1.0f) < 0.0001f, "right steering negative");
    CHECK(!input.camera_pressed && !input.confirm_pressed, "held actions do not retrigger");
    axis(joystick, 0, 1000, "set deadzone");
    vita_platform_poll(&platform, &input);
    CHECK(input.steering == 0, "deadzone");

    axis(joystick, 1, -32768, "set pitch up");
    vita_platform_poll(&platform, &input);
    CHECK(fabsf(input.camera_pitch - 1.0f) < 0.0001f, "leftY negative extreme pitches up");
    axis(joystick, 1, 32767, "set pitch down");
    vita_platform_poll(&platform, &input);
    CHECK(fabsf(input.camera_pitch + 1.0f) < 0.0001f, "leftY positive extreme pitches down");
    axis(joystick, 3, -32768, "set zoom out");
    vita_platform_poll(&platform, &input);
    CHECK(fabsf(input.camera_zoom + 1.0f) < 0.0001f, "rightY negative extreme zooms out");
    axis(joystick, 3, 32767, "set zoom in");
    vita_platform_poll(&platform, &input);
    CHECK(fabsf(input.camera_zoom - 1.0f) < 0.0001f, "rightY positive extreme zooms in");
    axis(joystick, 1, 1000, "set pitch deadzone");
    axis(joystick, 3, -1000, "set zoom deadzone");
    vita_platform_poll(&platform, &input);
    CHECK(input.camera_pitch == 0 && input.camera_zoom == 0, "camera axis deadzones");

    button(joystick, 10, 1); button(joystick, 11, 1);
    vita_platform_poll(&platform, &input);
    CHECK(input.quit && !input.pause_pressed, "Select+Start exits without toggling pause");
    CHECK(SDL_JoystickDetachVirtual(device) == 0, "detach controller");
    vita_platform_poll(&platform, &input);
    CHECK(platform.controller == NULL && platform.previous_buttons == 0, "disconnect clears controller state");
    vita_platform_close(&platform);
    printf("Vita input host checks: %d passed, %d failed\n", checks - failures, failures);
    return failures ? 1 : 0;
}
