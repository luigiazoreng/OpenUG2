#include "car_viewer.h"
#include "car_scene.h"
#include "render.h"
#include "platform.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define CAR_GPU_BUDGET (24u * 1024u * 1024u)
#define CAR_MAX_TEXTURES 128

typedef struct { GLuint id; uint32_t key; int decal, mode; } CarTexture;
struct CarViewer {
    CarScene car;
    RProg program;
    GpuMesh *gpu, *caption, floor;
    CarTexture textures[CAR_MAX_TEXTURES];
    int texture_count;
    int *glass_order;
    CarViewerStats stats;
};

static int caption_scene_build(const char *text, N2Scene *scene,
                               size_t *gpu_bytes) {
    const float x0 = -.98f, y0 = .96f, px = .006f, py = .011f;
    size_t pixels = 0, chars = strlen(text);
    memset(scene, 0, sizeof *scene);
    *gpu_bytes = 0;
    for (size_t c = 0; c < chars; c++) {
        const unsigned char *glyph = render_font_glyph(text[c]);
        if (!glyph) return 0;
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (glyph[row] & (4 >> col)) pixels++;
    }
    /* u16 indices can address 65,536 vertices, but keep this mesh strictly
       below that vertex count for the GLES2-friendly caption buffer. */
    if (!pixels || pixels > (size_t)UINT16_MAX / 4u) return 0;
    size_t nverts = pixels * 4u, nidx = pixels * 6u;
    if (nverts >= 65536u || nverts > (size_t)INT_MAX || nidx > (size_t)INT_MAX ||
        nverts > SIZE_MAX / (5u * sizeof(float)) ||
        nverts > SIZE_MAX / (3u * sizeof(float)) ||
        nidx > SIZE_MAX / sizeof(uint16_t)) return 0;
    size_t vertices_bytes = nverts * (8u * sizeof(float));
    size_t indices_bytes = nidx * sizeof(uint16_t);
    if (vertices_bytes > SIZE_MAX - indices_bytes) return 0;
    *gpu_bytes = vertices_bytes + indices_bytes;

    scene->meshes = calloc(1, sizeof *scene->meshes);
    if (!scene->meshes) return 0;
    scene->count = scene->cap = 1;
    N2Mesh *mesh = scene->meshes;
    mesh->verts = malloc(nverts * 5u * sizeof(float));
    mesh->idx = malloc(nidx * sizeof(uint16_t));
    if (!mesh->verts || !mesh->idx) { n2_free_scene(scene); return 0; }
    mesh->nverts = (int)nverts; mesh->nidx = (int)nidx;
    size_t vertex = 0, index = 0;
    for (size_t c = 0; c < chars; c++) {
        const unsigned char *glyph = render_font_glyph(text[c]);
        float char_x = x0 + (float)c * 4.0f * px;
        for (int row = 0; row < 5; row++) for (int col = 0; col < 3; col++) {
            if (!(glyph[row] & (4 >> col))) continue;
            float left = char_x + (float)col * px;
            float top = y0 - (float)row * py;
            const float quad[4][5] = {
                {left, top, 0, 0, 0},
                {left + px*.85f, top, 0, 1, 0},
                {left + px*.85f, top + py*.85f, 0, 1, 1},
                {left, top + py*.85f, 0, 0, 1}
            };
            memcpy(mesh->verts + vertex * 5u, quad, sizeof quad);
            uint16_t base = (uint16_t)vertex;
            const uint16_t tris[6] = {base, (uint16_t)(base+1),
                (uint16_t)(base+2), base, (uint16_t)(base+2),
                (uint16_t)(base+3)};
            memcpy(mesh->idx + index, tris, sizeof tris);
            vertex += 4u; index += 6u;
        }
    }
    return vertex == nverts && index == nidx;
}

static CarTexture *car_texture(CarViewer *v, uint32_t key) {
    for (int i = 0; i < v->texture_count; i++) if (v->textures[i].key == key) return v->textures+i;
    return NULL;
}

