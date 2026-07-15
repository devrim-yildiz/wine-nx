#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <switch.h>
#include <deko3d.h>

/* Deko3d compositor backend for wine_nx_fb_lock/unlock/present -- additive
 * and opt-in (set WINE_NX_DEKO3D to any non-empty value; unset/default is
 * the existing, untouched libnx-framebuffer path in runtime.c). Reuses
 * exactly the mechanism proven hardware-working in
 * wine-nx-probe/source/deko3d_smoke.c's stage 2: a CPU-writable staging
 * buffer copied onto the current swapchain image via
 * dkCmdBufCopyBufferToImage(). No shaders/samplers needed -- Wine's DIB is
 * already flat RGBA8 pixel data, the same shape the smoke test's gradient
 * upload proved works. The two copy command lists (one per framebuffer
 * slot) are built once at init and resubmitted every present: only the
 * *content* at the referenced GPU address changes frame to frame, not the
 * command encoding itself, so no per-frame command rebuild is needed here
 * (unlike the smoke test's cube, which had to re-encode new matrix values
 * inline via pushConstants every frame).
 *
 * Handle-type creators (dkDeviceCreate, dkMemBlockCreate, dkCmdBufCreate,
 * dkQueueCreate, dkSwapchainCreate) return real pointers -- confirmed via
 * DK_DECL_HANDLE's expansion to `struct tag_DkX*` in deko3d.h, as opposed
 * to DK_DECL_OPAQUE value-types like DkImage/DkFence/DkShader, which have
 * no return value to check at all. Every handle creator below is
 * NULL-checked and logged; everything else relies on the cbDebug callback
 * (also wired below), which reports the actual DkResult code deko3d itself
 * assigns to a caught error, not a guess. No reference example ever checks
 * these for NULL either, but they're real pointers per the header, so
 * checking is free insurance, not fabricated protocol.
 *
 * Every log line goes through wine_nx_runtime_trace() -- the same
 * fflush-per-line path runtime.c's own log_line() already uses for
 * wine-nx-runtime.log, proven to survive a hard crash (that's exactly how
 * both bugs in the smoke test's cube stage got root-caused from a single
 * hardware log, no extra round-trip). Nothing here invents a new log file
 * or a new flush discipline. */

extern void wine_nx_runtime_trace( const char *msg );

/* Shared HUD stat-tracking/drawing, factored out of runtime.c's libnx
 * wine_nx_fb_present() so this backend drives the exact same on-screen FPS
 * overlay and rolling window/avg/min/max stats instead of a second, drifted
 * implementation -- see the comment on wine_nx_hud_mark_attempt() in
 * runtime.c for the attempted/executed semantics. */
extern void wine_nx_hud_mark_attempt(void);
extern void wine_nx_hud_mark_executed( uint64_t present_ms );
extern void wine_nx_hud_draw( void *bits, int fb_w, int fb_h, int stride );

#define WINE_NX_DEKO3D_FB_W 1280
#define WINE_NX_DEKO3D_FB_H 720
#define WINE_NX_DEKO3D_FB_NUM 2
#define WINE_NX_DEKO3D_CMDMEMSIZE (16 * 1024)

static DkDevice g_dk_device;
static DkQueue g_dk_queue;

static ViDisplay g_dk_viDisplay;
static ViLayer g_dk_viLayer;
static NWindow g_dk_win;

static DkMemBlock g_dk_fbMemBlock;
static DkImage g_dk_framebuffers[WINE_NX_DEKO3D_FB_NUM];
static DkSwapchain g_dk_swapchain;

static DkMemBlock g_dk_cmdMemBlock;
static DkCmdBuf g_dk_cmdbuf;
static DkCmdList g_dk_cmdsCopy[WINE_NX_DEKO3D_FB_NUM];

static DkMemBlock g_dk_stagingMemBlock;
static void *g_dk_stagingCpuAddr;
static DkFence g_dk_stagingFence;
static int g_dk_stagingFenceValid;

static pthread_mutex_t g_dk_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *g_dk_pendingBits;
static int g_dk_pendingDirty;
static int g_dk_lockDepth;
static int g_dk_ready;
static int g_dk_init_failed;

static void wine_nx_deko3d_trace( const char *fmt, ... )
{
    char buf[256];
    va_list args;
    va_start( args, fmt );
    vsnprintf( buf, sizeof(buf), fmt, args );
    va_end( args );
    wine_nx_runtime_trace( buf );
}

