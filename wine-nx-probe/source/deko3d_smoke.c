#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <deko3d.h>

/* Crash diagnostics, added after an instant black-screen-then-fatal-error
 * on the first hardware run of stage 3 with no evidence of where; kept in
 * place afterward rather than ripped out, since it's cheap and found two
 * real bugs (see the bottom of the file header comment below) in exactly
 * two hardware round-trips instead of guesswork. Opened eagerly and
 * flushed after every write (not just at exit) so whatever ran before a
 * hard crash is still on the SD card afterward -- a clean fclose() at the
 * end can't be relied on. deko3d's own DkDeviceMaker.cbDebug hook (unset
 * before this) reports validation errors the driver catches; checkpoint
 * logmsg() calls through graphicsInitialize() narrow down anything the
 * driver doesn't catch (a hard fault) to the last completed step. */
static FILE *g_log;

static void log_open(void)
{
    g_log = fopen( "sdmc:/switch/deko3d_smoke.log", "w" );
}

static void logmsg( const char *fmt, ... )
{
    va_list args;
    if (!g_log) return;
    va_start( args, fmt );
    vfprintf( g_log, fmt, args );
    va_end( args );
    fputc( '\n', g_log );
    fflush( g_log );
}

static void debug_callback( void *userData, const char *context, DkResult result, const char *message )
{
    (void)userData;
    logmsg( "[DK DEBUG] context=%s result=%d message=%s",
           context ? context : "(null)", (int)result, message ? message : "(null)" );
}

/* Deko3d bring-up smoke test -- GPU-compositor milestone (see
 * wine-nx-probe/3d-accel-scoping.md), built in stages, each hardware-
 * confirmed before the next was started. All three stages now confirmed
 * running together, holding steady 60-62fps on real hardware for a
 * 5-10 second test run (dark background, cyan box, gradient texture,
 * spinning shaded cube, and the live FPS counter all rendering correctly
 * at once) -- real shader/geometry work does not measurably cost FPS
 * over the clear-only stages 1-2 baseline.
 *
 * Stage 1 (HARDWARE-CONFIRMED, 60-62fps vs. the software GDI path's
 * measured 2fps): device/queue/swapchain/present against the same
 * nwindowGetDefault() handle wine_nx_fb_init() uses, dark background +
 * cyan box via scissor+clear, no shaders. FPS counter drawn the same way
 * (scissor rects per "on" pixel of a 3x5 bitmap font), timed with
 * armGetSystemTick()/armTicksToNs() -- not GetTickCount64, confirmed
 * broken on this Switch port (see README, "GetTickCount/GetTickCount64
 * Are Frozen"). Digits are rebuilt once a second (dkQueueWaitIdle() +
 * dkCmdBufClear() + re-record); that stall is once a second, not once a
 * frame, so it doesn't meaningfully cap the number being measured.
 *
 * Stage 2 (HARDWARE-CONFIRMED): a CPU-generated RGBA8 gradient copied
 * straight onto a sub-rect of the framebuffer image via
 * dkCmdBufCopyBufferToImage() -- deliberately not dkCmdBufBlitImage()/
 * dkCmdBufCopyImage() (image-to-image), which no example anywhere in the
 * devkitPro toolchain exercises, so their usage-flag requirements aren't
 * confirmed by any working reference. CopyBufferToImage is proven
 * end-to-end in deko_console/source/gpu_console.c's font-tileset upload.
 *
 * Stage 3 (HARDWARE-CONFIRMED): a rotating cube, adapted from
 * deko_examples/source/Example03_Cube.cpp -- the devkitPro example that
 * demonstrates a transformed 3D object with the simplest vertex/fragment
 * shader pair (no textures/samplers/descriptor sets, unlike Example04).
 * Vertex data, attribute layout, and both shaders
 * (source/shaders/{transform_vsh,color_fsh}.glsl) are copied verbatim
 * from that example; only the C++/GLM/SampleFramework scaffolding is
 * reimplemented in plain C:
 *   - Matrix math (identity/multiply/translate/rotate/perspective) is
 *     standard textbook column-major RH_ZO formulas, matching what
 *     Example03's glm::perspectiveRH_ZO/rotate/translate/scale calls
 *     produce -- not deko3d-specific, low risk to hand-roll.
 *   - The per-frame dynamic uniform update (the rotation matrix changes
 *     every frame, unlike the FPS text's once-a-second rebuild) is
 *     modeled directly on SampleFramework/CCmdMemRing.h's technique:
 *     a small ring of command-memory slices, each protected by its own
 *     DkFence, so the CPU can start recording next frame's push-constant
 *     command without a full dkQueueWaitIdle() stalling the entire
 *     pipeline (which would understate the real achievable FPS). The
 *     fence API itself (dkFenceWait/dkCmdBufSignalFence, DkFence as a
 *     zero-initializable opaque struct) mirrors gpu_console.c's own
 *     plain-C usage of the same primitives for a different resource.
 *   - Shader loading (loadShader()) is copied from deko_basic's plain-C
 *     version verbatim.
 * The persistent uniform buffer itself (as opposed to the command memory
 * that pushes into it) does not need ring protection: commands execute
 * in submission order on a single queue, so a frame's own push-constants
 * command always completes before that same frame's draw reads it, and
 * before the next frame's push-constants command is even submitted.
 *
 * FPS counter stays running through this stage so a real fps drop under
 * actual shader/geometry work (vs. the clear-only stages 1-2) would be
 * visible, not just assumed. Result: no drop -- still 60-62fps.
 *
 * Two real bugs surfaced getting here, both found from hardware crash logs
 * (see log_open()/logmsg() below), not guessed at:
 *   - main() never called romfsInit(), so loadShader()'s
 *     fopen("romfs:/shaders/...") returned NULL and the unchecked
 *     fseek(NULL, ...) right after it crashed immediately. deko_basic's
 *     reference main() calls romfsInit() first; missed copying that over
 *     along with loadShader() itself.
 *   - The vertex buffer, uniform buffer, and dynamic command ring's
 *     DkMemBlockCreate() sizes weren't rounded up to DK_MEMBLOCK_ALIGNMENT
 *     (4096) the way gpu_console.c rounds even its small CPU-data buffers
 *     -- crashed hard enough that even the cbDebug callback never fired. */

