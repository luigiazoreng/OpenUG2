# Renderer boundary proposal

This is the design slice for issue #14, based on source revision
`3f7536eba3ecfe6640e0115e57b0a48c49db53aa`. It proposes a C boundary around
existing GPU work. It does not implement that boundary, change draw order, move
world/gameplay ownership, add a render graph, or select a future Vulkan/DX12
backend. OpenGL remains the reference until equivalent fixed captures and
performance checks exist.

The source-level call inventory is in [RENDERER_CALL_INVENTORY.md](RENDERER_CALL_INVENTORY.md).
It reports 83 direct GL API names and 1,201 distinct API/file/function/line
locations, including conditional code and calls written in source macros.
Indirect timer-query entry points are in a separate section. The generated
[Vita API inventory](VITA_API_INVENTORY.md) is pinned to the same revision.
The engine inventory covers `src/**`, including optional frontend/debug code;
the isolated Vita bootstrap under `platform/vita/**` and host tools are
separate platform/build surfaces, not part of the engine renderer contract.

## Current ownership is distributed

`render.h` describes shared renderer utilities and types, not exclusive
ownership of GL. The actual call sites establish several owners:

- `src/render.c` builds the main shader program, uploads common scene meshes and
  TPK textures, and owns the helper lifetimes for headlight shadows, road
  reflections, environment probes, car shadows, rain and world effects.
- `src/main.c` owns the application frame loop and issues most scene draw and
  pipeline calls directly. It also retains and releases some GPU objects
  returned by renderer helpers, including car and traffic resources.
- `src/world.c` owns the world texture cache that retains uploaded textures;
  `src/world_resident.c` owns resident GPU allocations and retires their buffers
  and textures. World CPU scenes and residency policy stay in these modules.
- `src/world_mesh.c` separately owns debug-map buffers and a debug shader
  program. `src/hud.c` owns HUD art lifetime and submits HUD draws. The optional
  frontend in `src/frontend/frontend_draw.c` owns its program/buffers. The
  optional ImGui GL backend lives in `src/debugui.cpp`.

Creation helpers do not always own the returned object. For example,
`upload_tpk_texture_to_gpu` creates a GL texture, while the world cache or
resident-resource owner decides when to release it. `GpuMesh` values can be
created in `render.c` and later released by application or resident teardown.
The inventory lists every `glGen*`/`glCreate*` and `glDelete*` call owner; the
object table also records that timer queries are created through a dynamically
loaded pointer and have no explicit `glDeleteQueries` call in this revision.
Context destruction reclaims those context objects at shutdown.

This boundary must own backend objects without absorbing their callers' CPU
data or policies. World loading, TPK decode/cache policy, scene residency,
visibility/culling, car selection, draw ordering, HUD layout, and event/input
state stay with their current owners. Callers own CPU input buffers and the
opaque handles they retain; the renderer owns the backend allocations those
handles identify. Destruction invalidates that handle, and an invalid or stale
handle returns an error rather than being reinterpreted as a backend object.

## Small C surface for review

The following is interface shape, not a header to compile or an implementation
commit. Keep typed handles distinct so a texture cannot accidentally be used
as a buffer. A zero slot is invalid; a generation distinguishes a newly
allocated object from a stale handle after slot reuse. Format, primitive,
comparison, blend, and vertex scalar values are renderer enums, never copied GL
tokens. The `renderer_create` signature intentionally leaves the platform
surface provider out until the host/backend creation order is agreed; the
eventual host seam must use an opaque handle or callbacks, not SDL types.

