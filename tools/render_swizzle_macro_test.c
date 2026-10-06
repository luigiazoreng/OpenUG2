#include "render.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void __real_glShaderSource(GLuint shader, GLsizei count,
                                  const GLchar *const *string,
                                  const GLint *length);
extern void __real_glGetShaderiv(GLuint shader, GLenum pname, GLint *params);

static GLuint fragment_shader;
static int macros_injected;
static int fragment_compiled;
static int shader_failure_logged;
static int program_failure_logged;
static int program_linked;
static char shader_log[4096];

static const char macros[] =
    "#define s x\n"
    "#define t y\n"
    "#define st xy\n"
    "#define ts yx\n"
    "#define ss xx\n"
    "#define tt yy\n";

void __wrap_glShaderSource(GLuint shader, GLsizei count,
                           const GLchar *const *string, const GLint *length) {
    GLint type = 0;
    __real_glGetShaderiv(shader, GL_SHADER_TYPE, &type);
    if (type != GL_FRAGMENT_SHADER || count <= 0 || !string || !string[0]) {
        __real_glShaderSource(shader, count, string, length);
        return;
    }

    size_t input_len = 0;
    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i]) continue;
        if (length && length[i] >= 0) input_len += (size_t)length[i];
        else input_len += strlen(string[i]);
    }
    char *input = malloc(input_len + 1);
    if (!input) {
        __real_glShaderSource(shader, count, string, length);
        return;
    }
    size_t at = 0;
    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i]) continue;
        size_t n = (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
        memcpy(input + at, string[i], n);
        at += n;
    }
    input[at] = '\0';

    /* GLSL requires #version to remain the first directive. Mirror all six
     * component aliases from VitaGL's pinned glsl_translator_hdr.h. */
    char *newline = strchr(input, '\n');
    if (!newline) {
        __real_glShaderSource(shader, count, string, length);
        free(input);
        return;
    }
    size_t prefix_len = (size_t)(newline + 1 - input);
    size_t macros_len = sizeof(macros) - 1;
    char *injected = malloc(input_len + macros_len + 1);
    if (!injected) {
        __real_glShaderSource(shader, count, string, length);
        free(input);
        return;
    }
    memcpy(injected, input, prefix_len);
    memcpy(injected + prefix_len, macros, macros_len);
    memcpy(injected + prefix_len + macros_len, input + prefix_len,
           input_len - prefix_len + 1);
    const GLchar *sources[] = {injected};
    fragment_shader = shader;
    macros_injected = 1;
    __real_glShaderSource(shader, 1, sources, NULL);
    free(injected);
    free(input);
}

void __wrap_glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    __real_glGetShaderiv(shader, pname, params);
    if (shader == fragment_shader && pname == GL_COMPILE_STATUS) {
        fragment_compiled = *params == GL_TRUE;
    }
}

static void capture_diagnostic(const char *format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n < 0) return;
    if (strstr(line, "RENDER SHADER") && strstr(line, "FAIL log=")) {
        shader_failure_logged = 1;
        const char *log = strstr(line, "FAIL log=");
        if (log) snprintf(shader_log, sizeof shader_log, "%s", log + 9);
    }
    if (strstr(line, "RENDER PROGRAM FAIL")) program_failure_logged = 1;
}

static void cleanup(SDL_GLContext context, SDL_Window *window) {
    if (context) SDL_GL_DeleteContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init: %s\n", SDL_GetError());
        return 1;
    }
#ifdef N2_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_Window *window = SDL_CreateWindow("VitaGL swizzle macro regression",
        0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context) {
        fprintf(stderr, "GL context: %s\n", SDL_GetError());
        cleanup(NULL, window);
        return 1;
    }
    printf("HOST GL vendor=%s renderer=%s version=%s\n",
        glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    while (glGetError() != GL_NO_ERROR) { }
    render_set_diagnostic_logger(capture_diagnostic);
    RProg program = render_program();
    render_set_diagnostic_logger(NULL);
    if (program.prog) {
        GLint linked = GL_FALSE;
        glGetProgramiv(program.prog, GL_LINK_STATUS, &linked);
        program_linked = linked == GL_TRUE;
        render_delete_program(program.prog);
    }

    if (!macros_injected || !fragment_shader) {
        fprintf(stderr, "FAIL: fragment macro injection did not run\n");
        cleanup(context, window);
        return 1;
    }
    if (!fragment_compiled || program.prog == 0 || !program_linked ||
        shader_failure_logged || program_failure_logged) {
        fprintf(stderr, "FAIL: expected fragment compile and program link success; compiled=%d linked=%d shader-log=%s\n",
            fragment_compiled, program_linked, shader_log);
        cleanup(context, window);
        return 1;
    }
    printf("PASS: fragment compile and program link succeeded with all six injected VitaGL aliases\n");
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "FAIL: GL error 0x%x\n", (unsigned)error);
        cleanup(context, window);
        return 1;
    }
    cleanup(context, window);
    return 0;
}
