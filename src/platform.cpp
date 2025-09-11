#include "platform.hpp"

void print_libs(){
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
  vkEnumerateInstanceVersion(&ver);
    std::cout << "Vulkan loader present. Instance API version: "
              << VK_API_VERSION_MAJOR(ver) << "."
              << VK_API_VERSION_MINOR(ver) << "."
              << VK_API_VERSION_PATCH(ver) << "\n";
}