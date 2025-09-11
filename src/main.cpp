#include <iostream>

// SDL
#if __has_include(<SDL3/SDL.h>)
  #include <SDL3/SDL.h>
#elif __has_include(<SDL.h>)
  #include <SDL.h>
#else
  #error "SDL headers not found"
#endif

// FreeType
#include <ft2build.h>
#include FT_FREETYPE_H

// Vulkan headers
#include <vulkan/vulkan.h>

int main() {
  std::cout << "hello, world 👋\n";

  // SDL compile-time sanity
  std::cout << "SDL version (compile-time): ";
  #if defined(SDL_MAJOR_VERSION)
    std::cout << SDL_MAJOR_VERSION << "." << SDL_MINOR_VERSION << "." << SDL_PATCHLEVEL << "\n";
  #else
    std::cout << "unknown\n";
  #endif

  // FreeType init
  FT_Library ft;
  if (FT_Init_FreeType(&ft) == 0) {
    std::cout << "FreeType init: OK\n";
    FT_Done_FreeType(ft);
  } else {
    std::cout << "FreeType init: FAILED\n";
  }

  // Vulkan: query loader version if available
  uint32_t ver = 0;
  if (vkEnumerateInstanceVersion) {
    vkEnumerateInstanceVersion(&ver);
    std::cout << "Vulkan loader present. Instance API version: "
              << VK_API_VERSION_MAJOR(ver) << "."
              << VK_API_VERSION_MINOR(ver) << "."
              << VK_API_VERSION_PATCH(ver) << "\n";
  } else {
    std::cout << "Vulkan loader not available (need system runtime).\n";
  }

  return 0;
}
