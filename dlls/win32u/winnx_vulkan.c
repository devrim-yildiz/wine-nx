/*
 * Vulkan display driver for the Nintendo Switch (Horizon), backed by NVK
 * (wine-nx-probe/third_party/nxvk), a Mesa/NAK Vulkan driver for the Tegra
 * X1/GM20B statically linked directly into this binary. There is no dynamic
 * Vulkan loader on Horizon, so unlike x11drv/macdrv this doesn't dlopen
 * anything -- it talks straight to NVK's single exported ICD entry point,
 * vk_icdGetInstanceProcAddr(). Hardware-proven end to end (instance create,
 * device create, buffer/memory, command submission, GPU execution, verified
 * readback) by wine-nx-probe/third_party/nxvk/switch/smoke/nvk_smoke.c
 * before this file existed -- see wine-nx-probe/3d-accel-scoping.md.
 *
 * Presentation goes through Nintendo's own VK_NN_vi_surface extension (a
 * Switch-specific WSI, not part of Wine's generated wine/vulkan.h). The
 * struct/function pointer type are hand-declared below rather than pulling
 * in nxvk's own vulkan headers, which would collide with Wine's parallel,
 * independently-generated definitions of VkInstance/VkResult/etc in this
 * same translation unit -- both describe the same underlying vk.xml ABI, so
 * this is safe, and is the standard way Wine itself adds support for an
 * extension its own generator doesn't know about yet.
 *
 * Surface creation needs a dedicated ViLayer/NWindow, not the process's
 * default one -- nwindowGetDefault() is claimed once, before main(), by
 * libnx's own __nx_win_init(), and the GDI framebuffer path
 * (wine-nx-probe/source/runtime.c) already owns it. Same fix already
 * proven for deko3d (see runtime_deko3d.c's own header comment): a second,
 * independent ViLayer on the same physical display, since viCreateLayer()
 * is a plain per-call IPC request, not a singleton.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "win32u_private.h"
#include "ntuser_private.h"

/* WINE_NX_HAVE_VULKAN is only defined (wine-nx-probe/CMakeLists.txt) when
 * NVK's static archives were found at CMake-configure time and this whole
 * file is added to the build. The guard below is defense in depth in case
 * that ever changes -- without NVK linked, everything here references an
 * undefined vk_icdGetInstanceProcAddr symbol. */
#if defined(__SWITCH__) && defined(WINE_NX_HAVE_VULKAN)

extern void wine_nx_runtime_trace( const char *msg ) __attribute__((weak));

static void winnx_vk_trace( const char *fmt, ... )
{
    char buf[256];
    va_list args;

    if (!&wine_nx_runtime_trace) return;
    va_start( args, fmt );
    vsnprintf( buf, sizeof(buf), fmt, args );
    va_end( args );
    wine_nx_runtime_trace( buf );
}

/* NVK's single exported ICD entry point (wine-nx-probe/third_party/nxvk,
 * statically linked -- see wine-nx-probe/CMakeLists.txt). Declared by hand
 * to match switch/smoke/nvk_harness.h exactly; VKAPI_ATTR/VKAPI_CALL are
 * both no-ops on this AArch64 ELF target (there's only one calling
 * convention here), so a plain declaration is ABI-compatible with the real
 * exported symbol, same as every other native-side bridge function in this
 * codebase (wine_nx_fb_lock() etc). */
extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr( VkInstance instance, const char *name );

/* VK_NN_vi_surface -- Nintendo's WSI extension, not part of Wine's
 * generated wine/vulkan.h (Switch-specific, not upstream Khronos at
 * generation time). Hand-declared to match
 * wine-nx-probe/third_party/nxvk/include/vulkan/vulkan_vi.h exactly, using
 * Wine's own VkInstance/VkResult/etc types. */
#define WINE_NX_VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN ((VkStructureType)1000062000)
typedef UINT32 wine_nx_VkViSurfaceCreateFlagsNN;
typedef struct
{
    VkStructureType sType;
    const void *pNext;
    wine_nx_VkViSurfaceCreateFlagsNN flags;
    void *window;
} wine_nx_VkViSurfaceCreateInfoNN;
typedef VkResult (VKAPI_PTR *PFN_wine_nx_vkCreateViSurfaceNN)( VkInstance instance,
    const wine_nx_VkViSurfaceCreateInfoNN *create_info, const VkAllocationCallbacks *allocator,
    VkSurfaceKHR *surface );

