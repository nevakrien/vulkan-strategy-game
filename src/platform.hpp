#ifndef PLATFORM_HPP
#define PLATFORM_HPP

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

void print_libs();

#endif //PLATFORM_HPP
