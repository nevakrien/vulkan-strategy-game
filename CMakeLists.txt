cmake_minimum_required(VERSION 3.20)
project(mygame LANGUAGES C CXX)

# --------------- Standard and Flags -----------
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

# Export your own headers to dependents; pull third-party includes from their targets.
target_include_directories(mygame_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
  PRIVATE
    #sdl3
    $<TARGET_PROPERTY:SDL3::SDL3-static,INTERFACE_INCLUDE_DIRECTORIES>
    
    #freetype
    $<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>

    #vulkan
    $<TARGET_PROPERTY:Vulkan::Headers,INTERFACE_INCLUDE_DIRECTORIES>

    #glsl
    $<TARGET_PROPERTY:glslang::glslang,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:glslang::SPIRV,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:glslang::glslang-default-resource-limits,INTERFACE_INCLUDE_DIRECTORIES>
)

# ---------------- Link profile: one target to rule them all ----------------
# Export the exact same includes & libs for app and tests.
add_library(mygame_fullprofile INTERFACE)


# Libraries everyone links with (transitively)
target_link_libraries(mygame_fullprofile INTERFACE
  mygame_core
  SDL3::SDL3-static
  freetype
  Vulkan::Vulkan

  glslang::glslang
  glslang::SPIRV
  glslang::glslang-default-resource-limits
)

# Ensure vendored headers are seen BEFORE any system Vulkan headers
target_include_directories(mygame_fullprofile
  INTERFACE
    BEFORE
    third_party/Vulkan-Headers/include
)


# ---------------- Third-party: SDL3 (static, trimmed) ----------------
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


# ---------------- Vulkan (normalize to a target) ----------------
add_subdirectory(third_party/Vulkan-Headers EXCLUDE_FROM_ALL)

# Prefer a config/package if present; otherwise make an imported shim.
find_package(Vulkan QUIET)  # defines Vulkan::Vulkan on many setups

if(NOT TARGET Vulkan::Vulkan)
  add_library(Vulkan::Vulkan INTERFACE IMPORTED)
  if(WIN32)
    target_link_libraries(Vulkan::Vulkan INTERFACE vulkan-1)
  elseif(APPLE)
    find_library(MOLTENVK MoltenVK REQUIRED)
    target_link_libraries(Vulkan::Vulkan INTERFACE "${MOLTENVK}")
  elseif(ANDROID)
    target_link_libraries(Vulkan::Vulkan INTERFACE vulkan)
  else() # Linux/*nix
    target_link_libraries(Vulkan::Vulkan INTERFACE vulkan)
  endif()
endif()

# ---------------- Third-party: glslang (static, trimmed) ----------------
set(ENABLE_SPIRV       ON  CACHE BOOL "" FORCE)
set(ENABLE_HLSL        OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS  OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/glslang EXCLUDE_FROM_ALL)


# ---------------- App executable: single main.cpp at repo root ----------------
add_executable(mygame ${CMAKE_SOURCE_DIR}/main.cpp)
target_link_libraries(mygame PRIVATE mygame_fullprofile)

# ---------------- Tests  ----------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
endif()

# FLAGS
if(MSVC)
  target_compile_options(mygame_core PRIVATE /W4 /permissive-)
  target_compile_options(mygame      PRIVATE /W4 /permissive-)
else()
  target_compile_options(mygame_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(mygame      PRIVATE -Wall -Wextra -Wpedantic)
endif()