```c
typedef struct Renderer Renderer;
typedef struct { uint32_t slot, generation; } RBuffer;
typedef struct { uint32_t slot, generation; } RTexture;
typedef struct { uint32_t slot, generation; } RProgram;
typedef struct { uint32_t slot, generation; } RTarget;

typedef enum {
    R_BACKEND_AUTO, R_BACKEND_OPENGL, R_BACKEND_GLES2,
    R_BACKEND_VULKAN, R_BACKEND_DX12
} RBackend;
typedef enum { R_OK, R_INVALID, R_UNSUPPORTED, R_OUT_OF_MEMORY, R_DEVICE_LOST } RResult;

typedef struct {
    uint32_t version;            /* version 1 */
    RBackend backend;
    uint32_t width, height;
    uint32_t frame_cap;
    uint32_t quality;            /* low / medium / high */
    float draw_distance;
    uint32_t diagnostics;
    uint8_t fullscreen;
} RendererConfig;

typedef struct {
    uint32_t max_texture_size, max_fragment_texture_units, max_combined_texture_units;
    uint32_t max_vertex_attributes;
    uint32_t max_color_attachments, max_stencil_bits;
    uint32_t flags;              /* readback, timer query, anisotropy, stencil, etc. */
} RendererCaps;

typedef enum { R_BUFFER_STATIC, R_BUFFER_DYNAMIC, R_BUFFER_STREAM } RBufferUsage;
typedef enum { R_FORMAT_R8, R_FORMAT_RGB8, R_FORMAT_RGBA8,
               R_FORMAT_DEPTH, R_FORMAT_DEPTH_STENCIL } RFormat;
typedef enum { R_CAP_READBACK = 1u, R_CAP_TIMER_QUERY = 2u,
               R_CAP_ANISOTROPY = 4u, R_CAP_STENCIL = 8u } RendererCapFlag;
typedef struct { size_t size; RBufferUsage usage; const void *initial_data; } RBufferDesc;
typedef enum { R_TEXTURE_2D, R_TEXTURE_CUBE } RTextureKind;
typedef enum { R_FACE_2D, R_FACE_POS_X, R_FACE_NEG_X, R_FACE_POS_Y,
               R_FACE_NEG_Y, R_FACE_POS_Z, R_FACE_NEG_Z } RTextureFace;
typedef struct {
    RTextureKind kind;
    uint32_t width, height, mip_levels, flags;
    RFormat format;
    const void *pixels[6]; size_t row_bytes;
} RTextureDesc;
typedef struct {
    const char *glsl_120_vertex, *glsl_120_fragment;
    const char *glsl_es_100_vertex, *glsl_es_100_fragment;
    /* Future backends may add offline-compiled variants without a runtime
       shader compiler becoming an engine dependency. */
} RProgramDesc;

typedef enum {
    R_PARAM_MODEL, R_PARAM_VIEW_PROJECTION, R_PARAM_BASE_COLOR,
    R_PARAM_ALPHA, R_PARAM_EMISSIVE, R_PARAM_LIGHTING, R_PARAM_FOG,
    R_PARAM_ENVIRONMENT, R_PARAM_CUTOUT, R_PARAM_WET_SURFACE,
    R_PARAM_DIAGNOSTIC
} RParam;
typedef enum { R_VALUE_FLOAT, R_VALUE_INT, R_VALUE_VEC2, R_VALUE_VEC3,
               R_VALUE_VEC4, R_VALUE_MAT4 } RValueType;
typedef struct { RParam semantic; RValueType type; uint32_t count; const void *data; } RParamValue;
typedef struct {
    RProgram program;
    RTexture textures[6];
    uint32_t flags;
    const RParamValue *values; uint32_t value_count;
} RMaterial;
typedef enum { R_PRIMITIVE_TRIANGLES, R_PRIMITIVE_LINES, R_PRIMITIVE_POINTS } RPrimitive;
typedef enum { R_ATTR_POSITION, R_ATTR_UV, R_ATTR_NORMAL, R_ATTR_COLOR } RVertexSemantic;
typedef enum { R_SCALAR_F32, R_SCALAR_U8, R_SCALAR_U16 } RScalarType;
typedef struct {
    RBuffer buffer;
    RVertexSemantic semantic;
    RScalarType scalar;
    uint32_t components, stride_bytes, offset_bytes;
    uint8_t normalized;
} RVertexBinding;
typedef enum { R_COMPARE_NEVER, R_COMPARE_LESS, R_COMPARE_EQUAL, R_COMPARE_LEQUAL,
               R_COMPARE_GREATER, R_COMPARE_NOTEQUAL, R_COMPARE_GEQUAL, R_COMPARE_ALWAYS } RCompare;
typedef enum { R_STENCIL_KEEP, R_STENCIL_ZERO, R_STENCIL_REPLACE,
               R_STENCIL_INC, R_STENCIL_DEC, R_STENCIL_INVERT } RStencilOp;
typedef enum { R_CULL_NONE, R_CULL_FRONT, R_CULL_BACK, R_CULL_FRONT_AND_BACK } RCullMode;
typedef enum { R_FRONT_CCW, R_FRONT_CW } RFrontFace;
typedef enum { R_BLEND_OPAQUE, R_BLEND_CUTOUT, R_BLEND_ALPHA, R_BLEND_ADDITIVE,
               R_BLEND_CUSTOM } RBlendMode;
typedef enum { R_BLEND_ZERO, R_BLEND_ONE, R_BLEND_SRC_ALPHA,
               R_BLEND_ONE_MINUS_SRC_ALPHA } RBlendFactor;
typedef struct {
    uint8_t color_write_mask;    /* bit 0..3 = R/G/B/A */
    uint8_t depth_test, depth_write, stencil_test, scissor_test;
    RCompare depth_compare;
    RCullMode cull;
    RFrontFace front_face;
    RBlendMode blend;
    RBlendFactor src_rgb, dst_rgb, src_alpha, dst_alpha;
    uint32_t stencil_reference, stencil_read_mask, stencil_write_mask;
    RCompare stencil_compare;
    RStencilOp stencil_fail, depth_fail, depth_pass;
    uint32_t scissor_x, scissor_y, scissor_width, scissor_height;
} RPipelineState;
typedef enum { R_INDEX_U16 } RIndexType;
typedef struct {
    RVertexBinding bindings[4]; uint32_t binding_count;
    RBuffer indices;
    RIndexType index_type;       /* first slice uses 16-bit indices */
    uint32_t indexed, first_index, index_count, first_vertex, vertex_count;
    RPrimitive primitive;
    RPipelineState pipeline;
    RMaterial material;
} RDrawPacket;
typedef struct {
    RTexture color;
    RTexture depth;              /* invalid selects backend depth storage */
    uint32_t width, height, depth_stencil_bits, flags;
} RTargetDesc;
typedef struct { uint32_t x, y, width, height; float clear_rgba[4]; uint32_t clear_flags; } RFrameDesc;
typedef enum { R_ORIGIN_TOP_LEFT, R_ORIGIN_BOTTOM_LEFT } RImageOrigin;
typedef struct {
    uint32_t width, height, row_bytes;
    RFormat format;
    RImageOrigin origin;
    void *pixels; size_t capacity;
} RReadback;

RResult renderer_create(const RendererConfig *, Renderer **out, char *error, size_t error_cap);
void renderer_destroy(Renderer **);
RResult renderer_get_caps(Renderer *, RendererCaps *out);
RResult renderer_buffer_create(Renderer *, const RBufferDesc *, RBuffer *out);
RResult renderer_buffer_update(Renderer *, RBuffer, size_t offset, size_t bytes, const void *data);
void renderer_buffer_destroy(Renderer *, RBuffer *);
RResult renderer_texture_create(Renderer *, const RTextureDesc *, RTexture *out);
RResult renderer_texture_update(Renderer *, RTexture, RTextureFace, uint32_t level,
                                const RTextureDesc *);
void renderer_texture_destroy(Renderer *, RTexture *);
RResult renderer_program_create(Renderer *, const RProgramDesc *, RProgram *out,
                                char *error, size_t error_cap);
void renderer_program_destroy(Renderer *, RProgram *);
RResult renderer_target_create(Renderer *, const RTargetDesc *, RTarget *out);
void renderer_target_destroy(Renderer *, RTarget *);
RResult renderer_begin_frame(Renderer *, const RFrameDesc *);
RResult renderer_set_target(Renderer *, RTarget, RTextureFace face); /* invalid selects swapchain */
RResult renderer_draw(Renderer *, const RDrawPacket *);
RResult renderer_readback(Renderer *, RReadback *); /* optional, explicit capability */
RResult renderer_end_frame(Renderer *);              /* submits/presents once */
```

