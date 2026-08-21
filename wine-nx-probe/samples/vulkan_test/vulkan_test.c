/*
 * Minimal 32-bit (i386) Windows Vulkan smoke test: creates a window, loads
 * vulkan-1.dll, creates an instance/surface/device/swapchain, and presents a
 * color-cycling clear (no shaders/pipelines -- same minimal, highest-
 * confidence shape as nxvk's own nvk_vi_swapchain.c, which is already
 * hardware-proven on the native homebrew side).
 *
 * This is the first real end-to-end test of the Wine Vulkan driver wired up
 * in dlls/win32u/winnx_vulkan.c: real guest PE code -> vulkan-1.dll ->
 * winevulkan.dll's thunks -> dlls/win32u/vulkan.c -> wine_nx_drv_VulkanInit
 * -> NVK's vk_icdGetInstanceProcAddr, statically linked into
 * wine-nx-runtime. Everything up to here (NVK itself, via vk_icdGetInstanceProcAddr
 * directly, no Wine involved) was already hardware-proven by
 * wine-nx-probe/third_party/nxvk/switch/smoke/nvk_smoke.c and
 * nvk_vi_swapchain.c; this is what actually exercises the new driver code.
 *
 * Built i686-w64-mingw32 on purpose, like samples/cube32/cube32.c, to run
 * the real 32-bit WOW64/box64 guest path rather than a native ARM64 PE.
 *
 * Vulkan functions are resolved dynamically via vkGetInstanceProcAddr
 * (itself obtained from GetProcAddress on the loaded vulkan-1.dll) rather
 * than linked against an import lib -- the standard, loader-agnostic way to
 * use Vulkan on Windows, and it means this file needs no .lib at build time.
 */
#include <windows.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <stdio.h>

#define WIN_W 640
#define WIN_H 480
#define MAX_INFLIGHT 2

static FILE *g_log;

static void logf_line( const char *fmt, ... )
{
    va_list args;
    if (!g_log) return;
    va_start( args, fmt );
    vfprintf( g_log, fmt, args );
    va_end( args );
    fputc( '\n', g_log );
    fflush( g_log );
}

#define LOAD_INST( inst, fn ) \
    PFN_vk##fn fn = (PFN_vk##fn)vkGetInstanceProcAddr( (inst), "vk" #fn )
#define LOAD_DEV( dev, fn ) \
    PFN_vk##fn fn = (PFN_vk##fn)vkGetDeviceProcAddr( (dev), "vk" #fn )