/* Reports validation errors deko3d itself catches -- unset by default (see
 * DkDeviceMaker.cbDebug in deko3d.h), which would otherwise drop these on
 * the floor. Same technique used in deko3d_smoke.c after its first
 * hardware crash produced no diagnostic information at all. */
static void wine_nx_deko3d_debug_callback( void *userData, const char *context, DkResult result, const char *message )
{
    (void)userData;
    wine_nx_deko3d_trace( "[NXDK][DKERR] context=%s result=%d message=%s",
                          context ? context : "(null)", (int)result, message ? message : "(null)" );
}

int wine_nx_deko3d_fb_init(void)
{
    DkDeviceMaker deviceMaker;
    DkImageLayoutMaker imageLayoutMaker;
    DkImageLayout framebufferLayout;
    DkMemBlockMaker memBlockMaker;
    DkImage const *swapchainImages[WINE_NX_DEKO3D_FB_NUM];
    DkSwapchainMaker swapchainMaker;
    DkCmdBufMaker cmdbufMaker;
    DkQueueMaker queueMaker;
    uint32_t fbSize, fbAlign, stagingSize;
    unsigned i;

    if (g_dk_ready) return 0;
    if (g_dk_init_failed) return -1;   /* don't retry a known-bad init every present() call */

    wine_nx_deko3d_trace( "[NXDK] init: starting deko3d compositor backend" );

    dkDeviceMakerDefaults( &deviceMaker );
    deviceMaker.cbDebug = wine_nx_deko3d_debug_callback;
    g_dk_device = dkDeviceCreate( &deviceMaker );
    if (!g_dk_device)
    {
        wine_nx_deko3d_trace( "[NXDK] init FAILED at dkDeviceCreate (returned NULL)" );
        g_dk_init_failed = 1;
        return -1;
    }
    wine_nx_deko3d_trace( "[NXDK] device created" );

    dkImageLayoutMakerDefaults( &imageLayoutMaker, g_dk_device );
    imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    imageLayoutMaker.format = DkImageFormat_RGBA8_Unorm;
    imageLayoutMaker.dimensions[0] = WINE_NX_DEKO3D_FB_W;
    imageLayoutMaker.dimensions[1] = WINE_NX_DEKO3D_FB_H;
    dkImageLayoutInitialize( &framebufferLayout, &imageLayoutMaker );

    fbSize  = dkImageLayoutGetSize( &framebufferLayout );
    fbAlign = dkImageLayoutGetAlignment( &framebufferLayout );
    fbSize = (fbSize + fbAlign - 1) & ~(fbAlign - 1);

    dkMemBlockMakerDefaults( &memBlockMaker, g_dk_device, WINE_NX_DEKO3D_FB_NUM * fbSize );
    memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    g_dk_fbMemBlock = dkMemBlockCreate( &memBlockMaker );
    if (!g_dk_fbMemBlock)
    {
        wine_nx_deko3d_trace( "[NXDK] init FAILED at dkMemBlockCreate for framebuffers (returned NULL, size=%u)", fbSize * WINE_NX_DEKO3D_FB_NUM );
        g_dk_init_failed = 1;
        return -1;
    }

    for (i = 0; i < WINE_NX_DEKO3D_FB_NUM; i++)
    {
        swapchainImages[i] = &g_dk_framebuffers[i];
        dkImageInitialize( &g_dk_framebuffers[i], &framebufferLayout, g_dk_fbMemBlock, i * fbSize );
    }
    wine_nx_deko3d_trace( "[NXDK] framebuffer images created" );

    /* nwindowGetDefault() turned out to be structurally unusable here: per
     * libnx's own nx/source/display/default_window.c (__nx_win_init, the
     * code that actually builds the "default" NWindow the whole runtime
     * shares), nwindowSetDimensions() is called on it exactly once, at
     * process startup, before main() even runs -- confirmed on hardware via
     * a diagnostic probe that got back MAKERESULT(Module_Libnx,
     * LibnxError_AlreadyInitialized) (0xf59) when calling it a second time,
     * cleanly, not a crash. dkSwapchainCreate() makes that same call
     * internally (dk_swapchain.cpp, Swapchain::initialize(), devkitPro/deko3d
     * master), so it always hit the same wall. No public libnx API undoes
     * that lock (nwindowReleaseBuffers() only clears buffer registrations,
     * confirmed by testing that fix in isolation -- it changed nothing).
     *
     * The fix is to stop sharing the default window at all: build our own
     * ViDisplay/ViLayer/NWindow, the same real sequence __nx_win_init uses
     * for the *default* one, so ours starts from a clean, never-configured
     * state. viInitialize() is refcounted (NX_GENERATE_SERVICE_GUARD_PARAMS
     * in libnx's vi.c), so calling it again here on top of the runtime's
     * existing vi session is safe -- it just bumps the refcount. viCreateLayer()
     * is a plain per-call IPC request (vi.c), not a singleton, so a second,
     * independent layer on the same physical display is the supported way
     * multiple consumers coexist on Horizon (this is how overlays/capture
     * work) -- not a workaround. */
    {
        Result rc;

        rc = viInitialize( ViServiceType_Default );
        wine_nx_deko3d_trace( "[NXDK] viInitialize rc=0x%x", rc );
        if (R_FAILED( rc ))
        {
            wine_nx_deko3d_trace( "[NXDK] init FAILED at viInitialize" );
            g_dk_init_failed = 1;
            return -1;
        }

        rc = viOpenDefaultDisplay( &g_dk_viDisplay );
        wine_nx_deko3d_trace( "[NXDK] viOpenDefaultDisplay rc=0x%x", rc );
        if (R_FAILED( rc ))
        {
            wine_nx_deko3d_trace( "[NXDK] init FAILED at viOpenDefaultDisplay" );
            g_dk_init_failed = 1;
            return -1;
        }

        rc = viCreateLayer( &g_dk_viDisplay, &g_dk_viLayer );
        wine_nx_deko3d_trace( "[NXDK] viCreateLayer rc=0x%x", rc );
        if (R_FAILED( rc ))
        {
            wine_nx_deko3d_trace( "[NXDK] init FAILED at viCreateLayer" );
            g_dk_init_failed = 1;
            return -1;
        }

        rc = viSetLayerScalingMode( &g_dk_viLayer, ViScalingMode_FitToLayer );
        wine_nx_deko3d_trace( "[NXDK] viSetLayerScalingMode rc=0x%x", rc );
        if (R_FAILED( rc ))
        {
            wine_nx_deko3d_trace( "[NXDK] init FAILED at viSetLayerScalingMode" );
            g_dk_init_failed = 1;
            return -1;
        }

        rc = nwindowCreateFromLayer( &g_dk_win, &g_dk_viLayer );
        wine_nx_deko3d_trace( "[NXDK] nwindowCreateFromLayer rc=0x%x", rc );
        if (R_FAILED( rc ))
        {
            wine_nx_deko3d_trace( "[NXDK] init FAILED at nwindowCreateFromLayer" );
            g_dk_init_failed = 1;
            return -1;
        }

        rc = nwindowSetDimensions( &g_dk_win, WINE_NX_DEKO3D_FB_W, WINE_NX_DEKO3D_FB_H );
        wine_nx_deko3d_trace( "[NXDK] nwindowSetDimensions rc=0x%x", rc );
        if (R_FAILED( rc ))
        {
            wine_nx_deko3d_trace( "[NXDK] init FAILED at nwindowSetDimensions" );
            g_dk_init_failed = 1;
            return -1;
        }

        wine_nx_deko3d_trace( "[NXDK] dedicated ViLayer/NWindow ready" );
    }

    dkSwapchainMakerDefaults( &swapchainMaker, g_dk_device, &g_dk_win, swapchainImages, WINE_NX_DEKO3D_FB_NUM );
    wine_nx_deko3d_trace( "[NXDK] swapchain maker configured, calling dkSwapchainCreate" );
    g_dk_swapchain = dkSwapchainCreate( &swapchainMaker );
    if (!g_dk_swapchain)
    {
        wine_nx_deko3d_trace( "[NXDK] init FAILED at dkSwapchainCreate (returned NULL)" );
        g_dk_init_failed = 1;
        return -1;
    }
    wine_nx_deko3d_trace( "[NXDK] swapchain created" );

    /* Staging buffer: the full-screen CPU-writable source Wine's DIB gets
     * copied from. DkMemBlockCreate() sizes must be rounded up to
     * DK_MEMBLOCK_ALIGNMENT (4096) -- confirmed by gpu_console.c, which
     * rounds even its small CPU-data buffers this way. Missing this
     * rounding crashed the smoke test's cube stage hard enough that even
     * cbDebug never fired; not repeating that here. */
    stagingSize = (WINE_NX_DEKO3D_FB_W * WINE_NX_DEKO3D_FB_H * 4 + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1);
    dkMemBlockMakerDefaults( &memBlockMaker, g_dk_device, stagingSize );
    memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    g_dk_stagingMemBlock = dkMemBlockCreate( &memBlockMaker );
    if (!g_dk_stagingMemBlock)
    {
        wine_nx_deko3d_trace( "[NXDK] init FAILED at dkMemBlockCreate for staging buffer (returned NULL, size=%u)", stagingSize );
        g_dk_init_failed = 1;
        return -1;
    }
    g_dk_stagingCpuAddr = dkMemBlockGetCpuAddr( g_dk_stagingMemBlock );
    wine_nx_deko3d_trace( "[NXDK] staging buffer created (%u bytes)", stagingSize );

    dkMemBlockMakerDefaults( &memBlockMaker, g_dk_device, WINE_NX_DEKO3D_CMDMEMSIZE );
    memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    g_dk_cmdMemBlock = dkMemBlockCreate( &memBlockMaker );
    if (!g_dk_cmdMemBlock)
    {
        wine_nx_deko3d_trace( "[NXDK] init FAILED at dkMemBlockCreate for command memory (returned NULL)" );
        g_dk_init_failed = 1;
        return -1;
    }

    dkCmdBufMakerDefaults( &cmdbufMaker, g_dk_device );
    g_dk_cmdbuf = dkCmdBufCreate( &cmdbufMaker );
    if (!g_dk_cmdbuf)
    {
        wine_nx_deko3d_trace( "[NXDK] init FAILED at dkCmdBufCreate (returned NULL)" );
        g_dk_init_failed = 1;
        return -1;
    }
    dkCmdBufAddMemory( g_dk_cmdbuf, g_dk_cmdMemBlock, 0, WINE_NX_DEKO3D_CMDMEMSIZE );

    /* One static copy list per framebuffer slot -- built once here, then
     * resubmitted unchanged every present(). CopyBufferToImage needs no
     * bound render target first: gpu_console.c's own font-tileset upload
     * (the only CopyBufferToImage usage anywhere in the devkitPro
     * examples) never binds one either, it copies directly via the
     * explicit DkImageView destination parameter. */
    {
        DkCopyBuf copySrc;
        DkImageRect copyDst;

        copySrc.addr = dkMemBlockGetGpuAddr( g_dk_stagingMemBlock );
        copySrc.rowLength = 0;
        copySrc.imageHeight = 0;

        copyDst.x = 0; copyDst.y = 0; copyDst.z = 0;
        copyDst.width = WINE_NX_DEKO3D_FB_W; copyDst.height = WINE_NX_DEKO3D_FB_H; copyDst.depth = 1;

        for (i = 0; i < WINE_NX_DEKO3D_FB_NUM; i++)
        {
            DkImageView dstView;
            dkImageViewDefaults( &dstView, &g_dk_framebuffers[i] );
            dkCmdBufCopyBufferToImage( g_dk_cmdbuf, &copySrc, &dstView, &copyDst, 0 );
            g_dk_cmdsCopy[i] = dkCmdBufFinishList( g_dk_cmdbuf );
        }
    }
    wine_nx_deko3d_trace( "[NXDK] copy command lists built" );

    dkQueueMakerDefaults( &queueMaker, g_dk_device );
    queueMaker.flags = DkQueueFlags_Graphics;
    g_dk_queue = dkQueueCreate( &queueMaker );
    if (!g_dk_queue)
    {
        wine_nx_deko3d_trace( "[NXDK] init FAILED at dkQueueCreate (returned NULL)" );
        g_dk_init_failed = 1;
        return -1;
    }

    g_dk_ready = 1;
    wine_nx_deko3d_trace( "[NXDK] init complete -- deko3d compositor active" );
    return 0;
}