This preserves an immediate, caller-driven draw model. `RMaterial` is a
per-draw value, not a scene owner or mandatory GPU allocation. The six current
sampler slots are explicit: 0 is `uTex`; 1 and 2 are the headlight shadow maps;
3 and 4 are reflection color/depth; 5 is the environment cubemap. Textures at
slot 5 use `R_TEXTURE_CUBE`. These slots match `src/render.c` assignments.
`RVertexBinding` can express the current separate
position/UV, normal, color, and index streams without repacking `GpuMesh`.
`RPipelineState` names the current color mask, depth compare/write, cull/front
face, separate RGB/alpha blend factors, stencil reference/masks/compare/ops,
and scissor rectangle. Stencil requests require a stencil-capable target; if
that attachment is absent the draw returns `R_UNSUPPORTED` or follows an
explicit documented fallback for that effect.

For `RTextureDesc`, a 2D upload uses `R_FACE_2D`; a cubemap uses one face entry
per `RTextureFace`. All six current fragment samplers are validated against
`max_fragment_texture_units`, rather than assuming that a context accepted the
program just because it was created.

Materials borrow their six texture and program handles for the duration of
`renderer_draw`; targets borrow attachment textures, while a target owns any
private depth/stencil allocation the backend creates. Destroying a target
releases only its target/depth storage. Upload and parameter pointers are
consumed before the API call returns and are never retained.