#define FB_NUM 2
#define FB_WIDTH  1280
#define FB_HEIGHT 720
#define CMDMEMSIZE (32 * 1024)
#define TEXTCMDMEMSIZE (16 * 1024)
#define HUD_SCALE 8   /* each 3x5 glyph cell -> 8x8 screen px */
#define TEX_W 256
#define TEX_H 144
#define TEX_X 60
#define TEX_Y 520

#define CODEMEMSIZE (64 * 1024)
#define DYN_RING_SLOTS FB_NUM
#define DYN_SLICE_SIZE 512

static DkDevice g_device;
static DkMemBlock g_framebufferMemBlock;
static DkImage g_framebuffers[FB_NUM];
static DkSwapchain g_swapchain;

static DkMemBlock g_cmdbufMemBlock;
static DkCmdBuf g_cmdbuf;
static DkCmdList g_cmdsBindFramebuffer[FB_NUM];
static DkCmdList g_cmdsRender;

static DkMemBlock g_textCmdbufMemBlock;
static DkCmdBuf g_textCmdbuf;
static DkCmdList g_cmdsText;

static DkMemBlock g_texStagingMemBlock;
static DkCmdList g_cmdsTexUpload[FB_NUM];

static DkQueue g_renderQueue;

static unsigned int g_fps_count;
static unsigned int g_fps_display;
static uint64_t g_fps_epoch_ns;

/* -------------------------------------------------------------------- */
/* Cube: state, matrix math, shaders, dynamic uniform ring              */
/* -------------------------------------------------------------------- */

typedef struct { float m[16]; } Mat4;   /* column-major, matches GLSL mat4 */

typedef struct
{
    Mat4 mdlvMtx;
    Mat4 projMtx;
} Transformation;

typedef struct { float position[3]; float color[3]; } CubeVertex;

/* Vertex data copied verbatim (component values) from Example03_Cube.cpp's
 * CubeVertexData -- 6 faces x 4 verts, drawn as DkPrimitive_Quads. */