void *wine_nx_deko3d_fb_lock( int *width, int *height, int *stride_px )
{
    if (!g_dk_ready && wine_nx_deko3d_fb_init()) return NULL;

    pthread_mutex_lock( &g_dk_mutex );
    if (!g_dk_pendingBits)
    {
        /* Starting a fresh (not-yet-presented) use of the staging buffer:
         * wait for any previous present's copy to finish reading it first,
         * so the CPU doesn't overwrite pixels the GPU hasn't consumed yet.
         * Same technique gpu_console.c uses to protect its own CPU-written
         * charBuf (dkFenceWait before touching it, dkQueueSignalFence
         * right after submitting the commands that read it). Timed because
         * this is a genuine CPU-stall point the old libnx path never had:
         * if the GPU is behind, this is where a frame's time actually goes,
         * not in GDI paint cost or the message loop. */
        if (g_dk_stagingFenceValid)
        {
            uint64_t t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
            dkFenceWait( &g_dk_stagingFence, UINT64_MAX );
            uint64_t t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
            wine_nx_deko3d_trace( "[NXDK][TIMING] fenceWait took %ums", (unsigned)(t1 - t0) );
            g_dk_stagingFenceValid = 0;
        }
        g_dk_pendingBits = g_dk_stagingCpuAddr;
    }
    g_dk_lockDepth++;
    if (width)     *width     = WINE_NX_DEKO3D_FB_W;
    if (height)    *height    = WINE_NX_DEKO3D_FB_H;
    if (stride_px) *stride_px = WINE_NX_DEKO3D_FB_W;   /* tightly packed, no tiling padding */
    return g_dk_pendingBits;
}

