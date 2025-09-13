#define SDL_MAIN_HANDLED
#include "platform.hpp"
#include <vector>
#include <cstring>
#include <algorithm>


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

  // FreeType init
  FT_Library ft;
  if (FT_Init_FreeType(&ft) == 0) {
    SDL_Log("FreeType init: OK");
    FT_Done_FreeType(ft);
  } else {
    SDL_Log("FreeType init: FAILED");
  }

  // Vulkan loader version
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

// GLOBALS
SDL_Window* g_window = nullptr;
int window_w = 0;
int window_h = 0;

VulkanGlobals g_vulkan;

static const char* kWindowTitle = "Vulkan Strategy Game";

//INIT SHUTDOWN
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

bool platform_init() {
    if (g_window) return true; // already init

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Vulkan_LoadLibrary failed: %s", SDL_GetError());
        return false;
    }

    // Get the primary display's desktop mode
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    if (display == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetPrimaryDisplay failed: %s", SDL_GetError());
        return false;
    }

    const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(display);
    if (!dm) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        return false;
    }

    // Create a fullscreen Vulkan window at the desktop resolution
    g_window = SDL_CreateWindow(
        kWindowTitle,
        dm->w, dm->h,
        SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    if (!g_window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    window_w = dm->w;
    window_h = dm->h;
    SDL_Log("Fullscreen desktop %dx%d", window_w, window_h);

    // -------------------------
    // Vulkan Instance (SDL3)
    // -------------------------
    Uint32 extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    if (!sdlExts || extCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
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
        else SDL_Log("Validation layer not present; continuing without it.");
    }
#endif

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = nullptr;
    ici.enabledExtensionCount = static_cast<uint32_t>(instExts.size());
    ici.ppEnabledExtensionNames = instExts.data();
    ici.enabledLayerCount = static_cast<uint32_t>(instLayers.size());
    ici.ppEnabledLayerNames = instLayers.empty() ? nullptr : instLayers.data();

    VK_CHECK(vkCreateInstance(&ici, nullptr, &g_vulkan.instance));

    // ---------
    // Surface
    // ---------
    if (!SDL_Vulkan_CreateSurface(g_window, g_vulkan.instance, /*allocator*/nullptr, &g_vulkan.surface)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return false;
    }

    // -----------------------------------------
    // Pick physical device + queue (gfx+present)
    // -----------------------------------------
    uint32_t devCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(g_vulkan.instance, &devCount, nullptr));
    if (devCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No Vulkan-capable device found");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(devCount);
    VK_CHECK(vkEnumeratePhysicalDevices(g_vulkan.instance, &devCount, devs.data()));

    g_vulkan.physical_device = VK_NULL_HANDLE;
    g_vulkan.graphics_family = UINT32_MAX;
    g_vulkan.present_family  = UINT32_MAX;

    for (auto pd : devs) {
        // --- find graphics & present families (can be different) ---
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
                if (canPresent) pres = i;
            }

            if (gfx != UINT32_MAX && pres != UINT32_MAX) break;
        }
        if (gfx == UINT32_MAX || pres == UINT32_MAX) continue;

        // --- require VK_KHR_swapchain ---
        uint32_t extCount = 0;
        VK_CHECK(vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr));
        std::vector<VkExtensionProperties> exts(extCount);
        VK_CHECK(vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, exts.data()));
        bool has_swapchain = false;
        for (const auto& e : exts) {
            if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) { has_swapchain = true; break; }
        }
        if (!has_swapchain) continue;

        // --- surface must have at least one format & present mode ---
        uint32_t fmtCount = 0, pmCount = 0;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, g_vulkan.surface, &fmtCount, nullptr));
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(pd, g_vulkan.surface, &pmCount, nullptr));
        if (fmtCount == 0 || pmCount == 0) continue;

        // success: commit and stop on the first valid device
        g_vulkan.physical_device = pd;
        g_vulkan.graphics_family = gfx;
        g_vulkan.present_family  = pres;
        break;
    }

    if (g_vulkan.physical_device == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "No suitable device: need graphics+present families, %s, and a valid surface format/present mode",
            VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        return false;
    }

    VkPhysicalDeviceProperties device_props{};
    vkGetPhysicalDeviceProperties(g_vulkan.physical_device, &device_props);
    SDL_Log("found physical device %s (gfx qf=%u, present qf=%u)",
           device_props.deviceName, g_vulkan.graphics_family, g_vulkan.present_family);



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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
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

    SDL_Log("Swapchain: %ux%u, %u images, fmt=%d, present=%d",
            chosenExtent.width, chosenExtent.height, count,
            (int)chosenFormat.format, (int)chosenMode);
}

// //comands
// {
//   VkCommandPoolCreateInfo pool_info{};
//     pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
//     pool_info.queueFamilyIndex = g_vulkan.graphics_family;
//     // RESET flag lets you individually reset command buffers if needed
//     pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

//    VK_CHECK(vkCreateCommandPool(g_vulkan.device, &pool_info, nullptr, &g_vulkan.command_pool));

//     g_vulkan.command_buffers.resize(g_vulkan.framebuffers.size());


//     VkCommandBufferAllocateInfo ai{};
//     ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
//     ai.commandPool        = g_vulkan.command_pool;          // created earlier
//     ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//     ai.commandBufferCount = static_cast<uint32_t>(g_vulkan.command_buffers.size());

//     VK_CHECK(vkAllocateCommandBuffers(g_vulkan.device, &ai, g_vulkan.command_buffers.data()));

// }
// (next steps elsewhere: create image views per swapchain image, depth buffer, framebuffers, etc.)


    return true;
}



void platform_shutdown() {
  if (g_vulkan.device)     { vkDeviceWaitIdle(g_vulkan.device);}
  if (g_vulkan.swapchain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(g_vulkan.device, g_vulkan.swapchain, nullptr);
      g_vulkan.swapchain = VK_NULL_HANDLE;
      g_vulkan.swapchain_images.clear();
      g_vulkan.swapchain_format = VK_FORMAT_UNDEFINED;
      g_vulkan.swapchain_extent = {};
  }

  // if (g_vulkan.command_pool != VK_NULL_HANDLE) {
  //     vkDestroyCommandPool(g_vulkan.device, g_vulkan.command_pool, nullptr);
  //     g_vulkan.command_pool = VK_NULL_HANDLE;
  //      g_vulkan.command_buffers.clear();
  // }

  if (g_vulkan.device)     { vkDestroyDevice(g_vulkan.device, nullptr); g_vulkan.device = VK_NULL_HANDLE; }
  if (g_vulkan.surface)    { SDL_Vulkan_DestroySurface(g_vulkan.instance, g_vulkan.surface, nullptr); g_vulkan.surface = VK_NULL_HANDLE; }
  if (g_vulkan.instance)   { vkDestroyInstance(g_vulkan.instance, nullptr); g_vulkan.instance = VK_NULL_HANDLE; }
  if (g_window)            { SDL_DestroyWindow(g_window); g_window = nullptr; }

  SDL_Quit();
  SDL_Vulkan_UnloadLibrary();
}