void car_viewer_destroy(CarViewer *v) {
    if (!v) return;
    render_use_program(0);
    free_scene_gpu(v->gpu, v->car.scene.count);
    free_scene_gpu(v->caption, 1);
    if (v->floor.vbo) glDeleteBuffers(1, &v->floor.vbo);
    if (v->floor.nbo) glDeleteBuffers(1, &v->floor.nbo);
    if (v->floor.cbo) glDeleteBuffers(1, &v->floor.cbo);
    if (v->floor.ibo) glDeleteBuffers(1, &v->floor.ibo);
    for (int i = 0; i < v->texture_count; i++) glDeleteTextures(1, &v->textures[i].id);
    if (v->program.prog) render_delete_program(v->program.prog);
    free(v->glass_order); car_scene_free(&v->car); free(v);
}

CarViewer *car_viewer_create(const char *root, char *error, size_t errorcap) {
    CarViewer *v = calloc(1, sizeof *v);
    N2Scene caption_scene = {0};
    size_t caption_gpu_bytes = 0;
    if (!v) { snprintf(error, errorcap, "viewer allocation failed"); return NULL; }
    if (!car_scene_load(root, &v->car, error, errorcap)) goto fail;
#if OPENUG2_VITA_CAR_SHADER
    v->program = render_program_static_car();
#else
    v->program = render_program();
#endif
    if (!v->program.prog) { snprintf(error, errorcap, "production renderer shader failed; collect GPU diagnostics"); goto fail; }
    for (int i = 0; i < v->car.scene.count; i++) {
        const N2Mesh *m = v->car.scene.meshes+i;
        v->stats.gpu_estimated_bytes += (size_t)m->nverts * (32u + (m->vcol ? 4u : 0u)) + (size_t)m->nidx * 2u;
    }
    const char *caption = "240SX STOCK   X COLOR   SQ LIGHTS   TRI VIEW   START PAUSE   SELECT START EXIT";
    if (!caption_scene_build(caption, &caption_scene, &caption_gpu_bytes)) {
        snprintf(error, errorcap, "caption mesh allocation/16-bit index limit failed"); goto fail;
    }
    size_t floor_bytes = 4u * (5u+3u) * sizeof(float) + 6u * sizeof(uint16_t);
    if (caption_gpu_bytes > SIZE_MAX - v->stats.gpu_estimated_bytes ||
        floor_bytes > SIZE_MAX - v->stats.gpu_estimated_bytes - caption_gpu_bytes) {
        snprintf(error, errorcap, "caption/floor GPU byte count overflows"); goto fail;
    }
    v->stats.gpu_estimated_bytes += caption_gpu_bytes + floor_bytes;
    if (v->stats.gpu_estimated_bytes > CAR_GPU_BUDGET) {
        snprintf(error, errorcap, "mesh and caption payload exceed 24 MiB viewer GPU budget"); goto fail;
    }
    v->gpu = upload_scene(&v->car.scene);
    if (!v->gpu || glGetError() != GL_NO_ERROR) {
        snprintf(error, errorcap, "GPU scene allocation/upload failed"); goto fail;
    }
    v->caption = upload_scene(&caption_scene);
    if (!v->caption || !v->caption[0].vbo || !v->caption[0].nbo || !v->caption[0].ibo ||
        glGetError() != GL_NO_ERROR) {
        snprintf(error, errorcap, "caption GPU upload failed"); goto fail;
    }
    n2_free_scene(&caption_scene);
    v->glass_order = malloc((size_t)v->car.scene.count * sizeof *v->glass_order);
    if (!v->glass_order) { snprintf(error, errorcap, "glass order allocation failed"); goto fail; }
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    g_tex_s3tc = extensions && strstr(extensions, "GL_EXT_texture_compression_s3tc");
    glActiveTexture(GL_TEXTURE0); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < v->car.texture_key_count; i++) {
        uint32_t key = v->car.texture_keys[i]; N2Tex texture = {0};
        if (!car_scene_decode(&v->car, key, &texture)) {
            snprintf(error, errorcap, "referenced stock texture %08x cannot be decoded", key); goto fail;
        }
        /* Budget the larger RGBA mip chain even if compressed upload is active.
           Driver metadata and shader allocations are measured separately. */
        size_t cost = 0;
        for (int w = texture.w, h = texture.h;; w = w > 1 ? w/2 : 1, h = h > 1 ? h/2 : 1) {
            cost += (size_t)w * (size_t)h * 4u;
            if (w == 1 && h == 1) break;
        }
        if (v->texture_count == CAR_MAX_TEXTURES ||
            v->stats.gpu_estimated_bytes > CAR_GPU_BUDGET ||
            cost > CAR_GPU_BUDGET - v->stats.gpu_estimated_bytes) {
            free(texture.rgb); free(texture.alpha); free(texture.dxt);
            snprintf(error, errorcap, "texture mip payload exceeds viewer GPU budget"); goto fail;
        }
        GLuint id = upload_tpk_texture_to_gpu(&texture);
        int decal = texture.alpha != NULL && texture.dxtfmt != 1, mode = n2_tex_mode(&texture);
        free(texture.rgb); free(texture.alpha); free(texture.dxt);
        if (!id || glGetError() != GL_NO_ERROR) {
            if (id) glDeleteTextures(1, &id);
            snprintf(error, errorcap, "GPU upload failed for texture %08x", key); goto fail;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        v->textures[v->texture_count++] = (CarTexture){id, key, decal, mode};
        v->stats.gpu_estimated_bytes += cost;
    }
    v->floor = make_quad();
    if (!v->floor.vbo || !v->floor.ibo || glGetError() != GL_NO_ERROR) {
        snprintf(error, errorcap, "viewer floor GPU allocation failed"); goto fail;
    }
    ResBudgetStats files = {0}; res_budget_snapshot(&v->car.budget, &files);
    v->stats.file_peak_bytes = files.peak; v->stats.cpu_mesh_bytes = v->car.cpu_mesh_bytes;
    v->stats.textures = (unsigned)v->texture_count; v->stats.meshes = (unsigned)v->car.scene.count;
    car_scene_release_files(&v->car);
    res_budget_snapshot(&v->car.budget, &files);
    if (files.used || files.live) { snprintf(error, errorcap, "file ownership remains after texture upload"); goto fail; }
    vita_log("CAR READY meshes=%u textures=%u wheels=4\n", v->stats.meshes, v->stats.textures);
    vita_log("CAR ASSET source_bytes=%u cpu_mesh_bytes=%lu file_peak_bytes=%lu file_budget_bytes=%u gpu_estimated_bytes=%lu gpu_budget_bytes=%u stock_wheel=%d stance=GLOBALB native_compression=%d\n",
        v->car.source_bytes, (unsigned long)v->stats.cpu_mesh_bytes, (unsigned long)v->stats.file_peak_bytes,
        files.limit, (unsigned long)v->stats.gpu_estimated_bytes, CAR_GPU_BUDGET, v->car.stock_wheel, g_tex_s3tc);
    return v;
fail:
    n2_free_scene(&caption_scene);
    car_viewer_destroy(v); return NULL;
}

