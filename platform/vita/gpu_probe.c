#include "gpu_probe.h"
#include "platform.h"
#include "render.h"

#ifdef N2_GLES
#define PROBE_GLSL "precision mediump float;\n"
#else
#define PROBE_GLSL "#version 120\n"
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

typedef struct {
    GLuint program, vbo, ibo, fbo, color, depth, texture;
    GLint use_tex, color_uniform, z;
    int passed, failed, unsupported;
} Probe;

static GLuint probe_shader(GLenum kind, const char *source) {
    GLuint shader = glCreateShader(kind);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL); glCompileShader(shader);
    GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[2048] = {0}; glGetShaderInfoLog(shader, sizeof info, NULL, info);
        vita_log("GPU PROBE shader_compile FAIL kind=%u log=%s\n", (unsigned)kind, info);
        glDeleteShader(shader); return 0;
    }
    return shader;
}

static int probe_init(Probe *p) {
    glActiveTexture(GL_TEXTURE0);
    const char *vs = PROBE_GLSL
        "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV; uniform float uZ;"
        "void main(){ vUV=aUV; gl_Position=vec4(aPos,uZ,1.0); }";
    const char *fs = PROBE_GLSL
        "varying vec2 vUV; uniform sampler2D uTex; uniform float uUseTex; uniform vec3 uColor;"
        "void main(){ gl_FragColor=mix(vec4(uColor,1.0),texture2D(uTex,vUV),uUseTex); }";
    GLuint vertex = probe_shader(GL_VERTEX_SHADER, vs), fragment = probe_shader(GL_FRAGMENT_SHADER, fs);
    if (vertex && fragment) {
        p->program = glCreateProgram(); glAttachShader(p->program, vertex); glAttachShader(p->program, fragment);
        glBindAttribLocation(p->program, 0, "aPos"); glBindAttribLocation(p->program, 1, "aUV");
        glLinkProgram(p->program);
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    GLint linked = 0;
    if (p->program) glGetProgramiv(p->program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[2048] = {0};
        if (p->program) glGetProgramInfoLog(p->program, sizeof info, NULL, info);
        vita_log("GPU PROBE simple_program FAIL log=%s\n", info); return 0;
    }
    glUseProgram(p->program);
    p->use_tex = glGetUniformLocation(p->program, "uUseTex");
    p->color_uniform = glGetUniformLocation(p->program, "uColor");
    p->z = glGetUniformLocation(p->program, "uZ");
    glUniform1i(glGetUniformLocation(p->program, "uTex"), 0);
    const GLfloat vertices[] = {-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    const GLushort indices[] = {0,1,2, 0,2,3};
    glGenBuffers(1, &p->vbo); glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    glGenBuffers(1, &p->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);
    glGenTextures(1, &p->color); glBindTexture(GL_TEXTURE_2D, p->color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &p->depth); glBindRenderbuffer(GL_RENDERBUFFER, p->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 8, 8);
    glGenFramebuffers(1, &p->fbo); glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, p->depth);
    GLenum state = glCheckFramebufferStatus(GL_FRAMEBUFFER), error = glGetError();
    if (state != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR) {
        vita_log("GPU PROBE framebuffer FAIL status=0x%x error=0x%x\n", (unsigned)state, (unsigned)error); return 0;
    }
    glGenTextures(1, &p->texture); glBindTexture(GL_TEXTURE_2D, p->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glViewport(0, 0, 8, 8); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    return glGetError() == GL_NO_ERROR;
}

static void probe_draw(Probe *p, int textured, float r, float g, float b, float z) {
    glUniform1f(p->use_tex, (float)textured); glUniform3f(p->color_uniform, r, g, b);
    glUniform1f(p->z, z); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
}

static void probe_check(Probe *p, const char *name, int x, int y, int r, int g, int b) {
    unsigned char pixel[4] = {0}; glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum error = glGetError();
    int ok = error == GL_NO_ERROR && abs((int)pixel[0]-r) <= 8 &&
        abs((int)pixel[1]-g) <= 8 && abs((int)pixel[2]-b) <= 8;
    if (ok) p->passed++; else p->failed++;
    vita_log("GPU PROBE %s %s rgba=%u,%u,%u,%u expected_rgb=%d,%d,%d error=0x%x\n",
        name, ok ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2], pixel[3], r, g, b, (unsigned)error);
}

/* Disable blending for this readback: a zero-alpha sample must not be
   indistinguishable from a quad that never touched the blue framebuffer. */
static void probe_raw_check(Probe *p, const char *name, int r, int g, int b, int a) {
    unsigned char pixel[4] = {0};
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum error = glGetError();
    int ok = error == GL_NO_ERROR && abs((int)pixel[0]-r) <= 8 &&
        abs((int)pixel[1]-g) <= 8 && abs((int)pixel[2]-b) <= 8 &&
        abs((int)pixel[3]-a) <= 8;
    if (ok) p->passed++; else p->failed++;
    vita_log("GPU PROBE %s %s rgba=%u,%u,%u,%u expected_rgba=%d,%d,%d,%d error=0x%x\n",
        name, ok ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2], pixel[3], r, g, b, a, (unsigned)error);
}

