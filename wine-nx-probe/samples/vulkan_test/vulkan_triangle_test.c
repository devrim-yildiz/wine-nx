/*
 * 32-bit (i386) Windows Vulkan triangle test: real render pass, graphics
 * pipeline, and vkCmdDraw -- one step up from vulkan_test.c's bare
 * clear-only present loop, exercising shader modules, a pipeline layout, a
 * framebuffer per swapchain image, and dynamic viewport/scissor state.
 *
 * Bring-up (window, instance, WSI surface, device, swapchain) is copied
 * unchanged from vulkan_test.c -- already hardware-proven on 2026-07-22
 * (300 frames presented through the real Wine Vulkan driver chain). The
 * pipeline configuration itself (empty vertex input, TRIANGLE_LIST, no
 * depth test, no blend, dynamic viewport+scissor) is copied from
 * wine-nx-probe/third_party/nxvk/switch/smoke/nvk_gfx.h's
 * nvk_graphics_pipeline(), already hardware-proven on this same NVK build
 * via nvk_tri.c (offscreen target instead of a swapchain, but the pipeline
 * itself doesn't care).
 *
 * Shaders are tri.vert (hardcoded triangle position + color via
 * gl_VertexIndex, no vertex buffer) + solid.frag from
 * third_party/nxvk/switch/smoke/shaders/, pre-compiled to SPIR-V via
 * gen-shaders.sh and included here as plain uint32_t arrays -- no runtime
 * shader compiler needed on the guest.
 */
#include <windows.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <stdio.h>

#include "tri_vert.h"
#include "solid_frag.h"

