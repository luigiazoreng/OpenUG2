#include "render.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static GLuint injected_fragment;
static int injected_source;
static int real_fragment_compile_failed;
static int optimistic_status_returned;
static int fragment_log_queried_live;
static int fragment_log_nonempty;
static int program_link_failure_logged;
static int diagnostic_contains_shader_log;
static unsigned shader_compile_phase_logs;
static unsigned shader_status_logs;
static unsigned program_link_phase_logs;
static unsigned program_link_status_logs;
static char diagnostics[16384];
static size_t diagnostics_used;

extern void __real_glShaderSource(GLuint shader, GLsizei count,
                                  const GLchar *const *string,
                                  const GLint *length);
extern void __real_glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
extern void __real_glGetShaderInfoLog(GLuint shader, GLsizei maxLength,
                                      GLsizei *length, GLchar *infoLog);

void __wrap_glShaderSource(GLuint shader, GLsizei count,
                           const GLchar *const *string, const GLint *length) {
    GLint type = 0;
    __real_glGetShaderiv(shader, GL_SHADER_TYPE, &type);
    if (type == GL_FRAGMENT_SHADER) {
        static const GLchar invalid_fragment[] =
            "void main() { gl_FragColor = ; }\n";
        const GLchar *bad_source[] = {invalid_fragment};
        injected_fragment = shader;
        injected_source = 1;
        __real_glShaderSource(shader, 1, bad_source, NULL);
        return;
    }
    __real_glShaderSource(shader, count, string, length);
}

void __wrap_glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    __real_glGetShaderiv(shader, pname, params);
    if (shader == injected_fragment && pname == GL_COMPILE_STATUS) {
        real_fragment_compile_failed = (*params == GL_FALSE);
        *params = GL_TRUE; /* Model VitaGL's deferred/optimistic compile status. */
        optimistic_status_returned = 1;
    }
}

void __wrap_glGetShaderInfoLog(GLuint shader, GLsizei maxLength,
                               GLsizei *length, GLchar *infoLog) {
    if (shader == injected_fragment) {
        fragment_log_queried_live = glIsShader(shader) == GL_TRUE;
        if (!fragment_log_queried_live) {
            if (length) *length = 0;
            if (maxLength > 0 && infoLog) infoLog[0] = '\0';
            return;
        }
    }
    __real_glGetShaderInfoLog(shader, maxLength, length, infoLog);
    if (shader == injected_fragment && infoLog && infoLog[0])
        fragment_log_nonempty = 1;
}

static void capture_diagnostic(const char *format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    int line_len = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (line_len < 0) return;
    size_t line_bytes = strlen(line);
    if (diagnostics_used < sizeof diagnostics) {
        if (line_bytes > 0) {
            size_t available = sizeof diagnostics - diagnostics_used;
            size_t copied = line_bytes < available ? line_bytes : available - 1;
            memcpy(diagnostics + diagnostics_used, line, copied);
            diagnostics_used += copied;
            diagnostics[diagnostics_used] = '\0';
        }
    }
    if (strstr(line, "RENDER PROGRAM FAIL"))
        program_link_failure_logged = 1;
    if (strstr(line, "RENDER SHADER") && strstr(line, "phase=compile"))
        shader_compile_phase_logs++;
    if (strstr(line, "RENDER SHADER") && strstr(line, "phase=status compiled=1"))
        shader_status_logs++;
    if (strstr(line, "RENDER PROGRAM") && strstr(line, "phase=link\n"))
        program_link_phase_logs++;
    if (strstr(line, "RENDER PROGRAM") && strstr(line, "phase=link_status linked=0"))
        program_link_status_logs++;
    const char *log = strstr(line, "RENDER LINK SHADER");
    if (log) {
        log = strstr(log, "log=");
        if (log && log[4] && log[4] != '\n') diagnostic_contains_shader_log = 1;
    }
}

static int fail(const char *reason) {
    fprintf(stderr, "render_link_failure_test: FAIL: %s\n", reason);
    if (diagnostics_used) fprintf(stderr, "%s", diagnostics);
    return 1;
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
    SDL_Window *window = SDL_CreateWindow("render link failure", 0, 0, 64, 64,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context) {
        fprintf(stderr, "GL context: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("HOST GL vendor=%s renderer=%s version=%s\n",
           glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    while (glGetError() != GL_NO_ERROR) { }
    render_set_diagnostic_logger(capture_diagnostic);
    RProg program = render_program();
    render_set_diagnostic_logger(NULL);

    if (program.prog != 0) {
        render_use_program(0);
        render_delete_program(program.prog);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return fail("failed link returned a live RProg");
    }
    if (!injected_source || !injected_fragment)
        return fail("fragment source was not intercepted");
    if (!real_fragment_compile_failed || !optimistic_status_returned)
        return fail("fixture did not prove real failure plus optimistic status");
    if (!program_link_failure_logged)
        return fail("renderer did not log link failure");
    if (shader_compile_phase_logs < 2 || shader_status_logs < 2 ||
        program_link_phase_logs != 1 || program_link_status_logs != 1)
        return fail("renderer phase/status diagnostics were incomplete");
    if (!fragment_log_queried_live || !fragment_log_nonempty)
        return fail("fragment diagnostic was not read while shader remained live");
    if (!diagnostic_contains_shader_log)
        return fail("renderer diagnostic did not include shader-log text");
    if (glIsShader(injected_fragment) == GL_TRUE)
        return fail("failed-link shader object was not released");
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "unexpected GL error after rollback: 0x%x\n", (unsigned)error);
        return fail("GL state was not clean after rollback");
    }

    printf("render_link_failure_test: PASS injected=fragment compile_real=FAIL "
           "compile_status=optimistic link=FAIL shader_log=live/nonempty "
           "RProg=0 shader_released=1 GL_ERROR=0\n");
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