#define WINE_NX_VK_LAYER_W 1280
#define WINE_NX_VK_LAYER_H 720

static ViDisplay wine_nx_vk_display;
static ViLayer wine_nx_vk_layer;
static NWindow wine_nx_vk_window_storage;
static NWindow *wine_nx_vk_window;   /* either &wine_nx_vk_window_storage, or deko3d's own */
static int wine_nx_vk_window_ready;
static int wine_nx_vk_window_failed;

/* Built once, lazily, the first time a real Vulkan surface is requested.
 *
 * Hardware evidence changed this function twice already:
 *   - viOpenDefaultDisplay() is NOT safe to call a second time from the
 *     same process (0x1272), even after runtime_deko3d.c's own call
 *     already succeeded -- fixed by reusing its already-open ViDisplay via
 *     wine_nx_deko3d_get_vi_display() instead of opening our own.
 *   - viCreateLayer() is ALSO not safe to call a second time (0x272),
 *     contradicting the original assumption (see the "supported way
 *     multiple consumers coexist" comment on wine_nx_deko3d_fb_init()) --
 *     true for privileged system modules, evidently not for a regular
 *     application's "vi:u" access. Fixed the same way: reuse deko3d's own
 *     already-created NWindow directly via wine_nx_deko3d_get_vi_window()
 *     instead of creating a second layer/window, so the Vulkan swapchain
 *     presents through the exact same window deko3d's own compositor
 *     already owns. UNVERIFIED how the two producers (deko3d's own
 *     dkQueuePresentImage loop and Vulkan's vkQueuePresentKHR) actually
 *     interact sharing one NWindow buffer queue -- next thing the
 *     hardware log needs to answer.
 *
 * Only falls back to opening an independent display/layer/window of our
 * own when deko3d's own compositor never got that far at all (e.g. it
 * isn't linked into this build, or its own init failed for some other
 * reason) -- untested territory, kept only so this doesn't regress a
 * hypothetical non-deko3d Vulkan-enabled build. */
static int wine_nx_vulkan_ensure_window(void)
{
    Result rc;
    NWindow *shared_window;

    if (wine_nx_vk_window_ready) return 0;
    if (wine_nx_vk_window_failed) return -1;

    rc = viInitialize( ViServiceType_Default );
    winnx_vk_trace( "[NXVK] viInitialize rc=0x%x", rc );
    if (R_FAILED( rc )) goto failed;

    {
        extern NWindow *wine_nx_deko3d_get_vi_window(void) __attribute__((weak));
        if (&wine_nx_deko3d_get_vi_window && (shared_window = wine_nx_deko3d_get_vi_window()))
        {
            winnx_vk_trace( "[NXVK] reusing deko3d's already-open NWindow" );
            wine_nx_vk_window = shared_window;
            wine_nx_vk_window_ready = 1;
            return 0;
        }
    }

    {
        extern int wine_nx_deko3d_get_vi_display( ViDisplay * ) __attribute__((weak));
        if (&wine_nx_deko3d_get_vi_display && !wine_nx_deko3d_get_vi_display( &wine_nx_vk_display ))
        {
            winnx_vk_trace( "[NXVK] reusing deko3d's already-open ViDisplay" );
            goto have_display;
        }
    }

    rc = viOpenDefaultDisplay( &wine_nx_vk_display );
    winnx_vk_trace( "[NXVK] viOpenDefaultDisplay rc=0x%x", rc );
    if (R_FAILED( rc )) goto failed;

have_display:
    rc = viCreateLayer( &wine_nx_vk_display, &wine_nx_vk_layer );
    winnx_vk_trace( "[NXVK] viCreateLayer rc=0x%x", rc );
    if (R_FAILED( rc )) goto failed;

    rc = viSetLayerScalingMode( &wine_nx_vk_layer, ViScalingMode_FitToLayer );
    winnx_vk_trace( "[NXVK] viSetLayerScalingMode rc=0x%x", rc );
    if (R_FAILED( rc )) goto failed;

    rc = nwindowCreateFromLayer( &wine_nx_vk_window_storage, &wine_nx_vk_layer );
    winnx_vk_trace( "[NXVK] nwindowCreateFromLayer rc=0x%x", rc );
    if (R_FAILED( rc )) goto failed;

    rc = nwindowSetDimensions( &wine_nx_vk_window_storage, WINE_NX_VK_LAYER_W, WINE_NX_VK_LAYER_H );
    winnx_vk_trace( "[NXVK] nwindowSetDimensions rc=0x%x", rc );
    if (R_FAILED( rc )) goto failed;

    wine_nx_vk_window = &wine_nx_vk_window_storage;
    wine_nx_vk_window_ready = 1;
    winnx_vk_trace( "[NXVK] dedicated ViLayer/NWindow ready" );
    return 0;

failed:
    wine_nx_vk_window_failed = 1;
    return -1;
}

