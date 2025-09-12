cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build third-party static
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build all sources under src/ (auto-updates on file changes)
file(GLOB_RECURSE MYGAME_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.c)
add_executable(mygame ${MYGAME_SOURCES})

# Let #include "foo.hpp" find headers in src/
target_include_directories(mygame PRIVATE src)

# Vulkan headers (include-only)
target_include_directories(mygame PRIVATE third_party/Vulkan-Headers/include)

# Link the system Vulkan loader (dynamic)
if(WIN32)
  target_link_libraries(mygame PRIVATE vulkan-1)
elseif(APPLE)
  # macOS: needs MoltenVK loader
  find_library(MOLTENVK MoltenVK)
  if(MOLTENVK)
    target_link_libraries(mygame PRIVATE ${MOLTENVK})
  endif()
elseif(ANDROID)
  # Android NDK ships libvulkan.so
  target_link_libraries(mygame PRIVATE vulkan)
elseif(UNIX)
  # Linux / BSD
  target_link_libraries(mygame PRIVATE vulkan)
endif()


# SDL2 (static) — fixed assumptions, no probing
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

# Disable everything except video/events
set(SDL_VIDEO  ON  CACHE BOOL "" FORCE)
set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
set(SDL_AUDIO  ON  CACHE BOOL "" FORCE)
set(SDL_EVENTS ON CACHE BOOL "" FORCE)

set(SDL_RENDER   OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC   OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR   OFF CACHE BOOL "" FORCE)
set(SDL_POWER    OFF CACHE BOOL "" FORCE)
set(SDL_LOCALE   OFF CACHE BOOL "" FORCE)

#add SDL
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
target_link_libraries(mygame PRIVATE SDL3::SDL3-static)

# FreeType: build static and trim optional shared deps
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Turn off optional dependencies that would add more shared libs
set(FT_DISABLE_ZLIB    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2   ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI  ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)

add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)

# Link the *static* target provided by the subdir build
target_link_libraries(mygame PRIVATE freetype)
target_include_directories(mygame PRIVATE third_party/freetype/include)


# Warnings (optional)
if(MSVC)
  target_compile_options(mygame PRIVATE /W4 /permissive-)
else()
  target_compile_options(mygame PRIVATE -Wall -Wextra -Wpedantic)
endif()