static void probe_upload_bytes(const unsigned char *block, int bytes, int format) {
#ifdef OPENUG2_VITA
    /* A 4x4 DXT image contains one block, so swizzling cannot reorder it.
       This diagnostic observes the backend buffer without accepting pixels
       based on its contents. The GPU readback remains the pass criterion. */
    glFinish();
    const unsigned char *stored = vglGetTexDataPointer(GL_TEXTURE_2D);
    vita_log("GPU UPLOAD dxt%d one_block_bytes=%d matches_source=%d stored=", format, bytes,
        stored && memcmp(stored, block, (size_t)bytes) == 0);
    if (stored) for (int i = 0; i < bytes; i++) vita_log("%02x", stored[i]);
    vita_log("\n");
#else
    (void)block; (void)bytes; (void)format;
#endif
}

static void probe_clear(void) {
    glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void probe_rgba(Probe *p, const unsigned char *rgba, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, p->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
}

static void probe_dxt(Probe *p, int format, int native) {
    /* One red block; DXT3/5 carry approximately half alpha. */
    unsigned char block[16] = {0}, rgb[4*4*3], alpha[4*4], rgba[4*4*4];
    int offset = format == 1 ? 0 : 8, bytes = format == 1 ? 8 : 16;
    block[offset+1] = 0xf8; /* c0 = 0xf800, c1 = 0, every selector = 0. */
    if (format == 3) memset(block, 0x88, 8);
    if (format == 5) { block[0] = 128; block[1] = 0; }
    if (format == 1) n2_dxt1(block, 4, 4, rgb, alpha);
    else if (format == 3) n2_dxt3(block, 4, 4, rgb, alpha);
    else n2_dxt5(block, 4, 4, rgb, alpha);
    for (int i = 0; i < 16; i++) {
        memcpy(rgba+i*4, rgb+i*3, 3); rgba[i*4+3] = alpha[i];
    }
    char name[48]; snprintf(name, sizeof name, "dxt%d_rgba_fallback", format);
    probe_rgba(p, rgba, 4, 4); probe_clear(); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); probe_draw(p, 1, 1, 1, 1, 0);
    probe_check(p, name, 4, 4, alpha[0], 0, 255-alpha[0]); glDisable(GL_BLEND);
    if (!native) {
        p->unsupported += 2;
        vita_log("GPU PROBE dxt%d_native_raw UNSUPPORTED capability_not_advertised\n", format);
        vita_log("GPU PROBE dxt%d_native UNSUPPORTED capability_not_advertised\n", format); return;
    }
    GLenum type = format == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
        format == 3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, type, 4, 4, 0, bytes, block);
    GLenum error = glGetError();
    if (error) {
        p->failed++; vita_log("GPU PROBE dxt%d_native FAIL upload_error=0x%x\n", format, (unsigned)error); return;
    }
    probe_upload_bytes(block, bytes, format);
    glDisable(GL_BLEND); probe_clear(); probe_draw(p, 1, 1, 1, 1, 0);
    snprintf(name, sizeof name, "dxt%d_native_raw", format);
    probe_raw_check(p, name, 255, 0, 0, alpha[0]);
    probe_clear(); glEnable(GL_BLEND); probe_draw(p, 1, 1, 1, 1, 0);
    snprintf(name, sizeof name, "dxt%d_native", format);
    probe_check(p, name, 4, 4, alpha[0], 0, 255-alpha[0]); glDisable(GL_BLEND);
}