static VkResult wine_nx_vulkan_surface_create( HWND hwnd, BOOL raw, const struct vulkan_instance *instance,
                                               VkSurfaceKHR *surface, struct client_surface **client )
{
    PFN_wine_nx_vkCreateViSurfaceNN p_vkCreateViSurfaceNN;
    wine_nx_VkViSurfaceCreateInfoNN create_info = { .sType = WINE_NX_VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN };
    VkResult res;

    (void)raw;

    if (wine_nx_vulkan_ensure_window())
    {
        winnx_vk_trace( "[NXVK] surface_create: no dedicated window available" );
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    p_vkCreateViSurfaceNN = (PFN_wine_nx_vkCreateViSurfaceNN)
        (UINT_PTR)vk_icdGetInstanceProcAddr( instance->host.instance, "vkCreateViSurfaceNN" );
    if (!p_vkCreateViSurfaceNN)
    {
        winnx_vk_trace( "[NXVK] surface_create: vkCreateViSurfaceNN not resolved" );
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    if (!(*client = nulldrv_client_surface_create( hwnd ))) return VK_ERROR_OUT_OF_HOST_MEMORY;

    create_info.window = wine_nx_vk_window;
    res = p_vkCreateViSurfaceNN( instance->host.instance, &create_info, NULL, surface );
    winnx_vk_trace( "[NXVK] vkCreateViSurfaceNN -> %d", res );
    if (res)
    {
        client_surface_release( *client );
        *client = NULL;
    }
    return res;
}

static VkBool32 wine_nx_vulkan_get_physical_device_presentation_support( struct vulkan_physical_device *physical_device, uint32_t queue )
{
    (void)physical_device;
    (void)queue;
    /* One GPU, one display -- always presentable, same answer nulldrv gives
     * for its own headless case. */
    return VK_TRUE;
}

static void wine_nx_vulkan_map_instance_extensions( struct vulkan_instance_extensions *extensions )
{
    /* NVK presents through VK_NN_vi_surface, which has no has_X bitfield in
     * Wine's generated wine/vulkan.h (Switch-specific extension, not part
     * of the vk.xml this was generated from) -- so unlike
     * nulldrv_map_instance_extensions()'s VK_EXT_headless_surface mapping,
     * there's no real host capability bit to gate this on. Presentation is
     * always available on this hardware once NVK links, so advertise
     * VK_KHR_win32_surface unconditionally; the actual "VK_NN_vi_surface"
     * host extension string gets injected directly in
     * convert_instance_create_info() (dlls/win32u/vulkan.c, __SWITCH__
     * block), the only place that builds the real, host-facing list. */
    extensions->has_VK_KHR_win32_surface = 1;
}

static void wine_nx_vulkan_map_device_extensions( struct vulkan_device_extensions *extensions )
{
    (void)extensions;
}

static const struct vulkan_driver_funcs wine_nx_vulkan_driver_funcs =
{
    .p_vulkan_surface_create = wine_nx_vulkan_surface_create,
    .p_get_physical_device_presentation_support = wine_nx_vulkan_get_physical_device_presentation_support,
    .p_map_instance_extensions = wine_nx_vulkan_map_instance_extensions,
    .p_map_device_extensions = wine_nx_vulkan_map_device_extensions,
};

UINT wine_nx_drv_VulkanInit( UINT version, void *vulkan_handle, const struct vulkan_driver_funcs **driver_funcs )
{
    (void)vulkan_handle;
    if (version != WINE_VULKAN_DRIVER_VERSION) return STATUS_NOT_IMPLEMENTED;
    *driver_funcs = &wine_nx_vulkan_driver_funcs;
    winnx_vk_trace( "[NXVK] VulkanInit ok" );
    return STATUS_SUCCESS;
}

#endif /* __SWITCH__ && WINE_NX_HAVE_VULKAN */