static const CubeVertex g_cubeVertexData[24] = {
    /* +X face */
    { { +1.0f, +1.0f, +1.0f }, { 1.0f, 0.0f, 0.0f } },
    { { +1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { +1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
    { { +1.0f, +1.0f, -1.0f }, { 1.0f, 1.0f, 0.0f } },
    /* -X face */
    { { -1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
    { { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { -1.0f, -1.0f, +1.0f }, { 0.0f, 0.0f, 1.0f } },
    { { -1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f, 0.0f } },
    /* +Y face */
    { { -1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
    { { -1.0f, +1.0f, +1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { +1.0f, +1.0f, +1.0f }, { 0.0f, 0.0f, 1.0f } },
    { { +1.0f, +1.0f, -1.0f }, { 1.0f, 1.0f, 0.0f } },
    /* -Y face */
    { { -1.0f, -1.0f, +1.0f }, { 1.0f, 0.0f, 0.0f } },
    { { -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { +1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
    { { +1.0f, -1.0f, +1.0f }, { 1.0f, 1.0f, 0.0f } },
    /* +Z face */
    { { -1.0f, +1.0f, +1.0f }, { 1.0f, 0.0f, 0.0f } },
    { { -1.0f, -1.0f, +1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { +1.0f, -1.0f, +1.0f }, { 0.0f, 0.0f, 1.0f } },
    { { +1.0f, +1.0f, +1.0f }, { 1.0f, 1.0f, 0.0f } },
    /* -Z face */
    { { +1.0f, +1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
    { { +1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
    { { -1.0f, +1.0f, -1.0f }, { 1.0f, 1.0f, 0.0f } },
};

static const DkVtxAttribState g_cubeAttribState[] = {
    { .bufferId = 0, .isFixed = 0, .offset = offsetof(CubeVertex, position),
      .size = DkVtxAttribSize_3x32, .type = DkVtxAttribType_Float, .isBgra = 0 },
    { .bufferId = 0, .isFixed = 0, .offset = offsetof(CubeVertex, color),
      .size = DkVtxAttribSize_3x32, .type = DkVtxAttribType_Float, .isBgra = 0 },
};

static const DkVtxBufferState g_cubeBufferState[] = {
    { .stride = sizeof(CubeVertex), .divisor = 0 },
};

static void mat4_identity( Mat4 *m )
{
    memset( m->m, 0, sizeof(m->m) );
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.0f;
}

static Mat4 mat4_multiply( const Mat4 *a, const Mat4 *b )
{
    Mat4 r;
    int col, row, k;
    for (col = 0; col < 4; col++)
        for (row = 0; row < 4; row++)
        {
            float sum = 0.0f;
            for (k = 0; k < 4; k++) sum += a->m[k * 4 + row] * b->m[col * 4 + k];
            r.m[col * 4 + row] = sum;
        }
    return r;
}

static Mat4 mat4_translate( float x, float y, float z )
{
    Mat4 m; mat4_identity( &m );
    m.m[12] = x; m.m[13] = y; m.m[14] = z;
    return m;
}

static Mat4 mat4_rotate_x( float angle )
{
    Mat4 m; float c = cosf( angle ), s = sinf( angle );
    mat4_identity( &m );
    m.m[5] = c; m.m[6] = s;
    m.m[9] = -s; m.m[10] = c;
    return m;
}

static Mat4 mat4_rotate_y( float angle )
{
    Mat4 m; float c = cosf( angle ), s = sinf( angle );
    mat4_identity( &m );
    m.m[0] = c; m.m[2] = -s;
    m.m[8] = s; m.m[10] = c;
    return m;
}

static Mat4 mat4_scale( float s )
{
    Mat4 m; mat4_identity( &m );
    m.m[0] = m.m[5] = m.m[10] = s;
    return m;
}

/* Standard right-handed perspective projection, [0,1] depth range --
 * matches glm::perspectiveRH_ZO(), what Example03 uses (deko3d/NVN expect
 * [0,1] depth, not OpenGL's traditional [-1,1]). */
static Mat4 mat4_perspective( float fovyRadians, float aspect, float near, float far )
{
    Mat4 m;
    float f = 1.0f / tanf( fovyRadians / 2.0f );
    memset( m.m, 0, sizeof(m.m) );
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = far / (near - far);
    m.m[11] = -1.0f;
    m.m[14] = (far * near) / (near - far);
    return m;
}

static DkMemBlock g_codeMemBlock;
static uint32_t g_codeMemOffset;
static DkShader g_vertexShader;
static DkShader g_fragmentShader;

static DkMemBlock g_vtxMemBlock;
static DkMemBlock g_uniformMemBlock;
static DkMemBlock g_depthMemBlock;
static DkImage g_depthBuffer;

static DkMemBlock g_dynMemBlock;
static DkCmdBuf g_dynCmdbuf;
static DkFence g_dynFences[DYN_RING_SLOTS];
static unsigned g_dynSlice;

static DkCmdList g_cmdsCube;
static Transformation g_transform;

/* Adapted from deko_basic/source/main.c's loadShader() -- same body, plus
 * a checked fopen() instead of an unchecked one. The reference gets away
 * with not checking because its main() always calls romfsInit() first, so
 * fopen("romfs:/...") never fails there; logging instead of blindly
 * dereferencing a NULL FILE* here means a future path/mount problem shows
 * up as a log line instead of another silent crash. */
static void loadShader( DkShader *pShader, const char *path )
{
    FILE *f = fopen( path, "rb" );
    uint32_t size, codeOffset;

    if (!f)
    {
        logmsg( "[FATAL] loadShader: fopen(\"%s\") failed", path );
        return;
    }

    fseek( f, 0, SEEK_END );
    size = ftell( f );
    rewind( f );

    codeOffset = g_codeMemOffset;
    g_codeMemOffset += (size + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);

    fread( (uint8_t *)dkMemBlockGetCpuAddr( g_codeMemBlock ) + codeOffset, size, 1, f );
    fclose( f );

    {
        DkShaderMaker shaderMaker;
        dkShaderMakerDefaults( &shaderMaker, g_codeMemBlock, codeOffset );
        dkShaderInitialize( pShader, &shaderMaker );
    }
}

/* Record this frame's rotation matrix into a fresh slice of the dynamic
 * command ring, following SampleFramework/CCmdMemRing.h exactly: clear,
 * wait for that slice's fence (so we don't overwrite command memory the
 * GPU might still be reading), add the slice's memory, push the new
 * uniform data, signal the slice's fence, advance, finish the list. */
static DkCmdList record_dynamic_transform(void)
{
    DkCmdList list;
    uint32_t sliceOffset = g_dynSlice * DYN_SLICE_SIZE;

    dkCmdBufClear( g_dynCmdbuf );
    dkFenceWait( &g_dynFences[g_dynSlice], UINT64_MAX );
    dkCmdBufAddMemory( g_dynCmdbuf, g_dynMemBlock, sliceOffset, DYN_SLICE_SIZE );
    dkCmdBufPushConstants( g_dynCmdbuf, dkMemBlockGetGpuAddr( g_uniformMemBlock ), sizeof(Transformation),
                           0, sizeof(Transformation), &g_transform );
    dkCmdBufSignalFence( g_dynCmdbuf, &g_dynFences[g_dynSlice], false );
    g_dynSlice = (g_dynSlice + 1) % DYN_RING_SLOTS;

    list = dkCmdBufFinishList( g_dynCmdbuf );
    return list;
}

/* -------------------------------------------------------------------- */

/* 3x5 bitmap font, same design as wine_nx_hud_glyph() in runtime.c, trimmed
 * to just the characters "FPS:0123456789 " needs. */
static unsigned char hud_glyph( char c, int row )
{
    static const unsigned char rows[][5] = {
        /*0*/ {0x7,0x5,0x5,0x5,0x7}, /*1*/ {0x2,0x6,0x2,0x2,0x7},
        /*2*/ {0x7,0x1,0x7,0x4,0x7}, /*3*/ {0x7,0x1,0x7,0x1,0x7},
        /*4*/ {0x5,0x5,0x7,0x1,0x1}, /*5*/ {0x7,0x4,0x7,0x1,0x7},
        /*6*/ {0x7,0x4,0x7,0x5,0x7}, /*7*/ {0x7,0x1,0x1,0x1,0x1},
        /*8*/ {0x7,0x5,0x7,0x5,0x7}, /*9*/ {0x7,0x5,0x7,0x1,0x7},
        /*F*/ {0x7,0x4,0x7,0x4,0x4}, /*P*/ {0x7,0x5,0x7,0x4,0x4},
        /*S*/ {0x7,0x4,0x7,0x1,0x7}, /*:*/ {0x0,0x2,0x0,0x2,0x0},
        /* */ {0x0,0x0,0x0,0x0,0x0},
    };
    static const char chars[] = "0123456789FPS: ";
    const char *p = strchr( chars, c );
    if (!p || row < 0 || row > 4) return 0;
    return rows[p - chars][row];
}

/* Record one scissor+clear pair per "on" glyph pixel into cmdbuf. Same
 * technique as wine_nx_hud_draw_text() in runtime.c, just issued as GPU
 * clear commands instead of raw CPU pixel writes. */
static void record_text( DkCmdBuf cmdbuf, int x0, int y0, const char *text, float r, float g, float b )
{
    int x = x0;

    for (; *text; text++, x += 4 * HUD_SCALE)
    {
        int row, col;
        for (row = 0; row < 5; row++)
        {
            unsigned char bits = hud_glyph( *text, row );
            for (col = 0; col < 3; col++)
            {
                DkScissor s;
                if (!(bits & (1 << (2 - col)))) continue;
                s.x = x + col * HUD_SCALE;
                s.y = y0 + row * HUD_SCALE;
                s.width = HUD_SCALE;
                s.height = HUD_SCALE;
                dkCmdBufSetScissors( cmdbuf, 0, &s, 1 );
                dkCmdBufClearColorFloat( cmdbuf, 0, DkColorMask_RGBA, r, g, b, 1.0f );
            }
        }
    }
}

/* Fill a linear RGBA8 staging buffer with a simple two-axis gradient --
 * R varies left to right, G varies top to bottom, so every pixel is
 * genuinely distinct (not just a solid fill some other rect could produce),
 * proving actual per-pixel upload rather than another clear-color rect. */
static void fill_gradient( uint8_t *dst )
{
    unsigned x, y;

    for (y = 0; y < TEX_H; y++)
    {
        for (x = 0; x < TEX_W; x++)
        {
            uint8_t *px = dst + (y * TEX_W + x) * 4;
            px[0] = (uint8_t)(x * 255 / (TEX_W - 1));   /* R: left -> right */
            px[1] = (uint8_t)(y * 255 / (TEX_H - 1));   /* G: top -> bottom */
            px[2] = 160;                                 /* B: constant */
            px[3] = 255;                                 /* A: opaque */
        }
    }
}

/* Rebuild the "FPS:NNNN" command list from the current display value.
 * Caller must ensure the GPU is idle first (see dkQueueWaitIdle() at both
 * call sites) since this reuses -- not appends to -- the text cmdbuf's
 * memory. */
static void rebuild_fps_text(void)
{
    char line[16];

    snprintf( line, sizeof(line), "FPS:%u", g_fps_display );
    dkCmdBufClear( g_textCmdbuf );
    record_text( g_textCmdbuf, 24, 24, line, 0.83f, 0.92f, 0.37f );
    g_cmdsText = dkCmdBufFinishList( g_textCmdbuf );
}

static void graphicsInitialize(void)
{
    DkDeviceMaker deviceMaker;
    DkImageLayoutMaker imageLayoutMaker;
    DkImageLayout framebufferLayout;
    DkMemBlockMaker memBlockMaker;
    DkImage const *swapchainImages[FB_NUM];
    DkSwapchainMaker swapchainMaker;
    DkCmdBufMaker cmdbufMaker;
    DkQueueMaker queueMaker;
    DkScissor full, box;
    uint32_t framebufferSize, framebufferAlign;
    unsigned i;

    dkDeviceMakerDefaults( &deviceMaker );
    deviceMaker.cbDebug = debug_callback;   /* was unset (NULL) -- driver-caught errors were being silently dropped */
    g_device = dkDeviceCreate( &deviceMaker );
    logmsg( "[INIT] device created" );

    dkImageLayoutMakerDefaults( &imageLayoutMaker, g_device );
    imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    imageLayoutMaker.format = DkImageFormat_RGBA8_Unorm;
    imageLayoutMaker.dimensions[0] = FB_WIDTH;
    imageLayoutMaker.dimensions[1] = FB_HEIGHT;
    dkImageLayoutInitialize( &framebufferLayout, &imageLayoutMaker );

    framebufferSize  = dkImageLayoutGetSize( &framebufferLayout );
    framebufferAlign = dkImageLayoutGetAlignment( &framebufferLayout );
    framebufferSize = (framebufferSize + framebufferAlign - 1) & ~(framebufferAlign - 1);

    dkMemBlockMakerDefaults( &memBlockMaker, g_device, FB_NUM * framebufferSize );
    memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    g_framebufferMemBlock = dkMemBlockCreate( &memBlockMaker );

    for (i = 0; i < FB_NUM; i++)
    {
        swapchainImages[i] = &g_framebuffers[i];
        dkImageInitialize( &g_framebuffers[i], &framebufferLayout, g_framebufferMemBlock, i * framebufferSize );
    }

    dkSwapchainMakerDefaults( &swapchainMaker, g_device, nwindowGetDefault(), swapchainImages, FB_NUM );
    g_swapchain = dkSwapchainCreate( &swapchainMaker );
    logmsg( "[INIT] swapchain created" );

    /* Depth buffer for the cube -- Z24S8, matching Example03's
     * createFramebufferResources(). Bound alongside color on every
     * framebuffer-bind list below; harmless for the clear/copy commands
     * that don't touch depth state, needed for the cube's own draw. */
    {
        DkImageLayoutMaker depthLayoutMaker;
        DkImageLayout depthLayout;
        uint32_t depthSize, depthAlign;

        dkImageLayoutMakerDefaults( &depthLayoutMaker, g_device );
        depthLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
        depthLayoutMaker.format = DkImageFormat_Z24S8;
        depthLayoutMaker.dimensions[0] = FB_WIDTH;
        depthLayoutMaker.dimensions[1] = FB_HEIGHT;
        dkImageLayoutInitialize( &depthLayout, &depthLayoutMaker );

        depthSize  = dkImageLayoutGetSize( &depthLayout );
        depthAlign = dkImageLayoutGetAlignment( &depthLayout );
        depthSize = (depthSize + depthAlign - 1) & ~(depthAlign - 1);

        dkMemBlockMakerDefaults( &memBlockMaker, g_device, depthSize );
        memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
        g_depthMemBlock = dkMemBlockCreate( &memBlockMaker );

        dkImageInitialize( &g_depthBuffer, &depthLayout, g_depthMemBlock, 0 );
    }
    logmsg( "[INIT] depth buffer created" );

    dkMemBlockMakerDefaults( &memBlockMaker, g_device, CMDMEMSIZE );
    memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    g_cmdbufMemBlock = dkMemBlockCreate( &memBlockMaker );

    dkCmdBufMakerDefaults( &cmdbufMaker, g_device );
    g_cmdbuf = dkCmdBufCreate( &cmdbufMaker );
    dkCmdBufAddMemory( g_cmdbuf, g_cmdbufMemBlock, 0, CMDMEMSIZE );

    for (i = 0; i < FB_NUM; i++)
    {
        DkImageView colorView, depthView;
        dkImageViewDefaults( &colorView, &g_framebuffers[i] );
        dkImageViewDefaults( &depthView, &g_depthBuffer );
        dkCmdBufBindRenderTarget( g_cmdbuf, &colorView, &depthView );
        g_cmdsBindFramebuffer[i] = dkCmdBufFinishList( g_cmdbuf );
    }

    /* Full-screen clear plus one off-center box, entirely via
     * scissor+clear -- no shaders, no vertex/index buffers, nothing that
     * needs the uam shader compiler or a romfs image. */
    full.x = 0;      full.y = 0;      full.width = FB_WIDTH;  full.height = FB_HEIGHT;
    box.x  = FB_WIDTH / 2 - 200; box.y = FB_HEIGHT / 2 - 200; box.width = 400; box.height = 400;

    dkCmdBufSetScissors( g_cmdbuf, 0, &full, 1 );
    dkCmdBufClearColorFloat( g_cmdbuf, 0, DkColorMask_RGBA, 0.07f, 0.09f, 0.18f, 1.0f );
    dkCmdBufSetScissors( g_cmdbuf, 0, &box, 1 );
    dkCmdBufClearColorFloat( g_cmdbuf, 0, DkColorMask_RGBA, 0.37f, 0.92f, 0.83f, 1.0f );
    g_cmdsRender = dkCmdBufFinishList( g_cmdbuf );

    /* Texture upload: a CPU-writable staging buffer (plain data memory, no
     * DkMemBlockFlags_Image -- that flag is for GPU-native image storage,
     * not a linear source buffer) filled with a gradient, then copied onto
     * a sub-rect of each framebuffer image via the
     * dkCmdBufCopyBufferToImage() pattern proven in gpu_console.c. One
     * static list per framebuffer slot, built once, matching how the
     * per-slot framebuffer-bind lists already work above. */
    dkMemBlockMakerDefaults( &memBlockMaker, g_device, TEX_W * TEX_H * 4 );
    memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    g_texStagingMemBlock = dkMemBlockCreate( &memBlockMaker );
    fill_gradient( (uint8_t *)dkMemBlockGetCpuAddr( g_texStagingMemBlock ) );

    {
        DkCopyBuf copySrc;
        DkImageRect copyDst;

        copySrc.addr = dkMemBlockGetGpuAddr( g_texStagingMemBlock );
        copySrc.rowLength = 0;
        copySrc.imageHeight = 0;

        copyDst.x = TEX_X; copyDst.y = TEX_Y; copyDst.z = 0;
        copyDst.width = TEX_W; copyDst.height = TEX_H; copyDst.depth = 1;

        for (i = 0; i < FB_NUM; i++)
        {
            DkImageView dstView;
            dkImageViewDefaults( &dstView, &g_framebuffers[i] );
            dkCmdBufCopyBufferToImage( g_cmdbuf, &copySrc, &dstView, &copyDst, 0 );
            g_cmdsTexUpload[i] = dkCmdBufFinishList( g_cmdbuf );
        }
    }
    logmsg( "[INIT] texture upload lists built" );

    /* Cube: shaders, vertex buffer, uniform buffer, dynamic push-constant
     * ring, and the static draw list itself. */
    {
        DkMemBlockMaker codeMaker;
        DkVtxAttribState const *attribState = g_cubeAttribState;
        DkVtxBufferState const *bufferState = g_cubeBufferState;
        DkShader const *shaders[2];
        DkRasterizerState rasterizerState;
        DkColorState colorState;
        DkColorWriteState colorWriteState;
        DkDepthStencilState depthStencilState;
        DkViewport viewport;

        dkMemBlockMakerDefaults( &codeMaker, g_device, CODEMEMSIZE );
        codeMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
        g_codeMemBlock = dkMemBlockCreate( &codeMaker );
        g_codeMemOffset = 0;

        loadShader( &g_vertexShader, "romfs:/shaders/transform_vsh.dksh" );
        logmsg( "[INIT] vertex shader loaded" );
        loadShader( &g_fragmentShader, "romfs:/shaders/color_fsh.dksh" );
        logmsg( "[INIT] fragment shader loaded" );

        /* DkMemBlockCreate() sizes must be rounded up to DK_MEMBLOCK_ALIGNMENT
         * (4096) -- confirmed by gpu_console.c, which rounds even its small
         * CPU-data buffers this way (e.g. its scratchMemBlock). Missed here
         * for these three small, non-page-sized allocations; the framebuffer/
         * depth images were already safe (dkImageLayoutGetAlignment() rounding
         * covers those), and CMDMEMSIZE/TEXTCMDMEMSIZE/the gradient texture's
         * byte size all already happen to be exact multiples of 4096. */
        dkMemBlockMakerDefaults( &memBlockMaker, g_device,
                                 (sizeof(g_cubeVertexData) + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1) );
        memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        g_vtxMemBlock = dkMemBlockCreate( &memBlockMaker );
        memcpy( dkMemBlockGetCpuAddr( g_vtxMemBlock ), g_cubeVertexData, sizeof(g_cubeVertexData) );
        logmsg( "[INIT] vertex buffer created+filled" );

        dkMemBlockMakerDefaults( &memBlockMaker, g_device,
                                 (sizeof(Transformation) + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1) );
        memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        g_uniformMemBlock = dkMemBlockCreate( &memBlockMaker );
        logmsg( "[INIT] uniform buffer created" );

        dkMemBlockMakerDefaults( &memBlockMaker, g_device,
                                 (DYN_RING_SLOTS * DYN_SLICE_SIZE + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1) );
        memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        g_dynMemBlock = dkMemBlockCreate( &memBlockMaker );
        dkCmdBufMakerDefaults( &cmdbufMaker, g_device );
        g_dynCmdbuf = dkCmdBufCreate( &cmdbufMaker );
        logmsg( "[INIT] dynamic cmd ring created" );

        g_transform.projMtx = mat4_perspective( 40.0f * 3.14159265f / 180.0f,
                                                (float)FB_WIDTH / (float)FB_HEIGHT, 0.01f, 1000.0f );

        shaders[0] = &g_vertexShader;
        shaders[1] = &g_fragmentShader;
        dkRasterizerStateDefaults( &rasterizerState );
        dkColorStateDefaults( &colorState );
        dkColorWriteStateDefaults( &colorWriteState );
        dkDepthStencilStateDefaults( &depthStencilState );
        depthStencilState.depthTestEnable = 1;
        depthStencilState.depthWriteEnable = 1;
        depthStencilState.depthCompareOp = DkCompareOp_Less;

        viewport.x = 0.0f; viewport.y = 0.0f;
        viewport.width = (float)FB_WIDTH; viewport.height = (float)FB_HEIGHT;
        viewport.near = 0.0f; viewport.far = 1.0f;

        dkCmdBufSetViewports( g_cmdbuf, 0, &viewport, 1 );
        dkCmdBufSetScissors( g_cmdbuf, 0, &full, 1 );
        dkCmdBufClearDepthStencil( g_cmdbuf, true, 1.0f, 0xFF, 0 );
        dkCmdBufBindShaders( g_cmdbuf, DkStageFlag_GraphicsMask, shaders, 2 );
        dkCmdBufBindUniformBuffer( g_cmdbuf, DkStage_Vertex, 0, dkMemBlockGetGpuAddr( g_uniformMemBlock ),
                                   sizeof(Transformation) );
        dkCmdBufBindRasterizerState( g_cmdbuf, &rasterizerState );
        dkCmdBufBindColorState( g_cmdbuf, &colorState );
        dkCmdBufBindColorWriteState( g_cmdbuf, &colorWriteState );
        dkCmdBufBindDepthStencilState( g_cmdbuf, &depthStencilState );
        dkCmdBufBindVtxBuffer( g_cmdbuf, 0, dkMemBlockGetGpuAddr( g_vtxMemBlock ), sizeof(g_cubeVertexData) );
        dkCmdBufBindVtxAttribState( g_cmdbuf, attribState, 2 );
        dkCmdBufBindVtxBufferState( g_cmdbuf, bufferState, 1 );
        dkCmdBufDraw( g_cmdbuf, DkPrimitive_Quads, 24, 1, 0, 0 );
        dkCmdBufBarrier( g_cmdbuf, DkBarrier_Fragments, 0 );
        dkCmdBufDiscardDepthStencil( g_cmdbuf );
        g_cmdsCube = dkCmdBufFinishList( g_cmdbuf );
    }
    logmsg( "[INIT] cube static command list built" );

    /* Separate cmdbuf/memory for the FPS text: it gets rebuilt once a
     * second (see rebuild_fps_text()), and must never share memory with
     * the always-static lists above -- dkCmdBufClear() would invalidate
     * whatever else happened to live in the same block. */
    dkMemBlockMakerDefaults( &memBlockMaker, g_device, TEXTCMDMEMSIZE );
    memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    g_textCmdbufMemBlock = dkMemBlockCreate( &memBlockMaker );

    dkCmdBufMakerDefaults( &cmdbufMaker, g_device );
    g_textCmdbuf = dkCmdBufCreate( &cmdbufMaker );
    dkCmdBufAddMemory( g_textCmdbuf, g_textCmdbufMemBlock, 0, TEXTCMDMEMSIZE );

    dkQueueMakerDefaults( &queueMaker, g_device );
    queueMaker.flags = DkQueueFlags_Graphics;
    g_renderQueue = dkQueueCreate( &queueMaker );

    rebuild_fps_text();   /* seed an initial "FPS:0" list before the first frame */
    logmsg( "[INIT] queue created, fps text seeded -- init complete" );
}

static void graphicsUpdate(void)
{
    int slot;
    uint64_t now_ns;
    float time_s, angleY, angleX;
    Mat4 t, rx, ry, s, mv;
    DkCmdList dynList;
    static unsigned frameNo;

    if (frameNo < 5) logmsg( "[FRAME %u] start", frameNo );

    now_ns = armTicksToNs( armGetSystemTick() );
    if (!g_fps_epoch_ns) g_fps_epoch_ns = now_ns;
    if (now_ns - g_fps_epoch_ns >= 1000000000ULL)
    {
        g_fps_display = g_fps_count;
        g_fps_count = 0;
        g_fps_epoch_ns = now_ns;

        dkQueueWaitIdle( g_renderQueue );   /* text cmdbuf memory is about to be reused */
        rebuild_fps_text();
    }

    /* Model-view matrix for this frame: translate back, rotate Y at a
     * constant rate, wobble X gently, scale down -- mirrors Example03's
     * "Translate * RotateX * RotateY * Scale" composition (each step
     * right-multiplies, so Scale applies first to the unit cube, then
     * RotateY, then RotateX, then Translate). */
    time_s = (float)(now_ns / 1000000ULL) / 1000.0f;
    angleY = fmodf( time_s * 0.6f, 2.0f * 3.14159265f );
    angleX = 0.35f * sinf( time_s * 0.9f );

    t  = mat4_translate( 0.0f, 0.0f, -3.0f );
    rx = mat4_rotate_x( angleX );
    ry = mat4_rotate_y( angleY );
    s  = mat4_scale( 0.6f );
    mv = mat4_multiply( &t, &rx );
    mv = mat4_multiply( &mv, &ry );
    mv = mat4_multiply( &mv, &s );
    g_transform.mdlvMtx = mv;

    dynList = record_dynamic_transform();
    dkQueueSubmitCommands( g_renderQueue, dynList );

    slot = dkQueueAcquireImage( g_renderQueue, g_swapchain );
    dkQueueSubmitCommands( g_renderQueue, g_cmdsBindFramebuffer[slot] );
    dkQueueSubmitCommands( g_renderQueue, g_cmdsRender );
    dkQueueSubmitCommands( g_renderQueue, g_cmdsTexUpload[slot] );
    dkQueueSubmitCommands( g_renderQueue, g_cmdsCube );
    dkQueueSubmitCommands( g_renderQueue, g_cmdsText );
    dkQueuePresentImage( g_renderQueue, g_swapchain, slot );

    if (frameNo < 5) logmsg( "[FRAME %u] presented", frameNo );
    frameNo++;
    g_fps_count++;
}

static void graphicsExit(void)
{
    dkQueueWaitIdle( g_renderQueue );
    dkQueueDestroy( g_renderQueue );
    dkCmdBufDestroy( g_textCmdbuf );
    dkMemBlockDestroy( g_textCmdbufMemBlock );
    dkMemBlockDestroy( g_texStagingMemBlock );
    dkCmdBufDestroy( g_dynCmdbuf );
    dkMemBlockDestroy( g_dynMemBlock );
    dkMemBlockDestroy( g_uniformMemBlock );
    dkMemBlockDestroy( g_vtxMemBlock );
    dkMemBlockDestroy( g_codeMemBlock );
    dkCmdBufDestroy( g_cmdbuf );
    dkMemBlockDestroy( g_cmdbufMemBlock );
    dkSwapchainDestroy( g_swapchain );
    dkMemBlockDestroy( g_depthMemBlock );
    dkMemBlockDestroy( g_framebufferMemBlock );
    dkDeviceDestroy( g_device );
}

int main( int argc, char *argv[] )
{
    PadState pad;

    (void)argc;
    (void)argv;

    log_open();
    logmsg( "[BOOT] deko3d_smoke starting" );

    /* Required before any romfs:/ path can be opened -- loadShader() uses
     * fopen("romfs:/shaders/...") to load the uam-compiled shaders. Missing
     * on the first hardware run of this stage: fopen() returned NULL and
     * the unchecked fseek(NULL, ...) right after it crashed immediately. */
    romfsInit();
    logmsg( "[BOOT] romfsInit done" );

    graphicsInitialize();
    logmsg( "[BOOT] graphicsInitialize done" );

    padConfigureInput( 1, HidNpadStyleSet_NpadStandard );
    padInitializeDefault( &pad );

    while (appletMainLoop())
    {
        u64 kDown;

        padUpdate( &pad );
        kDown = padGetButtonsDown( &pad );
        if (kDown & HidNpadButton_Plus) break;   /* return to hbmenu */

        graphicsUpdate();
    }

    graphicsExit();
    romfsExit();
    logmsg( "[BOOT] clean exit" );
    return 0;
}
