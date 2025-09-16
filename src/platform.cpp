#define SDL_MAIN_HANDLED
#include "platform.hpp"
#include <vector>
#include <cstring>
#include <algorithm>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glslang/build_info.h>
#include <glslang/Public/ShaderLang.h>
#include <spirv-tools/libspirv.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>


//we need to check aginst os prefred gpu which isnt exposed by vulkan
#if defined(SDL_VIDEO_DRIVER_X11)
  #include <X11/Xlib.h>
  #include <X11/Xlib-xcb.h>
  #include <xcb/xcb.h>
#elif defined(_WIN32)
  #include <windows.h>    // LoadLibraryA, GetProcAddress
  #include <dxgi1_6.h>    // headers available at build time; runtime is probed dynamically
#endif

// GLOBALS
SDL_Window* g_window = nullptr;
int window_w = 0;
int window_h = 0;

VulkanGlobals g_vulkan;

static const char* kWindowTitle = "Vulkan Strategy Game";

FT_Library free_type;

bool platform_should_quit() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
        return true;
      case SDL_EVENT_KEY_DOWN:
        if (e.key.key == SDLK_ESCAPE) return true; // handy during bring-up
        break;
      default:
        break;
    }
  }
  return false;
}


void print_libs() {
  // SDL compile-time sanity
#if defined(SDL_MAJOR_VERSION)
  SDL_Log("SDL version (compile-time): %d.%d.%d",
       SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
#else
  SDL_Log("SDL version (compile-time): unknown");
#endif

  // FreeType
  SDL_Log("[FreeType] headers: %d.%d.%d",
            FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH);

  // Vulkan loader version
  {
    uint32_t ver = 0;
    if (vkEnumerateInstanceVersion(&ver) == VK_SUCCESS) {
      SDL_Log("Vulkan loader present. Instance API version: %u.%u.%u",
           VK_API_VERSION_MAJOR(ver),
           VK_API_VERSION_MINOR(ver),
           VK_API_VERSION_PATCH(ver));
    } else {
      SDL_Log("Vulkan loader not available or version query failed");
    }
  }

  //glsl
  {
    const char* opt_built =
    #if defined(ENABLE_OPT) || defined(ENABLE_SPIRV_OPT)
        "ON";
    #else
        "OFF";
    #endif

    #if defined(GLSLANG_VERSION_STRING)
        SDL_Log("[glslang] version: %s, optimizer built: %s", GLSLANG_VERSION_STRING, opt_built);
    #elif defined(GLSLANG_REVISION)
        SDL_Log("[glslang] revision: %s, optimizer built: %s", GLSLANG_REVISION, opt_built);
    #else
        auto ver = glslang::GetVersion();
        // New API; declared in ShaderLang.h in modern releases
        SDL_Log("[glslang] version: %u.%u.%u-%s, optimizer built: %s",
                ver.major,ver.minor,ver.patch,ver.flavor,
                opt_built);
    #endif

    // If SPIRV-Tools headers are visible, print its version string too.
    SDL_Log("[SPIRV-Tools] %s", spvSoftwareVersionString());
  }
}

//INIT SHUTDOWN

static bool pick_phisical_device(){
    uint32_t devCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(g_vulkan.instance, &devCount, nullptr));
    if (devCount == 0) {
        LOG_ERROR( "No Vulkan-capable device found");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(devCount);
    VK_CHECK(vkEnumeratePhysicalDevices(g_vulkan.instance, &devCount, devs.data()));

    g_vulkan.physical_device = VK_NULL_HANDLE;
    g_vulkan.graphics_family = UINT32_MAX;
    g_vulkan.present_family  = UINT32_MAX;


    // --- platform hint state (mutually exclusive) --------------------------------
    #if defined(SDL_VIDEO_DRIVER_X11)
    Display*          sel_dpy  = nullptr;
    xcb_connection_t* sel_conn = nullptr;
    xcb_visualid_t    sel_vis  = 0;
    {
        SDL_PropertiesID wp = SDL_GetWindowProperties(g_window);
        sel_dpy = (Display*)SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        if (sel_dpy) {
            sel_conn = XGetXCBConnection(sel_dpy);
            uint64_t visualid = SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_VISUALID_NUMBER, 0);
            sel_vis = (xcb_visualid_t)visualid;
        }
    }
    #elif defined(_WIN32)
    // Dynamically resolve dxgi.dll and CreateDXGIFactory1, then QI to IDXGIFactory6.
    // If anything fails, we just won't have an OS hint.
    LUID  os_pref_luid{}; bool have_os_pref_luid = false;
    {
        using PFN_CreateDXGIFactory1 = HRESULT (WINAPI*)(REFIID, void**);
        HMODULE hDXGI = GetModuleHandleA("dxgi.dll");
        if (!hDXGI) hDXGI = LoadLibraryA("dxgi.dll");
        if (hDXGI) {
            auto pCreateDXGIFactory1 =
                reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(hDXGI, "CreateDXGIFactory1"));
            if (pCreateDXGIFactory1) {
                IDXGIFactory1* f1 = nullptr;
                if (SUCCEEDED(pCreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f1)) && f1) {
                    IDXGIFactory6* f6 = nullptr;
                    if (SUCCEEDED(f1->QueryInterface(__uuidof(IDXGIFactory6), (void**)&f6)) && f6) {
                        IDXGIAdapter1* a = nullptr;
                        if (SUCCEEDED(f6->EnumAdapterByGpuPreference(
                                0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), (void**)&a)) && a) {
                            DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
                            os_pref_luid = d.AdapterLuid; have_os_pref_luid = true;
                            a->Release();
                        }
                        f6->Release();
                    }
                    f1->Release();
                }
            }
        }
    }
    #endif
    // -----------------------------------------------------------------------------

    // now we loop over avilable gpus
    // best-so-far according to (os_selected, unified, type_rank, score)
    bool     best_os_selected = false;
    bool     best_unified     = false;
    int      best_type_rank   = -1;   // 3: discrete, 2: integrated, 1: virtual, 0: other/CPU
    uint64_t best_score       = 0;

    // clear globals before scanning
    g_vulkan.physical_device = VK_NULL_HANDLE;
    g_vulkan.graphics_family = UINT32_MAX;
    g_vulkan.present_family  = UINT32_MAX;

    for (auto pd : devs) {
        VkPhysicalDeviceProperties device_props{};
        vkGetPhysicalDeviceProperties(pd, &device_props);
        LOG("considering physical device %s", device_props.deviceName);

        // --- require VK_KHR_swapchain
        uint32_t extCount = 0;
        VK_CHECK(vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr));
        std::vector<VkExtensionProperties> exts(extCount);
        VK_CHECK(vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, exts.data()));
        bool has_swapchain = false;
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { has_swapchain = true; break; }
        }
        if (!has_swapchain) continue;

        // --- find graphics & present families (present strengthened on X11)
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());

        uint32_t gfx = UINT32_MAX, pres = UINT32_MAX;
        for (uint32_t i = 0; i < qcount; ++i) {
            if (gfx == UINT32_MAX && (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                gfx = i;

            if (pres == UINT32_MAX) {
                VkBool32 canPresent = VK_FALSE;
                VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, g_vulkan.surface, &canPresent));
            #if defined(SDL_VIDEO_DRIVER_X11)
                if (canPresent && sel_conn && sel_vis) {
                    canPresent = vkGetPhysicalDeviceXcbPresentationSupportKHR(pd, i, sel_conn, sel_vis);
                }
            #endif
                if (canPresent) pres = i;
            }

            if (gfx != UINT32_MAX && pres != UINT32_MAX) break;
        }
        if (gfx == UINT32_MAX || pres == UINT32_MAX) continue;

        // --- try to unify: prefer one family that does both for THIS window
        bool unified = false;
        for (uint32_t i = 0; i < qcount; ++i) {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 sp = VK_FALSE;
                VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, g_vulkan.surface, &sp));
            #if defined(SDL_VIDEO_DRIVER_X11)
                if (sp && sel_conn && sel_vis) {
                    sp = vkGetPhysicalDeviceXcbPresentationSupportKHR(pd, i, sel_conn, sel_vis);
                }
            #endif
                if (sp) { gfx = pres = i; unified = true; break; }
            }
        }

        // --- surface must have at least one format & present mode
        uint32_t fmtCount = 0, pmCount = 0;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, g_vulkan.surface, &fmtCount, nullptr));
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(pd, g_vulkan.surface, &pmCount, nullptr));
        if (fmtCount == 0 || pmCount == 0) continue;

        // --- OS-selected (platform-specific)
        bool os_selected = false;
    #if defined(SDL_VIDEO_DRIVER_X11)
        os_selected = (sel_conn && sel_vis); // we verified present for THIS window’s connection/visual
    #elif defined(_WIN32)
        if (have_os_pref_luid) {
            VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2  p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            p2.pNext = &id;
            vkGetPhysicalDeviceProperties2(pd, &p2);
            if (id.deviceLUIDValid) {
                os_selected = (0 == std::memcmp(&os_pref_luid, &id.deviceLUID, sizeof(LUID)));
            }
        } else {
            os_selected = false;
        }
    #else
        os_selected = false;
    #endif

        // --- type rank (3: discrete, 2: integrated, 1: virtual, 0: other/CPU)
        int type_rank = 0;
        switch (device_props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   type_rank = 3; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: type_rank = 2; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    type_rank = 1; break;
            case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
            default:                                     type_rank = 0; break;
        }

        // --- capacity score = device-local VRAM (MB) × maxComputeWorkGroupInvocations
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(pd, &mp);
        uint64_t vramBytes = 0;
        for (uint32_t i = 0; i < mp.memoryHeapCount; ++i)
            if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                vramBytes += mp.memoryHeaps[i].size;

        // keep CPU/lavapipe/llvmpipe from “winning” via system RAM
        if (device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) vramBytes = 0;

        const uint64_t vramMB        = vramBytes >> 20;
        const uint64_t current_score = vramMB * device_props.limits.maxComputeWorkGroupInvocations;

        // --- choose best by (os_selected, unified, type_rank, score)
        const bool better =
            (os_selected != best_os_selected) ? os_selected :
            (unified     != best_unified    ) ? unified     :
            (type_rank   != best_type_rank  ) ? (type_rank > best_type_rank) :
            (current_score > best_score);

        if (better) {
            best_os_selected = os_selected;
            best_unified     = unified;
            best_type_rank   = type_rank;   // 3: discrete, 2: integrated, 1: virtual, 0: other/CPU
            best_score       = current_score;

            // assign directly to globals
            g_vulkan.physical_device = pd;
            g_vulkan.graphics_family = gfx;
            g_vulkan.present_family  = pres;

            // short-circuit on top-priority combo
            if (best_os_selected && best_unified) break;
        }
    }


    if (g_vulkan.physical_device == VK_NULL_HANDLE) {
        LOG_ERROR(
            "No suitable device: need graphics+present families, %s, and a valid surface format/present mode",
            VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        return false;
    }

    VkPhysicalDeviceProperties device_props{};
    vkGetPhysicalDeviceProperties(g_vulkan.physical_device, &device_props);
    LOG("found physical device %s (gfx qf=%u, present qf=%u)",
           device_props.deviceName, g_vulkan.graphics_family, g_vulkan.present_family);

    return true;

}