#define WIN_W 640
#define WIN_H 480
#define MAX_INFLIGHT 2
#define MAX_SWAPCHAIN_IMAGES 8

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
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkImage images[MAX_SWAPCHAIN_IMAGES];
    VkImageView views[MAX_SWAPCHAIN_IMAGES];
    VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
    VkCommandBuffer cmds[MAX_INFLIGHT];
    VkSemaphore acquire_sem[MAX_INFLIGHT], present_sem[MAX_INFLIGHT];
    VkFence inflight[MAX_INFLIGHT];
    VkResult r;
    uint32_t qfi = (uint32_t)-1, nimg = 0, i;
    VkExtent2D extent = { WIN_W, WIN_H };
    VkFormat surface_format = VK_FORMAT_B8G8R8A8_UNORM;
    int frame, presented = 0;

    (void)prev; (void)cmd;

    g_log = fopen( "C:\\vulkan_triangle_test.log", "w" );
    logf_line( "=== vulkan_triangle_test (32-bit guest) ===" );

    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"WineNxVulkanTriangleTest";
    wc.hCursor       = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW( &wc )) { logf_line( "RegisterClassW failed" ); return 1; }

    hwnd = CreateWindowExW( 0, L"WineNxVulkanTriangleTest", L"vulkan_triangle_test (32-bit)",
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
        app.pApplicationName = "wine-nx vulkan_triangle_test";
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
        LOAD_DEV( dev, CreateImageView );
        VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };

        sci.surface = surface;
        sci.minImageCount = 2;
        sci.imageFormat = surface_format;
        sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        r = CreateSwapchainKHR( dev, &sci, NULL, &swapchain );
        logf_line( "vkCreateSwapchainKHR -> %d", r );
        if (r != VK_SUCCESS) goto done;

        nimg = MAX_SWAPCHAIN_IMAGES;
        GetSwapchainImagesKHR( dev, swapchain, &nimg, images );
        logf_line( "swapchain images: %u", nimg );

        for (i = 0; i < nimg; i++)
        {
            VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vci.image = images[i];
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = surface_format;
            vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vci.subresourceRange.levelCount = 1;
            vci.subresourceRange.layerCount = 1;
            r = CreateImageView( dev, &vci, NULL, &views[i] );
            if (r != VK_SUCCESS) { logf_line( "FAIL: image view %u -> %d", i, r ); goto done; }
        }
    }

    {
        LOAD_DEV( dev, CreateRenderPass );
        VkAttachmentDescription att = {0};
        VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub = {0};
        VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };

        att.format = surface_format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color_ref;

        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;

        r = CreateRenderPass( dev, &rpci, NULL, &render_pass );
        logf_line( "vkCreateRenderPass -> %d", r );
        if (r != VK_SUCCESS) goto done;
    }

    {
        LOAD_DEV( dev, CreateFramebuffer );
        for (i = 0; i < nimg; i++)
        {
            VkFramebufferCreateInfo fbci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fbci.renderPass = render_pass;
            fbci.attachmentCount = 1;
            fbci.pAttachments = &views[i];
            fbci.width = extent.width;
            fbci.height = extent.height;
            fbci.layers = 1;
            r = CreateFramebuffer( dev, &fbci, NULL, &framebuffers[i] );
            if (r != VK_SUCCESS) { logf_line( "FAIL: framebuffer %u -> %d", i, r ); goto done; }
        }
        logf_line( "framebuffers created: %u", nimg );
    }

    {
        LOAD_DEV( dev, CreateShaderModule );
        LOAD_DEV( dev, CreatePipelineLayout );
        LOAD_DEV( dev, CreateGraphicsPipelines );
        VkShaderModuleCreateInfo vsci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        VkShaderModuleCreateInfo fsci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };

        vsci.codeSize = sizeof(tri_vert_spv);
        vsci.pCode = tri_vert_spv;
        r = CreateShaderModule( dev, &vsci, NULL, &vs );
        logf_line( "vertex shader module -> %d", r );
        if (r != VK_SUCCESS) goto done;

        fsci.codeSize = sizeof(solid_frag_spv);
        fsci.pCode = solid_frag_spv;
        r = CreateShaderModule( dev, &fsci, NULL, &fs );
        logf_line( "fragment shader module -> %d", r );
        if (r != VK_SUCCESS) goto done;

        r = CreatePipelineLayout( dev, &plci, NULL, &pipeline_layout );
        logf_line( "pipeline layout -> %d", r );
        if (r != VK_SUCCESS) goto done;

        {
            VkPipelineShaderStageCreateInfo stages[2] = {
                { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
                { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
            };
            VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
            VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
            VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
            VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
            VkPipelineColorBlendAttachmentState cba = {0};
            VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
            VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs;
            stages[0].pName = "main";
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs;
            stages[1].pName = "main";

            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            vp.viewportCount = 1;
            vp.scissorCount = 1;

            rs.polygonMode = VK_POLYGON_MODE_FILL;
            rs.cullMode = VK_CULL_MODE_NONE;
            rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rs.lineWidth = 1.0f;

            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cb.attachmentCount = 1;
            cb.pAttachments = &cba;

            dyn.dynamicStateCount = 2;
            dyn.pDynamicStates = dyn_states;

            gpci.stageCount = 2;
            gpci.pStages = stages;
            gpci.pVertexInputState = &vi;
            gpci.pInputAssemblyState = &ia;
            gpci.pViewportState = &vp;
            gpci.pRasterizationState = &rs;
            gpci.pMultisampleState = &ms;
            gpci.pColorBlendState = &cb;
            gpci.pDynamicState = &dyn;
            gpci.layout = pipeline_layout;
            gpci.renderPass = render_pass;
            gpci.subpass = 0;

            r = CreateGraphicsPipelines( dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline );
            logf_line( "graphics pipeline -> %d", r );
            if (r != VK_SUCCESS) goto done;
        }
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
        LOAD_DEV( dev, CmdBeginRenderPass );
        LOAD_DEV( dev, CmdEndRenderPass );
        LOAD_DEV( dev, CmdBindPipeline );
        LOAD_DEV( dev, CmdSetViewport );
        LOAD_DEV( dev, CmdSetScissor );
        LOAD_DEV( dev, CmdDraw );
        LOAD_DEV( dev, QueueSubmit );
        LOAD_DEV( dev, QueuePresentKHR );
        LOAD_DEV( dev, DeviceWaitIdle );

        for (frame = 0; ; frame++)
        {
            int slot = frame % MAX_INFLIGHT;
            uint32_t idx = 0;
            VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VkClearValue clear;
            VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            VkViewport viewport = { 0, 0, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
            VkRect2D scissor = { { 0, 0 }, extent };
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

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

            clear.color.float32[0] = 0.05f;
            clear.color.float32[1] = 0.05f;
            clear.color.float32[2] = 0.08f;
            clear.color.float32[3] = 1.0f;

            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            BeginCommandBuffer( cmds[slot], &bi );

            rpbi.renderPass = render_pass;
            rpbi.framebuffer = framebuffers[idx];
            rpbi.renderArea.extent = extent;
            rpbi.clearValueCount = 1;
            rpbi.pClearValues = &clear;
            CmdBeginRenderPass( cmds[slot], &rpbi, VK_SUBPASS_CONTENTS_INLINE );

            CmdSetViewport( cmds[slot], 0, 1, &viewport );
            CmdSetScissor( cmds[slot], 0, 1, &scissor );
            CmdBindPipeline( cmds[slot], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
            CmdDraw( cmds[slot], 3, 1, 0, 0 );

            CmdEndRenderPass( cmds[slot] );
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
    logf_line( "=== vulkan_triangle_test %s (presented %d frames) ===", presented > 0 ? "PASSED" : "FAILED", presented );
    if (g_log) fclose( g_log );
    return presented > 0 ? 0 : 1;
}