static void car_material(CarViewer *v, int i, int paint, int lights, int glass) {
    const float paints[4][3] = {{.70f,.74f,.80f},{.8f,.08f,.04f},{.9f,.7f,.06f},{.06f,.25f,.85f}};
    const N2Mesh *m = v->car.scene.meshes+i; const RProg *r = &v->program;
    CarTexture *t = car_texture(v, m->texkey);
    int dark = n2_car_dark_trim(m->car_material), metal = n2_car_metal_trim(m->car_material);
    int body = m->cat == N2_CAR_BODY || m->cat == N2_CAR_MISC;
    int lamp = m->cat == N2_CAR_LIGHT || m->cat == N2_CAR_BRAKELIGHT;
    float spec = body ? .7f : lamp ? .45f : .04f;
    render_uniform1f(r->uUseTex, t ? 1 : 0); render_uniform1f(r->uUnlit, 0);
    render_uniform1f(r->uVColor, m->vcol ? 2 : 0);
    render_uniform1f(r->uSpec, m->trim ? spec*.4f : spec);
    render_uniform1f(r->uGloss, m->trim ? 6 : body ? 80 : 20);
    render_uniform1f(r->uClearcoat, body && !m->trim ? .65f : 0);
    render_uniform1f(r->uEnv, body ? .45f : lamp ? .15f : 0);
    render_uniform1f(r->uDecal, body && t && t->decal ? 1 : 0);
    render_uniform1f(r->uAlphaTest, t && t->mode == N2_DRAW_CUTOUT ? 1 : 0);
    render_uniform1f(r->uTextureAlpha, t && (t->mode == N2_DRAW_BLEND || t->mode == N2_DRAW_ADD) ? 1 : 0);
    render_uniform1f(r->uAlpha, glass ? (m->cat == N2_CAR_GLASS ? .36f : .18f) : 1);
    render_uniform1f(r->uFresnel, glass ? 1 : 0); render_uniform1f(r->uRimTint, 0);
    render_uniform3f(r->uEmissive, 0, 0, 0);
    if (dark) {
        render_uniform3f(r->uColor, .045f,.045f,.048f); render_uniform1f(r->uUseTex, t ? 1 : 0);
        render_uniform1f(r->uDecal, 0);
        render_uniform1f(r->uSpec, .08f); render_uniform1f(r->uEnv, 0); render_uniform1f(r->uClearcoat, 0);
    } else if (metal) {
        render_uniform3f(r->uColor, .55f,.57f,.60f); render_uniform1f(r->uUseTex, t ? 1 : 0);
        render_uniform1f(r->uDecal, 0);
        render_uniform1f(r->uSpec, .7f);
    } else if (glass) render_uniform3f(r->uColor, .30f,.34f,.39f);
    else if (body) render_uniform3fv(r->uColor, 1, paints[paint & 3]);
    else if (m->cat == N2_CAR_INTERIOR || m->cat == N2_CAR_MECH) render_uniform3f(r->uColor, .055f,.052f,.050f);
    else render_uniform3f(r->uColor, .12f,.12f,.13f);
    glBindTexture(GL_TEXTURE_2D, t ? t->id : 0);
    if (!glass && m->cat == N2_CAR_BRAKELIGHT && n2_car_tail_lens(&v->car.scene, i))
        render_tail_lamp(r, t ? t->id : 0, lights, 0, 0, 1);
    else if (!glass && lights && m->cat == N2_CAR_LIGHT && n2_headlight_emitter(&v->car.scene, i))
        render_uniform3f(r->uEmissive, 1.2f, 1.1f, .8f);
}

