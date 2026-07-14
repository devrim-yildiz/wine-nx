#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <deko3d.h>

/* Deko3d bring-up smoke test -- step 1 of the GPU-compositor milestone
 * (see wine-nx-probe/3d-accel-scoping.md). Proves device/queue/swapchain
 * creation and present against the exact same nwindowGetDefault() handle
 * wine_nx_fb_init() already uses for the libnx framebuffer path, with the
 * smallest possible surface: no shaders, no vertex buffers, no romfs, just
 * scissor rectangles and clear-color commands. A textured quad (the real
 * compositor's actual workload -- upload a Wine DIB as a GPU texture,
 * composite, present) is a deliberate follow-up once this bare pipeline is
 * confirmed working on real hardware, not assumed from the devkitPro
 * examples it's adapted from (deko_basic and Example01_SimpleSetup).
 *
 * HARDWARE-CONFIRMED WORKING: dark background with the cyan box rendering
 * correctly on real Switch hardware -- device/queue/swapchain/present all
 * functional against nwindowGetDefault(). A textured quad is still the
 * next step, not yet built. */

#define FB_NUM 2
#define FB_WIDTH  1280
#define FB_HEIGHT 720
#define CMDMEMSIZE (16 * 1024)

static DkDevice g_device;
static DkMemBlock g_framebufferMemBlock;
static DkImage g_framebuffers[FB_NUM];
static DkSwapchain g_swapchain;

static DkMemBlock g_cmdbufMemBlock;
static DkCmdBuf g_cmdbuf;
static DkCmdList g_cmdsBindFramebuffer[FB_NUM];
static DkCmdList g_cmdsRender;

static DkQueue g_renderQueue;

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
    g_device = dkDeviceCreate( &deviceMaker );

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

    dkMemBlockMakerDefaults( &memBlockMaker, g_device, CMDMEMSIZE );
    memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    g_cmdbufMemBlock = dkMemBlockCreate( &memBlockMaker );

    dkCmdBufMakerDefaults( &cmdbufMaker, g_device );
    g_cmdbuf = dkCmdBufCreate( &cmdbufMaker );
    dkCmdBufAddMemory( g_cmdbuf, g_cmdbufMemBlock, 0, CMDMEMSIZE );

    for (i = 0; i < FB_NUM; i++)
    {
        DkImageView imageView;
        dkImageViewDefaults( &imageView, &g_framebuffers[i] );
        dkCmdBufBindRenderTarget( g_cmdbuf, &imageView, NULL );
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

    dkQueueMakerDefaults( &queueMaker, g_device );
    queueMaker.flags = DkQueueFlags_Graphics;
    g_renderQueue = dkQueueCreate( &queueMaker );
}

static void graphicsUpdate(void)
{
    int slot = dkQueueAcquireImage( g_renderQueue, g_swapchain );
    dkQueueSubmitCommands( g_renderQueue, g_cmdsBindFramebuffer[slot] );
    dkQueueSubmitCommands( g_renderQueue, g_cmdsRender );
    dkQueuePresentImage( g_renderQueue, g_swapchain, slot );
}

static void graphicsExit(void)
{
    dkQueueWaitIdle( g_renderQueue );
    dkQueueDestroy( g_renderQueue );
    dkCmdBufDestroy( g_cmdbuf );
    dkMemBlockDestroy( g_cmdbufMemBlock );
    dkSwapchainDestroy( g_swapchain );
    dkMemBlockDestroy( g_framebufferMemBlock );
    dkDeviceDestroy( g_device );
}

int main( int argc, char *argv[] )
{
    PadState pad;

    (void)argc;
    (void)argv;

    graphicsInitialize();

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
    return 0;
}
