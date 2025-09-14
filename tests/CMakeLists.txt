# tests/CMakeLists.txt

# Put test executables under build/tests (and build/tests/<Config> for multi-config)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG        ${CMAKE_BINARY_DIR}/tests/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE      ${CMAKE_BINARY_DIR}/tests/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/tests/RelWithDebInfo)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL   ${CMAKE_BINARY_DIR}/tests/MinSizeRel)

# Directories with one-file-per-test
set(AUTO_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/auto_tests")
set(VIS_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/visual_tests")

# Collect *.c/*.C/*.cxx/*.cpp via aux_source_directory (more robust than GLOB here)
set(AUTO_SOURCES   "")
set(VISUAL_SOURCES "")

if(EXISTS "${AUTO_DIR}")
  aux_source_directory("${AUTO_DIR}" AUTO_SOURCES)
endif()

if(EXISTS "${VIS_DIR}")
  aux_source_directory("${VIS_DIR}" VISUAL_SOURCES)
endif()

set(TEST_TARGETS)

# --- Auto tests (run with ctest) ---
foreach(src IN LISTS AUTO_SOURCES)
  get_filename_component(t "${src}" NAME_WE)
  add_executable(${t} "${src}")
  target_link_libraries(${t} PRIVATE mygame_fullprofile)
  target_compile_features(${t} PRIVATE cxx_std_20)
  add_test(NAME ${t} COMMAND ${t})
  set_tests_properties(${t} PROPERTIES LABELS "auto" TIMEOUT 30)
  list(APPEND TEST_TARGETS ${t})
endforeach()

# --- Visual/manual tests (build-only) ---
foreach(src IN LISTS VISUAL_SOURCES)
  get_filename_component(base "${src}" NAME_WE)
  set(t "visual_${base}")
  add_executable(${t} "${src}")
  target_link_libraries(${t} PRIVATE mygame_fullprofile)
  target_compile_features(${t} PRIVATE cxx_std_20)
  list(APPEND TEST_TARGETS ${t})
endforeach()

# --- Aggregate target ---
if(TEST_TARGETS)
  add_custom_target(build_tests DEPENDS ${TEST_TARGETS})
else()
  add_custom_target(build_tests
    COMMAND ${CMAKE_COMMAND} -E echo "No tests discovered under tests/."
  )
endif()