void wine_nx_deko3d_fb_unlock(void)
{
    g_dk_pendingDirty = 1;
    if (g_dk_lockDepth > 0) g_dk_lockDepth--;
    pthread_mutex_unlock( &g_dk_mutex );
}

/* Independent of wine_nx_hud_mark_attempt/executed's rolling window (which
 * feeds the on-screen HUD, shared with the libnx path) -- this is a separate,
 * deko3d-only counter specifically for the hardware log, so a presents/sec
 * number is readable straight from wine-nx-runtime-deko3d.log without
 * needing to read pixels off a screenshot. */
static unsigned int g_dk_presentLogCount;
static uint64_t g_dk_presentLogEpochMs;

void wine_nx_deko3d_fb_present(void)
{
    pthread_mutex_lock( &g_dk_mutex );
    if (g_dk_ready && g_dk_pendingBits && g_dk_pendingDirty && !g_dk_lockDepth)
    {
        uint64_t t0, t1, now_ms;
        int slot;

        wine_nx_hud_mark_attempt();

        t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
        slot = dkQueueAcquireImage( g_dk_queue, g_dk_swapchain );
        t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
        wine_nx_deko3d_trace( "[NXDK][TIMING] dkQueueAcquireImage took %ums slot=%d", (unsigned)(t1 - t0), slot );

        if (slot < 0 || slot >= WINE_NX_DEKO3D_FB_NUM)
        {
            /* No reference example checks this, but it's a plain int used
             * directly as an array index everywhere it appears -- a bounds
             * check here is free insurance against an out-of-range value
             * turning into an out-of-bounds g_dk_cmdsCopy[] read, not a
             * claim about what a negative/oversized value specifically
             * means. */
            wine_nx_deko3d_trace( "[NXDK] present: dkQueueAcquireImage returned out-of-range slot=%d", slot );
        }
        else
        {
            wine_nx_hud_draw( g_dk_pendingBits, WINE_NX_DEKO3D_FB_W, WINE_NX_DEKO3D_FB_H, WINE_NX_DEKO3D_FB_W );

            t0 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
            dkQueueSubmitCommands( g_dk_queue, g_dk_cmdsCopy[slot] );
            dkQueueSignalFence( g_dk_queue, &g_dk_stagingFence, false );
            g_dk_stagingFenceValid = 1;
            dkQueuePresentImage( g_dk_queue, g_dk_swapchain, slot );
            t1 = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
            wine_nx_deko3d_trace( "[NXDK][TIMING] submit+signal+present took %ums", (unsigned)(t1 - t0) );
            wine_nx_hud_mark_executed( t1 - t0 );

            g_dk_pendingBits = NULL;
            g_dk_pendingDirty = 0;

            g_dk_presentLogCount++;
            now_ms = armTicksToNs( armGetSystemTick() ) / 1000000ULL;
            if (!g_dk_presentLogEpochMs) g_dk_presentLogEpochMs = now_ms;
            if (now_ms - g_dk_presentLogEpochMs >= 1000)
            {
                wine_nx_deko3d_trace( "[NXDK][TIMING] presents/sec=%u", g_dk_presentLogCount );
                g_dk_presentLogCount = 0;
                g_dk_presentLogEpochMs = now_ms;
            }
        }
    }
    pthread_mutex_unlock( &g_dk_mutex );
}