static void car_mesh_draw(CarViewer *v, int i, const float vp[16], const float model[16]) {
    float mvp[16]; mat_mul(vp, model, mvp);
    render_model(&v->program, model); render_uniform_matrix4fv(v->program.uMVP, 1, GL_FALSE, mvp);
    draw_gpumesh(v->gpu+i); v->stats.draws++;
}

int car_viewer_draw(CarViewer *v, float yaw, float pitch, float distance,
                    int paint, int lights, int paused, int width, int height) {
    if (!v || width <= 0 || height <= 0 || !isfinite(yaw) || !isfinite(pitch) || !isfinite(distance) || distance <= 0) return 0;
    RProg *r = &v->program; v->stats.draws = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glClearColor(paused ? .03f : .08f, paused ? .06f : .10f, paused ? .30f : .14f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); render_use_program(r->prog); glActiveTexture(GL_TEXTURE0);
    float target[3] = {0,0,v->car.profile.ride + .5f*(v->car.profile.body[4]+v->car.profile.body[5])};
    float eye[3] = {cosf(yaw)*cosf(pitch)*distance,sinf(yaw)*cosf(pitch)*distance,target[2]+sinf(pitch)*distance};
    float forward[3] = {target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]};
    float projection[16], view[16], vp[16];
    mat_persp(.9f, (float)width/height, .1f, 50.0f, projection); mat_lookat(eye, forward, view); mat_mul(projection, view, vp);
    render_uniform3fv(r->uCamPos, 1, eye); render_uniform1f(r->uFogDensity, 0);
    render_uniform1f(r->uAmbient, .55f); render_uniform1f(r->uDiffuse, .65f);
    render_uniform1f(r->uEnvReady, 0); render_uniform1f(r->uHeadGain, 0); render_uniform1f(r->uHeadShadow, 0);
    render_uniform1f(r->uWetness, 0); render_uniform1f(r->uRoadReflection, 0); render_uniform1f(r->uPaintLights, 0);
    render_uniform1f(r->uVista, 0); render_uniform1f(r->uSoft, 0); render_uniform1f(r->uAlpha, 1);
    render_uniform1f(r->uFresnel, 0); render_uniform1f(r->uTextureAlpha, 0); render_uniform1f(r->uAlphaTest, 0);
    render_uniform1f(r->uEmissiveTex, 0); render_uniform1f(r->uUnlit, 0); render_uniform1f(r->uUseTex, 0);
    render_uniform1f(r->uVColor, 0); render_uniform1f(r->uSpec, 0); render_uniform1f(r->uEnv, 0);
    render_uniform1f(r->uClearcoat, 0); render_uniform1f(r->uDecal, 0); render_uniform3f(r->uEmissive, 0,0,0);
    render_uniform3f(r->uColor, .18f,.20f,.22f);
    float ground[16] = {10,0,0,0, 0,10,0,0, 0,0,1,0, -5,-5,-.02f,1}, mvp[16];
    mat_mul(vp, ground, mvp); render_model(r, ground); render_uniform_matrix4fv(r->uMVP, 1, GL_FALSE, mvp);
    draw_gpumesh(&v->floor); v->stats.draws++;
    float pos[3] = {0}, up[3] = {0,0,1}, body[16], wheels[4][16];
    mat_car(pos, 0, up, v->car.profile.ride, body);
    for (int k = 0; k < 4; k++) {
        float side = (k & 1) ? -1 : 1;
        float local[16] = {1,0,0,0, 0,side,0,0, 0,0,1,0,
            k < 2 ? v->car.wheel.front_axle : v->car.wheel.rear_axle,
            .5f*side*(k < 2 ? v->car.wheel.front_track : v->car.wheel.rear_track),v->car.profile.hub_z,1};
        mat_mul(body, local, wheels[k]);
    }
    for (int i = 0; i < v->car.scene.count; i++) {
        const N2Mesh *m = v->car.scene.meshes+i;
        if (m->car_mount == N2_MOUNT_WHEEL || m->cat == N2_CAR_GLASS || n2_lamp_clear_cover(m->cat,m->car_material)) continue;
        car_material(v, i, paint, lights, 0);
        if (m->car_mount == N2_MOUNT_FRONT_BRAKE || m->car_mount == N2_MOUNT_REAR_BRAKE) {
            int first = m->car_mount == N2_MOUNT_FRONT_BRAKE ? 0 : 2;
            car_mesh_draw(v, i, vp, wheels[first]); car_mesh_draw(v, i, vp, wheels[first+1]);
        } else car_mesh_draw(v, i, vp, body);
    }
    for (int i = 0; i < v->car.scene.count; i++) {
        const N2Mesh *m = v->car.scene.meshes+i;
        if (m->car_mount != N2_MOUNT_WHEEL || m->tierid != v->car.scene.meshes[v->car.stock_wheel].tierid) continue;
        CarTexture *t = car_texture(v, m->texkey); car_material(v, i, paint, lights, 0);
        render_uniform1f(r->uDecal, 0); render_uniform1f(r->uEnv, 0);
        if (m->cat == N2_CAR_TIRE || m->cat == N2_CAR_INTERIOR) render_uniform3f(r->uColor, .045f,.045f,.05f);
        else { render_uniform3f(r->uColor, .85f,.88f,.92f); render_uniform1f(r->uRimTint, 1); }
        for (int k = 0; k < 4; k++) {
            float mw[16]; mat_mul(vp, wheels[k], mw); render_model(r, wheels[k]); render_uniform_matrix4fv(r->uMVP, 1, GL_FALSE, mw);
            render_wheel_mesh(r, v->gpu+i, t ? t->id : 0, t ? t->mode : N2_DRAW_OPAQUE); v->stats.draws++;
        }
    }
    mat_mul(vp, body, mvp);
    int glass_count = render_car_glass_order(&v->car.scene, mvp, v->glass_order);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    for (int j = 0; j < glass_count; j++) {
        int i = v->glass_order[j]; car_material(v, i, paint, lights, 1); car_mesh_draw(v, i, vp, body);
    }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    render_uniform1f(r->uUseTex, 0); render_uniform1f(r->uVColor, 0); render_uniform1f(r->uSpec, 0);
    render_uniform1f(r->uEnv, 0); render_uniform1f(r->uDecal, 0); render_uniform1f(r->uFresnel, 0);
    render_uniform1f(r->uClearcoat, 0); render_uniform1f(r->uUnlit, 1); render_uniform1f(r->uAlpha, 1);
    render_uniform1f(r->uTextureAlpha, 0); render_uniform1f(r->uAlphaTest, 0); render_uniform3f(r->uEmissive, 0,0,0);
    render_uniform3f(r->uColor, 1,1,1); render_model(r, NULL);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    render_uniform_matrix4fv(r->uMVP, 1, GL_FALSE, identity);
    draw_gpumesh(v->caption); v->stats.draws++;
    return glGetError() == GL_NO_ERROR;
}

void car_viewer_stats(const CarViewer *v, CarViewerStats *out) {
    if (v && out) *out = v->stats;
}
