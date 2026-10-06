#include <stdarg.h>
#include <stdio.h>
#include "render.h"
#include "gpu_probe.h"

/* Same probe implementation as the console; only its platform logger differs. */
void vita_log(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args);
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL init: %s\n", SDL_GetError()); return 1;
    }
#ifdef N2_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_Window *window = SDL_CreateWindow("GPU probes", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context) {
        fprintf(stderr, "GL context: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 1;
    }
    printf("HOST GL vendor=%s renderer=%s version=%s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    int ok = vita_gpu_probe_run();
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return ok ? 0 : 1;
}
