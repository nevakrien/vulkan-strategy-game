cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------- Core library (everything under src/, NO main) ----------------
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/*.cpp
  ${CMAKE_SOURCE_DIR}/src/*.c
)
add_library(mygame_core STATIC ${CORE_SOURCES})

# Your own public headers live in src/
target_include_directories(mygame_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

# ---------------- Third-party: SDL3 (static) ----------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

# Trim what you don't need (tune as you like)
set(SDL_VIDEO   ON  CACHE BOOL "" FORCE)
set(SDL_HIDAPI  ON  CACHE BOOL "" FORCE)
set(SDL_AUDIO   ON  CACHE BOOL "" FORCE)
set(SDL_EVENTS  ON  CACHE BOOL "" FORCE)
set(SDL_RENDER  OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC   OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR   OFF CACHE BOOL "" FORCE)
set(SDL_POWER    OFF CACHE BOOL "" FORCE)
set(SDL_LOCALE   OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/SDL EXCLUDE_FROM_ALL)

# ---------------- Third-party: FreeType (static, trimmed) ----------------
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/freetype EXCLUDE_FROM_ALL)

# ---------------- Vulkan headers (include-only) ----------------
set(VK_HEADERS_DIR ${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include)

# ---------------- Link profile: one target to rule them all ----------------
# Export the exact same includes & libs for app and tests.
add_library(mygame_fullprofile INTERFACE)

# Includes everyone needs (headers your code #includes directly)
target_include_directories(mygame_fullprofile INTERFACE
  ${CMAKE_SOURCE_DIR}/src
  ${VK_HEADERS_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/freetype/include
)

# Platform Vulkan loader choice
set(VULKAN_LOADER "")
if(WIN32)
  set(VULKAN_LOADER vulkan-1)
elseif(APPLE)
  find_library(MOLTENVK MoltenVK)
  if(MOLTENVK)
    set(VULKAN_LOADER ${MOLTENVK})
  endif()
elseif(ANDROID)
  set(VULKAN_LOADER vulkan)
elseif(UNIX)
  set(VULKAN_LOADER vulkan)
endif()


# Libraries everyone links with (transitively)
target_link_libraries(mygame_fullprofile INTERFACE
  mygame_core
  SDL3::SDL3-static
  freetype
  ${VULKAN_LOADER}
)

target_include_directories(mygame_core PRIVATE
  ${VK_HEADERS_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/freetype/include
  ${CMAKE_SOURCE_DIR}/third_party/SDL/include
)

# ---------------- App executable: single main.cpp at repo root ----------------
add_executable(mygame ${CMAKE_SOURCE_DIR}/main.cpp)
target_link_libraries(mygame PRIVATE mygame_fullprofile)

# ---------------- Warnings ----------------
if(MSVC)
  target_compile_options(mygame_core PRIVATE /W4 /permissive-)
  target_compile_options(mygame      PRIVATE /W4 /permissive-)
else()
  target_compile_options(mygame_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(mygame      PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------- Tests  ----------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
endif()