bool platform_init(uint32_t vulkan_version) {
    if (g_window) return true; // already init

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR( "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        LOG_ERROR( "SDL_Vulkan_LoadLibrary failed: %s", SDL_GetError());
        return false;
    }

    // Get the primary display's desktop mode
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display == 0) {
        LOG_ERROR( "SDL_GetPrimaryDisplay failed: %s", SDL_GetError());
        return false;
    }

    const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(display);
    if (!dm) {
        LOG_ERROR( "SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        return false;
    }

    // Create a fullscreen Vulkan window at the desktop resolution
    g_window = SDL_CreateWindow(
        kWindowTitle,
        dm->w, dm->h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    if (!g_window) {
        LOG_ERROR( "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    window_w = dm->w;
    window_h = dm->h;
    LOG("Fullscreen desktop %dx%d", window_w, window_h);

    // -------------------------
    // Vulkan Instance (SDL3)
    // -------------------------
    Uint32 extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    if (!sdlExts || extCount == 0) {
        LOG_ERROR( "SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
        return false;
    }
    std::vector<const char*> instExts(sdlExts, sdlExts + extCount); // copy into std::vector

    // Optional validation layer (Debug only, if installed)
    std::vector<const char*> instLayers;
#ifndef NDEBUG
    {
        uint32_t availCount = 0;
        VK_CHECK(vkEnumerateInstanceLayerProperties(&availCount, nullptr));
        std::vector<VkLayerProperties> avail(availCount);
        VK_CHECK(vkEnumerateInstanceLayerProperties(&availCount, avail.data()));
        const char* kValidation = "VK_LAYER_KHRONOS_validation";
        bool found = false;
        for (const auto& lp : avail) {
            if (std::strcmp(lp.layerName, kValidation) == 0) { found = true; break; }
        }
        if (found) instLayers.push_back(kValidation);
        else LOG("Validation layer not present; continuing without it.");
    }
#endif

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "mygame";
    app.applicationVersion = VK_MAKE_VERSION(0,0,1);
    app.pEngineName = "mygame";
    app.engineVersion = VK_MAKE_VERSION(0,0,1);
    app.apiVersion = vulkan_version;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(instExts.size());
    ici.ppEnabledExtensionNames = instExts.data();
    ici.enabledLayerCount = static_cast<uint32_t>(instLayers.size());
    ici.ppEnabledLayerNames = instLayers.empty() ? nullptr : instLayers.data();

    VK_CHECK(vkCreateInstance(&ici, nullptr, &g_vulkan.instance));

    // ---------
    // Surface
    // ---------
    if (!SDL_Vulkan_CreateSurface(g_window, g_vulkan.instance, /*allocator*/nullptr, &g_vulkan.surface)) {
        LOG_ERROR( "SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return false;
    }

    // -----------------------------------------
    // Pick physical device + queue (gfx+present)
    // -----------------------------------------
    if (!pick_phisical_device())
        return false;


    // -----------------------
    // Logical device + queue
    // -----------------------
    float qprio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_vulkan.present_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &qprio;

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;

    VK_CHECK(vkCreateDevice(g_vulkan.physical_device, &dci, nullptr, &g_vulkan.device));
    vkGetDeviceQueue(g_vulkan.device, g_vulkan.present_family, 0, &g_vulkan.present_queue);
    vkGetDeviceQueue(g_vulkan.device, g_vulkan.graphics_family, 0, &g_vulkan.graphics_queue);


    // --- Create swapchain (SDL3 + Vulkan) ---
{
    // 1) Query surface support
    VkSurfaceCapabilitiesKHR caps{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        g_vulkan.physical_device, g_vulkan.surface, &caps));

    uint32_t fmtCount = 0, pmCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
        g_vulkan.physical_device, g_vulkan.surface, &fmtCount, nullptr));
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        g_vulkan.physical_device, g_vulkan.surface, &pmCount, nullptr));

    if (fmtCount == 0 || pmCount == 0) {
        LOG_ERROR(
            "Swapchain unsupported: formats=%u presentModes=%u", fmtCount, pmCount);
        std::exit(1);
    }

    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
        g_vulkan.physical_device, g_vulkan.surface, &fmtCount, formats.data()));

    std::vector<VkPresentModeKHR> modes(pmCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        g_vulkan.physical_device, g_vulkan.surface, &pmCount, modes.data()));

    // 2) Choose surface format (prefer BGRA8 SRGB non-linear)
    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = f;
            break;
        }
    }

    // 3) Choose present mode (prefer MAILBOX, else FIFO)
    VkPresentModeKHR chosenMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosenMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }

    // 4) Choose extent
    VkExtent2D chosenExtent{};
    if (caps.currentExtent.width != UINT32_MAX) {
        // Surface dictates the size (common on most platforms)
        chosenExtent = caps.currentExtent;
    } else {
        int dw = 0, dh = 0;
        // SDL3: physical drawable size in pixels (handles HiDPI correctly)
        SDL_GetWindowSizeInPixels(g_window, &dw, &dh);
        chosenExtent.width  = std::clamp<uint32_t>(static_cast<uint32_t>(dw),
                                  caps.minImageExtent.width,  caps.maxImageExtent.width);
        chosenExtent.height = std::clamp<uint32_t>(static_cast<uint32_t>(dh),
                                  caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    // 5) Image count (min+1, clamped to max if present)
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    // 6) Create swapchain
    uint32_t qfs[2] = { g_vulkan.graphics_family, g_vulkan.present_family };

    VkSwapchainCreateInfoKHR sci{ };
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = g_vulkan.surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = chosenFormat.format;
    sci.imageColorSpace  = chosenFormat.colorSpace;
    sci.imageExtent      = chosenExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (g_vulkan.graphics_family != g_vulkan.present_family) {
        sci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices   = qfs;
    } else {
        sci.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    }

    sci.preTransform   = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode    = chosenMode;
    sci.clipped        = VK_TRUE;
    sci.oldSwapchain   = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(g_vulkan.device, &sci, nullptr, &g_vulkan.swapchain));

    // 7) Fetch images and stash format/extent
    uint32_t count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(g_vulkan.device, g_vulkan.swapchain, &count, nullptr));
    g_vulkan.swapchain_images.resize(count);
    VK_CHECK(vkGetSwapchainImagesKHR(
        g_vulkan.device, g_vulkan.swapchain, &count, g_vulkan.swapchain_images.data()));

    g_vulkan.swapchain_format = chosenFormat.format;
    g_vulkan.swapchain_extent = chosenExtent;

    g_vulkan.viewport = VkViewport{
        .x = 0.f,
        .y = 0.f,
        .width  = float(g_vulkan.swapchain_extent.width),
        .height = float(g_vulkan.swapchain_extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f
    };

    g_vulkan.scissor = VkRect2D{
        .offset = {0, 0},
        .extent = g_vulkan.swapchain_extent
    };


    g_vulkan.swapchain_image_views.resize(count);
    for (size_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = g_vulkan.swapchain_images[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = g_vulkan.swapchain_format;
        info.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
        };
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel   = 0;
        info.subresourceRange.levelCount     = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount     = 1;
        VK_CHECK(vkCreateImageView(g_vulkan.device, &info, nullptr, &g_vulkan.swapchain_image_views[i]));
    }

    LOG("Swapchain: %ux%u, %u images, fmt=%d, present=%d",
            chosenExtent.width, chosenExtent.height, count,
            (int)chosenFormat.format, (int)chosenMode);


    if (FT_Init_FreeType(&free_type) == 0) {
      LOG("FreeType init: OK");
    } else {
      LOG("FreeType init: FAILED");
      return false;
    }

    glslang::InitializeProcess();


}
    return true;
}



