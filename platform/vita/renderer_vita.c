#include "renderer_vita.h"
#include "platform.h"
#include <vitaGL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct VitaRenderer { GLuint program, vbo, ibo, texture; GLint mvp, color; };
typedef struct { float xyz[3], uv[2]; } ProbeVertex;

/* Same attribute/varying/matrix/texture idioms as OpenUG2's basic path.
   This deliberately does not assert compatibility of the full lighting shader. */
static const char vertex_source[] =
    "attribute vec3 aPos; attribute vec2 aUV; uniform mat4 uMVP;"
    "varying vec2 vUV; void main(){vUV=aUV;gl_Position=uMVP*vec4(aPos,1.0);}";
static const char fragment_source[] =
    "precision mediump float; uniform sampler2D uTex; uniform vec4 uColor;"
    "varying vec2 vUV; void main(){gl_FragColor=texture2D(uTex,vUV)*uColor;}";
static const ProbeVertex vertices[] = {
    {{-0.65f,-0.65f,0}, {0,1}}, {{0.65f,-0.65f,0}, {1,1}}, {{0,0.65f,0}, {0.5f,0}}
};
static const uint16_t indices[] = {0,1,2};
static const unsigned char pixels[] = {
    255,255,255,255, 255,120,30,255, 255,120,30,255, 255,255,255,255
};

static GLuint shader_create(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL); glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        vita_log("shader type=0x%x failed: %s\n", (unsigned)type, log);
        glDeleteShader(shader); return 0;
    }
    return shader;
}

VitaRenderer *vita_renderer_create(void) {
    const GLubyte *version = glGetString(GL_VERSION);
    const GLubyte *extensions = glGetString(GL_EXTENSIONS);
    vita_log("GL version=%s\nextensions=%s\n", version ? (const char *)version : "unavailable",
             extensions ? (const char *)extensions : "unavailable");
    VitaRenderer *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    GLuint vertex = shader_create(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = shader_create(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        free(r); return NULL;
    }
    r->program = glCreateProgram();
    if (r->program) {
        glAttachShader(r->program, vertex); glAttachShader(r->program, fragment);
        glBindAttribLocation(r->program, 0, "aPos"); glBindAttribLocation(r->program, 1, "aUV");
        glLinkProgram(r->program);
    }
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint linked = 0;
    if (r->program) glGetProgramiv(r->program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048] = {0};
        if (r->program) glGetProgramInfoLog(r->program, sizeof log, NULL, log);
        vita_log("shader link failed: %s\n", log); goto fail;
    }
    r->mvp = glGetUniformLocation(r->program, "uMVP");
    r->color = glGetUniformLocation(r->program, "uColor");
    GLint sampler = glGetUniformLocation(r->program, "uTex");
    if (r->mvp < 0 || r->color < 0 || sampler < 0) { vita_log("required uniforms missing\n"); goto fail; }
    glUseProgram(r->program); glUniform1i(sampler, 0);
    glGenBuffers(1, &r->vbo); glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    glGenBuffers(1, &r->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);
    glActiveTexture(GL_TEXTURE0); glGenTextures(1, &r->texture);
    glBindTexture(GL_TEXTURE_2D, r->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLenum error = glGetError();
    if (!r->vbo || !r->ibo || !r->texture || error != GL_NO_ERROR) {
        vita_log("GPU resource upload failed: 0x%x\n", (unsigned)error); goto fail;
    }
    vita_log("GLSL + VBO + u16 IBO + RGBA texture: initialized (device visibility still requires inspection)\n");
    return r;
fail:
    vita_renderer_destroy(r); return NULL;
}

int vita_renderer_draw(VitaRenderer *r, int asset_ok, int paused, int loading) {
    const float identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    glViewport(0, 0, VITA_WIDTH, VITA_HEIGHT);
    if (paused) glClearColor(0.02f, 0.05f, 0.45f, 1); /* Explicit pause diagnostic. */
    else glClearColor(asset_ok ? 0.02f : 0.14f, asset_ok ? 0.13f : 0.02f, 0.05f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glUseProgram(r->program); glUniformMatrix4fv(r->mvp, 1, GL_FALSE, identity);
    glUniform4f(r->color, 1, 1, paused ? 0.3f : 1.0f, 1);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->texture);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ibo);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ProbeVertex), (const void *)offsetof(ProbeVertex, xyz));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ProbeVertex), (const void *)offsetof(ProbeVertex, uv));
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
    if (loading) {
        /* CPU retry progress indicator, with no extra allocations/draw calls. */
        glEnable(GL_SCISSOR_TEST); glScissor(0, 0, VITA_WIDTH, 8);
        glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) { vita_log("draw failed: 0x%x\n", (unsigned)error); return 0; }
    return 1;
}

void vita_renderer_destroy(VitaRenderer *r) {
    if (!r) return;
    glFinish();
    glUseProgram(0); glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
    if (r->texture) glDeleteTextures(1, &r->texture);
    if (r->vbo) glDeleteBuffers(1, &r->vbo);
    if (r->ibo) glDeleteBuffers(1, &r->ibo);
    if (r->program) glDeleteProgram(r->program);
    free(r);
}

size_t vita_renderer_estimated_bytes(void) {
    return sizeof vertices + sizeof indices + sizeof pixels;
}

void vita_renderer_log_pools(void) {
    vita_log("vgl_free_bytes: cdram=%lu ram=%lu phycont=%lu (pool free space, not process RAM/peak VRAM)\n",
        (unsigned long)vglMemFree(VGL_MEM_VRAM), (unsigned long)vglMemFree(VGL_MEM_RAM),
        (unsigned long)vglMemFree(VGL_MEM_PHYCONT));
}