static void probe_arrays(Probe *p) {
    const char *vs = PROBE_GLSL "attribute vec2 aPos; void main(){gl_Position=vec4(aPos,0.0,1.0);}";
    const char *fs = PROBE_GLSL "uniform vec3 uArray[2]; void main(){gl_FragColor=vec4(uArray[0]+uArray[1],1.0);}";
    GLuint vertex = probe_shader(GL_VERTEX_SHADER, vs), fragment = probe_shader(GL_FRAGMENT_SHADER, fs), program = 0;
    if (vertex && fragment) {
        program = glCreateProgram(); glAttachShader(program, vertex); glAttachShader(program, fragment);
        glBindAttribLocation(program, 0, "aPos"); glLinkProgram(program);
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    GLint linked = 0;
    if (program) glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked) {
        GLint base = render_uniform_location(program, "uArray[0]"), second = render_uniform_location(program, "uArray[1]");
        if (base != -1 && second != -1 && base != second) {
            const GLfloat colors[6] = {1,0,0, 0,1,0};
            render_use_program(program); render_uniform3fv(base, 2, colors);
            probe_clear(); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            probe_check(p, "uniform_arrays", 4, 4, 255, 255, 0);
            const GLfloat blue[3] = {0,0,1}; render_uniform3fv(second, 1, blue);
            probe_clear(); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
            probe_check(p, "uniform_array_element", 4, 4, 255, 0, 255);
        } else {
            p->failed++; vita_log("GPU PROBE uniform_arrays FAIL base=%d second=%d; build vitaGL with SAFE_UNIFORMS=1\n", base, second);
        }
    } else {
        p->failed++; vita_log("GPU PROBE uniform_arrays FAIL program_link\n");
    }
    render_use_program(0); if (program) render_delete_program(program);
    glUseProgram(p->program);
}

static void probe_dxt1_cutout(Probe *p, int native) {
    const unsigned char block[8] = {0,0,0,0xf8, 255,255,255,255};
    unsigned char rgb[48], alpha[16], rgba[64];
    n2_dxt1(block, 4, 4, rgb, alpha);
    for (int i = 0; i < 16; i++) { memcpy(rgba+i*4, rgb+i*3, 3); rgba[i*4+3] = alpha[i]; }
    probe_rgba(p, rgba, 4, 4); probe_clear(); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); probe_draw(p, 1, 1, 1, 1, 0);
    probe_check(p, "dxt1_cutout_rgba", 4, 4, 0, 0, 255); glDisable(GL_BLEND);
    if (!native) {
        p->unsupported += 2;
        vita_log("GPU PROBE dxt1_cutout_native_raw UNSUPPORTED capability_not_advertised\n");
        vita_log("GPU PROBE dxt1_cutout_native UNSUPPORTED capability_not_advertised\n"); return;
    }
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, 4, 0, sizeof block, block);
    probe_upload_bytes(block, sizeof block, 1);
    glDisable(GL_BLEND); probe_clear(); probe_draw(p, 1, 1, 1, 1, 0);
    probe_raw_check(p, "dxt1_cutout_native_raw", 0, 0, 0, 0);
    probe_clear(); glEnable(GL_BLEND); probe_draw(p, 1, 1, 1, 1, 0);
    probe_check(p, "dxt1_cutout_native", 4, 4, 0, 0, 255); glDisable(GL_BLEND);
}

static void probe_destroy(Probe *p) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0); glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
    if (p->texture) glDeleteTextures(1, &p->texture);
    if (p->color) glDeleteTextures(1, &p->color);
    if (p->depth) glDeleteRenderbuffers(1, &p->depth);
    if (p->fbo) glDeleteFramebuffers(1, &p->fbo);
    if (p->vbo) glDeleteBuffers(1, &p->vbo);
    if (p->ibo) glDeleteBuffers(1, &p->ibo);
    glUseProgram(0); if (p->program) glDeleteProgram(p->program);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0); glViewport(0, 0, VITA_WIDTH, VITA_HEIGHT);
}