Material semantic parameters replace backend uniform locations;
implementation must map every currently used `RProg` field and specialized
pass before routing any existing draw. Unknown semantics fail that draw with
a diagnostic. `RProgramDesc` allows OpenGL GLSL 1.20 and GLES GLSL ES 1.00
now; later offline shader variants can be added to the descriptor. No runtime
shader toolchain is required by the common engine.

`renderer_readback` is diagnostic/capture work, not a required per-frame step.
It reports `R_UNSUPPORTED` when unavailable and never fabricates zero pixels.
Its format, row pitch, and requested origin (`RReadback.origin`) are explicit;
the migration must preserve the current screenshot bytes and orientation.
Query/timer data is optional and
must report unavailable separately from a measured zero.

## Lifetime, thread, and platform boundary

One renderer/device has one owning render thread. Resource create/update,
draw, readback, and destruction run there; callers may parse assets or prepare
CPU arrays elsewhere but may not issue GL calls from worker threads. The
backend owns its device/context and frame surface. The application owns the
renderer object and calls `renderer_destroy` while the platform surface is
still valid; after that, the platform layer destroys its window/surface. No
SDL, GL, Vulkan, or DX12 type crosses this API.

The current main loop creates the SDL window/context itself. The boundary
proposal assigns native window/surface ownership to a platform adapter and
API context/device/swapchain ownership to `Renderer`; the adapter may pass an
opaque surface handle, never an SDL or GL type. The exact callback/creation
order remains an open integration decision, especially because Vulkan surface
creation depends on the instance. Regardless of that choice, all backend work
and present stay on the owning thread, with one frame open at a time. Begin/end
failure closes or abandons that frame predictably; device loss returns an
actionable result and does not expose stale handles.

Resource handles are valid only for their creating `Renderer`. Existing owner
modules release their handles before `renderer_destroy`; destruction can drain
remaining backend allocations as a final safety net and report leaked handles
in diagnostics. CPU geometry, textures, materials, and world resources retain
their existing owner and lifetime. No renderer object keeps a pointer into
short-lived caller upload memory after a create/update call returns.