void platform_shutdown() {
  if (!g_window) return;
  if (g_vulkan.device)     { vkDeviceWaitIdle(g_vulkan.device);}
  if (g_vulkan.swapchain != VK_NULL_HANDLE) {
      for (auto iv : g_vulkan.swapchain_image_views) if (iv) vkDestroyImageView(g_vulkan.device, iv, nullptr);
      g_vulkan.swapchain_image_views.clear();

      vkDestroySwapchainKHR(g_vulkan.device, g_vulkan.swapchain, nullptr);
      g_vulkan.swapchain = VK_NULL_HANDLE;
      g_vulkan.swapchain_images.clear();
      g_vulkan.swapchain_format = VK_FORMAT_UNDEFINED;
      g_vulkan.swapchain_extent = {};

      glslang::FinalizeProcess();
  }

  if (g_vulkan.device)     { vkDestroyDevice(g_vulkan.device, nullptr); g_vulkan.device = VK_NULL_HANDLE; }
  if (g_vulkan.surface)    { SDL_Vulkan_DestroySurface(g_vulkan.instance, g_vulkan.surface, nullptr); g_vulkan.surface = VK_NULL_HANDLE; }
  if (g_vulkan.instance)   { vkDestroyInstance(g_vulkan.instance, nullptr); g_vulkan.instance = VK_NULL_HANDLE; }
  
  SDL_DestroyWindow(g_window); g_window = nullptr;
  SDL_Quit();
  SDL_Vulkan_UnloadLibrary();

  FT_Done_FreeType(free_type); 
}