int vita_gpu_probe_run(void) {
    Uint64 begin = SDL_GetPerformanceCounter();
    GLint units = 0, size = 0, vertex = 0, fragment = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units); glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);
    GLenum limit_error = glGetError();
    if (limit_error) vita_log("GPU CAPS required_limits error=0x%x\n", (unsigned)limit_error);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vertex); glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fragment);
    GLenum optional_error = glGetError();
    if (optional_error) vita_log("GPU CAPS uniform_vector_query UNAVAILABLE error=0x%x\n", (unsigned)optional_error);
    vita_log("GPU CAPS texture_units=%d max_texture=%d vertex_uniform_vectors=%d fragment_uniform_vectors=%d\n", units, size, vertex, fragment);
#if OPENUG2_VITA_CAR_SHADER
    vita_log("GPU SHADER PROFILE static_car full_engine=deferred\n");
    const char *program_probe = "viewer_program";
    RProg engine = render_program_static_car();
#else
    const char *program_probe = "engine_program";
    RProg engine = render_program();
#endif
    GLint linked = 0;
    if (engine.prog) glGetProgramiv(engine.prog, GL_LINK_STATUS, &linked);
    GLenum engine_error = glGetError();
    int engine_ok = linked && units >= 6 && !limit_error && engine_error == GL_NO_ERROR;
    if (!engine_ok) {
        char info[2048] = {0}; if (engine.prog) glGetProgramInfoLog(engine.prog, sizeof info, NULL, info);
        vita_log("GPU PROBE %s FAIL linked=%d units=%d log=%s\n", program_probe, linked, units, info);
    } else vita_log("GPU PROBE %s PASS linked=1\n", program_probe);
    /* Uniform-state wrapper is also responsible for program-cache retirement. */
    render_use_program(0); if (engine.prog) render_delete_program(engine.prog);
    Probe p = {0}; p.passed = engine_ok; p.failed = !engine_ok;
    if (probe_init(&p)) {
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); probe_clear();
        probe_draw(&p, 0, 1, 0, 0, -.5f); probe_draw(&p, 0, 0, 0, 1, .5f);
        probe_check(&p, "framebuffer_depth", 4, 4, 255, 0, 0); glDisable(GL_DEPTH_TEST);
        unsigned char rgba[16] = {255,0,0,128, 255,0,0,128, 255,0,0,128, 255,0,0,128};
        probe_rgba(&p, rgba, 2, 2); probe_clear(); glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); probe_draw(&p, 1, 1, 1, 1, 0);
        probe_check(&p, "rgba_alpha", 4, 4, 128, 0, 127); glDisable(GL_BLEND);
        probe_arrays(&p);
        const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
        int native = extensions && strstr(extensions, "GL_EXT_texture_compression_s3tc");
        probe_dxt(&p, 1, native); probe_dxt(&p, 3, native); probe_dxt(&p, 5, native);
        probe_dxt1_cutout(&p, native);
        /* Hand-authored mip colors prove sampling of level 4, not just API success. */
        unsigned char pixels[16*16*4];
        const unsigned char colors[5][4] = {{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255},{0,255,255,255}};
        for (int level = 0, side = 16; level < 5; level++, side /= 2) {
            for (int i = 0; i < side*side; i++) memcpy(pixels+i*4, colors[level], 4);
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glViewport(0, 0, 1, 1); probe_clear(); probe_draw(&p, 1, 1, 1, 1, 0);
        probe_check(&p, "mip_sampling", 0, 0, 0, 255, 255);
        for (int i = 0; i < 16*16; i++) memcpy(pixels+i*4, colors[0], 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D); probe_clear(); probe_draw(&p, 1, 1, 1, 1, 0);
        probe_check(&p, "mip_generation", 0, 0, 255, 0, 0);
    } else p.failed++;
    probe_destroy(&p);
    double ms = (SDL_GetPerformanceCounter()-begin)*1000.0/SDL_GetPerformanceFrequency();
    vita_log("GPU SUITE passed=%d failed=%d unsupported=%d elapsed_ms=%.3f\n", p.passed, p.failed, p.unsupported, ms);
    return p.failed == 0;
}
