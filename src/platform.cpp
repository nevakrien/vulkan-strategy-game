#define SDL_MAIN_HANDLED
#include "platform.hpp"
#include <vector>
#include <cstring>


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

    for (auto pd : devs) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());
        for (uint32_t i = 0; i < qcount; ++i) {
            VkBool32 present = VK_FALSE;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, g_vulkan.surface, &present));
            if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                g_vulkan.physical_device = pd;
                g_vulkan.queue_family = i;
                break;
            }
        }
        if (g_vulkan.physical_device) break;
    }
    if (!g_vulkan.physical_device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No suitable device with graphics+present");
        return false;
    }

    // -----------------------
    // Logical device + queue
    // -----------------------
    float qprio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_vulkan.queue_family;
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
    vkGetDeviceQueue(g_vulkan.device, g_vulkan.queue_family, 0, &g_vulkan.queue);

    return true;
}



void platform_shutdown() {
  if (g_vulkan.device)     { vkDeviceWaitIdle(g_vulkan.device); vkDestroyDevice(g_vulkan.device, nullptr); g_vulkan.device = VK_NULL_HANDLE; }
  if (g_vulkan.surface)    { vkDestroySurfaceKHR(g_vulkan.instance, g_vulkan.surface, nullptr); g_vulkan.surface = VK_NULL_HANDLE; }
  if (g_vulkan.instance)   { vkDestroyInstance(g_vulkan.instance, nullptr); g_vulkan.instance = VK_NULL_HANDLE; }
  if (g_window)            { SDL_DestroyWindow(g_window); g_window = nullptr; }
  SDL_Quit();
  SDL_Vulkan_UnloadLibrary();
}
