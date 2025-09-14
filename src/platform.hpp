#ifndef PLATFORM_HPP
#define PLATFORM_HPP

#include <vector>
#include <cstdio>
#include <cstdlib>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define LOG_ERROR(format, ...) \
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, format, ##__VA_ARGS__)

#ifndef NDEBUG
#define LOG(format, ...) \
    SDL_Log(format, ##__VA_ARGS__)
#else
#define LOG(format, ...) \
    do { } while(0)
#endif

// -----------------
// Globals
// -----------------
extern SDL_Window* g_window;
extern int window_w;
extern int window_h;

struct VulkanGlobals {
    // Core instance/device objects
    VkInstance        instance        = VK_NULL_HANDLE;
    VkPhysicalDevice  physical_device = VK_NULL_HANDLE;
    VkDevice          device          = VK_NULL_HANDLE;

    // Queue family indices
    uint32_t          graphics_family = 0;
    uint32_t          present_family  = 0;

    // Queues
    VkQueue           graphics_queue  = VK_NULL_HANDLE;
    VkQueue           present_queue   = VK_NULL_HANDLE;

    // Surface
    VkSurfaceKHR      surface         = VK_NULL_HANDLE;

    // Swapchain
    VkSwapchainKHR              swapchain        = VK_NULL_HANDLE;
    std::vector<VkImage>        swapchain_images;
    std::vector<VkImageView>    swapchain_image_views;
    VkFormat                    swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D                  swapchain_extent = {};
};


extern VulkanGlobals g_vulkan;

extern FT_Library free_type;

// -----------------
// API
// -----------------
void print_libs();

// Initializes SDL (fullscreen desktop) and Vulkan (instance, surface, device,
// swapchain, image views). Uses a constant window title.
bool platform_init(uint32_t vulkan_version = VK_API_VERSION_1_0);

// Destroys all resources.
void platform_shutdown();

// Poll events; returns true if quit requested.
bool platform_should_quit();

// Human-readable VkResult (minimal)
static const char* vk_result_str(VkResult r) {
  switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    default: return "VK_ERROR_<unknown>";
  }
}

// Low-level check that prints file:line and the expression
static inline void vk_check(VkResult r, const char* expr, const char* file, int line) {
  if (r != VK_SUCCESS) {
    SDL_Log("Vulkan call failed: %s -> %s (%d) at %s:%d", expr, vk_result_str(r), (int)r, file, line);
    std::abort();
  }
}

// Macro wrapper so calls capture file/line automatically
#define VK_CHECK(EXPR) vk_check((EXPR), #EXPR, __FILE__, __LINE__)


#endif // PLATFORM_HPP