static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_DESTROY) { PostQuitMessage( 0 ); return 0; }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show )
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG msg;
    HMODULE vulkan_dll;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkImage images[8];
    VkCommandBuffer cmds[MAX_INFLIGHT];
    VkSemaphore acquire_sem[MAX_INFLIGHT], present_sem[MAX_INFLIGHT];
    VkFence inflight[MAX_INFLIGHT];
    VkResult r;
    uint32_t qfi = (uint32_t)-1, nimg = 0, i;
    VkExtent2D extent = { WIN_W, WIN_H };
    VkFormat surface_format = VK_FORMAT_B8G8R8A8_UNORM;
    int frame, presented = 0;

    (void)prev; (void)cmd;

    g_log = fopen( "C:\\vulkan_test.log", "w" );
    logf_line( "=== vulkan_test (32-bit guest) ===" );

    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"WineNxVulkanTest";
    wc.hCursor       = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW( &wc )) { logf_line( "RegisterClassW failed" ); return 1; }

    hwnd = CreateWindowExW( 0, L"WineNxVulkanTest", L"vulkan_test (32-bit)",
                            WS_POPUP | WS_VISIBLE, 0, 0, WIN_W, WIN_H,
                            NULL, NULL, inst, NULL );
    if (!hwnd) { logf_line( "CreateWindowExW failed" ); return 2; }
    ShowWindow( hwnd, show ? show : SW_SHOW );
    UpdateWindow( hwnd );

    vulkan_dll = LoadLibraryA( "vulkan-1.dll" );
    logf_line( "LoadLibraryA(vulkan-1.dll) -> %p", (void *)vulkan_dll );
    if (!vulkan_dll) goto done;

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress( vulkan_dll, "vkGetInstanceProcAddr" );
    logf_line( "GetProcAddress(vkGetInstanceProcAddr) -> %p", (void *)vkGetInstanceProcAddr );
    if (!vkGetInstanceProcAddr) goto done;

    {
        LOAD_INST( NULL, CreateInstance );
        const char *inst_exts[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
        VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };

        if (!CreateInstance) { logf_line( "no vkCreateInstance" ); goto done; }
        app.pApplicationName = "wine-nx vulkan_test";
        app.apiVersion = VK_API_VERSION_1_1;
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = 2;
        ici.ppEnabledExtensionNames = inst_exts;
        r = CreateInstance( &ici, NULL, &instance );
        logf_line( "vkCreateInstance -> %d", r );
        if (r != VK_SUCCESS) goto done;
    }

    {
        LOAD_INST( instance, CreateWin32SurfaceKHR );
        VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };

        if (!CreateWin32SurfaceKHR) { logf_line( "no vkCreateWin32SurfaceKHR" ); goto done; }
        sci.hinstance = inst;
        sci.hwnd = hwnd;
        r = CreateWin32SurfaceKHR( instance, &sci, NULL, &surface );
        logf_line( "vkCreateWin32SurfaceKHR -> %d", r );
        if (r != VK_SUCCESS) goto done;
    }

    {
        LOAD_INST( instance, EnumeratePhysicalDevices );
        LOAD_INST( instance, GetPhysicalDeviceProperties );
        LOAD_INST( instance, GetPhysicalDeviceQueueFamilyProperties );
        LOAD_INST( instance, GetPhysicalDeviceSurfaceSupportKHR );
        LOAD_INST( instance, GetPhysicalDeviceSurfaceCapabilitiesKHR );
        LOAD_INST( instance, GetPhysicalDeviceSurfaceFormatsKHR );
        LOAD_INST( instance, CreateDevice );
        VkPhysicalDevice devs[4];
        uint32_t ndev = 4, nqf, nfmt;
        VkQueueFamilyProperties qf[8];
        VkSurfaceCapabilitiesKHR caps;
        VkSurfaceFormatKHR fmts[16];
        VkPhysicalDeviceProperties props;

        if (!EnumeratePhysicalDevices || !CreateDevice) { logf_line( "missing instance entrypoints" ); goto done; }
        r = EnumeratePhysicalDevices( instance, &ndev, devs );
        logf_line( "vkEnumeratePhysicalDevices -> %d count=%u", r, ndev );
        if (r != VK_SUCCESS || ndev == 0) goto done;
        phys = devs[0];

        GetPhysicalDeviceProperties( phys, &props );
        logf_line( "device: %s", props.deviceName );

        nqf = 8;
        GetPhysicalDeviceQueueFamilyProperties( phys, &nqf, qf );
        for (i = 0; i < nqf; i++)
        {
            VkBool32 present_ok = VK_FALSE;
            if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            GetPhysicalDeviceSurfaceSupportKHR( phys, i, surface, &present_ok );
            if (present_ok) { qfi = i; break; }
        }
        logf_line( "graphics+present queue family: %d", (int)qfi );
        if (qfi == (uint32_t)-1) goto done;

        memset( &caps, 0, sizeof(caps) );
        r = GetPhysicalDeviceSurfaceCapabilitiesKHR( phys, surface, &caps );
        logf_line( "vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> %d", r );
        if (r == VK_SUCCESS && caps.currentExtent.width != 0xFFFFFFFFu) extent = caps.currentExtent;

        nfmt = 16;
        GetPhysicalDeviceSurfaceFormatsKHR( phys, surface, &nfmt, fmts );
        surface_format = nfmt ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
        logf_line( "surface %ux%u fmt=%d minImageCount=%u", extent.width, extent.height,
                   surface_format, caps.minImageCount );

        {
            float prio = 1.0f;
            const char *dev_exts[] = { "VK_KHR_swapchain" };
            VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

            qci.queueFamilyIndex = qfi;
            qci.queueCount = 1;
            qci.pQueuePriorities = &prio;
            dci.queueCreateInfoCount = 1;
            dci.pQueueCreateInfos = &qci;
            dci.enabledExtensionCount = 1;
            dci.ppEnabledExtensionNames = dev_exts;
            r = CreateDevice( phys, &dci, NULL, &dev );
            logf_line( "vkCreateDevice -> %d", r );
            if (r != VK_SUCCESS) goto done;
        }
    }

    vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr( instance, "vkGetDeviceProcAddr" );
    if (!vkGetDeviceProcAddr) { logf_line( "no vkGetDeviceProcAddr" ); goto done; }

    {
        LOAD_DEV( dev, GetDeviceQueue );
        GetDeviceQueue( dev, qfi, 0, &queue );
    }

    {
        LOAD_DEV( dev, CreateSwapchainKHR );
        LOAD_DEV( dev, GetSwapchainImagesKHR );
        VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };

        sci.surface = surface;
        sci.minImageCount = 2;
        sci.imageFormat = surface_format;
        sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        r = CreateSwapchainKHR( dev, &sci, NULL, &swapchain );
        logf_line( "vkCreateSwapchainKHR -> %d", r );
        if (r != VK_SUCCESS) goto done;

        nimg = 8;
        GetSwapchainImagesKHR( dev, swapchain, &nimg, images );
        logf_line( "swapchain images: %u", nimg );
    }

    {
        LOAD_DEV( dev, CreateCommandPool );
        LOAD_DEV( dev, AllocateCommandBuffers );
        VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };

        cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpi.queueFamilyIndex = qfi;
        if (CreateCommandPool( dev, &cpi, NULL, &cmd_pool ) != VK_SUCCESS) { logf_line( "FAIL: command pool" ); goto done; }

        ai.commandPool = cmd_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = MAX_INFLIGHT;
        if (AllocateCommandBuffers( dev, &ai, cmds ) != VK_SUCCESS) { logf_line( "FAIL: command buffers" ); goto done; }
    }

    {
        LOAD_DEV( dev, CreateSemaphore );
        LOAD_DEV( dev, CreateFence );
        VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (i = 0; i < MAX_INFLIGHT; i++)
        {
            CreateSemaphore( dev, &si, NULL, &acquire_sem[i] );
            CreateSemaphore( dev, &si, NULL, &present_sem[i] );
            CreateFence( dev, &fi, NULL, &inflight[i] );
        }
    }

    logf_line( "entering present loop" );
    {
        LOAD_DEV( dev, WaitForFences );
        LOAD_DEV( dev, ResetFences );
        LOAD_DEV( dev, AcquireNextImageKHR );
        LOAD_DEV( dev, ResetCommandBuffer );
        LOAD_DEV( dev, BeginCommandBuffer );
        LOAD_DEV( dev, EndCommandBuffer );
        LOAD_DEV( dev, CmdPipelineBarrier );
        LOAD_DEV( dev, CmdClearColorImage );
        LOAD_DEV( dev, QueueSubmit );
        LOAD_DEV( dev, QueuePresentKHR );
        LOAD_DEV( dev, DeviceWaitIdle );

        for (frame = 0; ; frame++)
        {
            int slot = frame % MAX_INFLIGHT;
            uint32_t idx = 0;
            VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VkImageMemoryBarrier to_dst = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            VkImageMemoryBarrier to_present = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            VkClearColorValue color;
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            float t = (float)frame / 60.0f;

            while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
            {
                if (msg.message == WM_QUIT) goto done;
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
            }

            WaitForFences( dev, 1, &inflight[slot], VK_TRUE, UINT64_MAX );

            r = AcquireNextImageKHR( dev, swapchain, UINT64_MAX, acquire_sem[slot], VK_NULL_HANDLE, &idx );
            if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
            {
                logf_line( "acquire frame %d -> %d", frame, r );
                if (r == VK_ERROR_OUT_OF_DATE_KHR) break;
                continue;
            }
            ResetFences( dev, 1, &inflight[slot] );
            ResetCommandBuffer( cmds[slot], 0 );

            color.float32[0] = 0.5f + 0.5f * (float)__builtin_sin( t );
            color.float32[1] = 0.5f + 0.5f * (float)__builtin_sin( t + 2.094 );
            color.float32[2] = 0.5f + 0.5f * (float)__builtin_sin( t + 4.188 );
            color.float32[3] = 1.0f;

            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            BeginCommandBuffer( cmds[slot], &bi );

            to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_dst.image = images[idx];
            to_dst.subresourceRange = range;
            CmdPipelineBarrier( cmds[slot], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, 0, NULL, 0, NULL, 1, &to_dst );

            CmdClearColorImage( cmds[slot], images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range );

            to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_present.image = images[idx];
            to_present.subresourceRange = range;
            CmdPipelineBarrier( cmds[slot], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                               0, 0, NULL, 0, NULL, 1, &to_present );

            EndCommandBuffer( cmds[slot] );

            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &acquire_sem[slot];
            submit.pWaitDstStageMask = &wait_stage;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmds[slot];
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &present_sem[slot];
            r = QueueSubmit( queue, 1, &submit, inflight[slot] );
            if (r != VK_SUCCESS) { logf_line( "submit frame %d -> %d", frame, r ); break; }

            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &present_sem[slot];
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain;
            pi.pImageIndices = &idx;
            r = QueuePresentKHR( queue, &pi );
            if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
            {
                logf_line( "present frame %d -> %d", frame, r );
                if (r == VK_ERROR_OUT_OF_DATE_KHR) break;
            }
            else
            {
                presented++;
                if (presented <= 5 || presented % 300 == 0)
                    logf_line( "presented frame %d (count=%d)", frame, presented );
            }
        }
        DeviceWaitIdle( dev );
    }

done:
    logf_line( "=== vulkan_test %s (presented %d frames) ===", presented > 0 ? "PASSED" : "FAILED", presented );
    if (g_log) fclose( g_log );
    return presented > 0 ? 0 : 1;
}