## GLES 2.0 floor

GLES 2.0 plus GLSL ES 1.00 is a supported production floor, not a reduced
afterthought. The [Khronos GLES registry](https://registry.khronos.org/OpenGL/index_es.php)
publishes the [GLES 2.0 API specification](https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf)
and [GLSL ES 1.00 specification](https://registry.khronos.org/OpenGL/specs/es/2.0/GLSL_ES_Specification_1.00.pdf).
The backend must query limits and extensions at device creation; never infer
them from the requested resolution or GPU name. The current shader samples
units 0 through 5, so program creation validates six usable fragment texture
units. Query timer/readback/stencil/anisotropy support and advertise it in the
caps flags; stencil tests require a stencil attachment on the active target.

The common contract must remain within GLES 2.0 core: GLSL ES 1.00,
vertex-attribute arrays, 16-bit indices, and ordinary 2D textures. Do not
require VAOs, uniform buffers, compute/geometry/tessellation shaders,
instancing, polygon mode, anisotropic filtering, timer queries, or readback
extensions. Request those only behind capability bits and retain a core-path
fallback. NPOT textures use only the restrictions guaranteed by core; mipmaps
or repeat wrapping for NPOT data require the relevant extension or a padded
power-of-two fallback. The current engine's `glPolygonMode` sites are
diagnostics and remain desktop-only.

Report queried limits such as max texture dimensions, texture units, vertex
attributes, and color attachments through `RendererCaps`. Clamp optional
quality features to those caps. If a required resource cannot be represented,
return a reasoned error or choose a documented lower-quality path; never skip
world geometry silently. Environment reflections, headlight shadows, timer
queries, rain extras, and upscalers remain optional when their inputs or
capabilities are unavailable.

## Configuration and safe fallback

Persist a versioned configuration with only the requested product controls:
backend, width/height, fullscreen, frame cap, quality preset, draw distance,
and diagnostics. Parse and validate it once before creating the renderer.
Reject unknown schema versions and invalid dimensions/caps with a concise
warning and known-safe defaults (the platform's implemented GL reference
backend, desktop GL or GLES2, windowed mode, conservative resolution/quality/
draw distance). A recognized but unimplemented Vulkan/DX12 backend also reports
the requested/active backend and falls back to the platform's GL reference; it
must not leave a blank frame or partially initialized device.
Capability-driven optional features may turn off individually, with one
actionable diagnostic. The native renderer remains complete without FSR/DLSS;
FSR evaluation comes first, DLSS is optional and vendor-gated.

## Migration and acceptance gates

1. Freeze the semantic parameter mapping against the current `RProg`, per-pass
   state, and all call owners in the inventory. Keep scene/world data in its
   current subsystem.
2. Put the existing GL entry points behind an OpenGL implementation of this
   contract without changing ordering, formats, clear/readback behavior,
   filtering, blend/depth state, or frame timing.
3. Fixed L4RA/L4RB reference captures, instance/resident counts, and PNG bytes
   have passed against the preserved upstream reference on the original desktop
   driver; normal, debug, and GLES builds also passed with zero warnings for
   source revision 3f7536e. Details are in [RENDERER_VALIDATION.md](RENDERER_VALIDATION.md).
   This verifies the current direct-GL runtime, not a backend adapter, which is
   not implemented in this design slice.
4. Re-run release, debug, and GLES builds warning-free after backend
   implementation and repeat the fixed captures for that implementation.
5. Only after the OpenGL boundary and conformance captures stabilize, add
   Vulkan and then DX12. Add optional image scaling after native fallback is
   already verified.

Open decisions for the next design review: the platform-surface callback and
creation order; the full semantic-uniform enumeration (including diagnostics);
and the exact fallback resolution/frame-cap defaults. These decisions do not
block the inventory review and are not silently fixed by this proposal.
