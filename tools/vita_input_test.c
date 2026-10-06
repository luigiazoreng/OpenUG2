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

    down.cbutton.which += 999; up.cbutton.which = down.cbutton.which;
    CHECK(SDL_PushEvent(&down) == 1 && SDL_PushEvent(&up) == 1, "queue foreign controller events");
    vita_platform_poll(&platform, &input);
    CHECK(!input.pause_pressed, "foreign controller ignored");

    button(joystick, 4, 1); button(joystick, 5, 1); button(joystick, 3, 1); button(joystick, 2, 1); button(joystick, 0, 1);
    CHECK(SDL_JoystickSetVirtualAxis(joystick, 0, -32768) == 0, "set left stick");
    SDL_JoystickUpdate(); vita_platform_poll(&platform, &input);
    CHECK(input.brake == 1 && input.throttle == 1 && input.handbrake && input.nitro_confirm, "held action buttons");
    CHECK(input.camera_pressed && input.confirm_pressed, "Triangle/Cross press edges");
    CHECK(fabsf(input.steering - 1.0f) < 0.0001f, "left steering positive");
    CHECK(SDL_JoystickSetVirtualAxis(joystick, 0, 32767) == 0, "set right stick");
    SDL_JoystickUpdate(); vita_platform_poll(&platform, &input);
    CHECK(fabsf(input.steering + 1.0f) < 0.0001f, "right steering negative");
    CHECK(!input.camera_pressed && !input.confirm_pressed, "held actions do not retrigger");
    CHECK(SDL_JoystickSetVirtualAxis(joystick, 0, 1000) == 0, "set deadzone");
    SDL_JoystickUpdate(); vita_platform_poll(&platform, &input);
    CHECK(input.steering == 0, "deadzone");

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
