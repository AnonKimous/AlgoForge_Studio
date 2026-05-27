cmake_minimum_required(VERSION 3.20)

project(min_vulkan_win32 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)

include(FetchContent)

set(SDL3_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL-main" CACHE PATH "SDL3 source directory")
if(EXISTS "${SDL3_SOURCE_DIR}/CMakeLists.txt")
  message(STATUS "Using local SDL3 source at ${SDL3_SOURCE_DIR}")
  set(SDL_SHARED ON CACHE BOOL "Build shared SDL3" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "Build static SDL3" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3_test library" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "Build SDL test suite" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "Install SDL3" FORCE)
  add_subdirectory("${SDL3_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/sdl3-build" EXCLUDE_FROM_ALL)
else()
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    message(STATUS "Local SDL3 not found; fetching from GitHub")
    FetchContent_Declare(
      sdl3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.2.14
      GIT_SHALLOW TRUE
    )
    set(SDL_SHARED ON CACHE BOOL "Build shared SDL3" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Build static SDL3" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3_test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Build SDL test suite" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Install SDL3" FORCE)
    FetchContent_MakeAvailable(sdl3)
  else()
    message(STATUS "Using installed SDL3 package")
  endif()
endif()

set(EIGEN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen" CACHE PATH "Eigen source directory")
if(EXISTS "${EIGEN_SOURCE_DIR}/Eigen/Dense")
  message(STATUS "Using local Eigen source at ${EIGEN_SOURCE_DIR}")
  set(EIGEN_INCLUDE_DIR "${EIGEN_SOURCE_DIR}")
else()
  message(STATUS "Local Eigen not found; fetching from GitLab")
  FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(eigen)
  set(EIGEN_INCLUDE_DIR "${eigen_SOURCE_DIR}")
endif()

set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-master" CACHE PATH "Dear ImGui source directory")
set(IMGUI_HAS_DOCKING OFF)
if(EXISTS "${IMGUI_SOURCE_DIR}/imgui.h")
  file(READ "${IMGUI_SOURCE_DIR}/imgui.h" IMGUI_HEADER_CONTENTS)
  string(FIND "${IMGUI_HEADER_CONTENTS}" "DockSpaceOverViewport" IMGUI_DOCKING_API_INDEX)
  if(IMGUI_DOCKING_API_INDEX GREATER -1)
    set(IMGUI_HAS_DOCKING ON)
    message(STATUS "Using local Dear ImGui source at ${IMGUI_SOURCE_DIR}")
  endif()
endif()
if(NOT IMGUI_HAS_DOCKING)
  set(IMGUI_CACHED_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/imgui-src")
  if(EXISTS "${IMGUI_CACHED_SOURCE_DIR}/imgui.h")
    file(READ "${IMGUI_CACHED_SOURCE_DIR}/imgui.h" IMGUI_CACHED_HEADER_CONTENTS)
    string(FIND "${IMGUI_CACHED_HEADER_CONTENTS}" "DockSpaceOverViewport" IMGUI_CACHED_DOCKING_API_INDEX)
    if(IMGUI_CACHED_DOCKING_API_INDEX GREATER -1)
      set(IMGUI_HAS_DOCKING ON)
      set(IMGUI_SOURCE_DIR "${IMGUI_CACHED_SOURCE_DIR}" CACHE PATH "Dear ImGui source directory" FORCE)
      message(STATUS "Using cached Dear ImGui source at ${IMGUI_SOURCE_DIR}")
    endif()
  endif()
endif()
if(NOT IMGUI_HAS_DOCKING)
  message(STATUS "Local Dear ImGui lacks docking/viewports; fetching docking branch from GitHub")
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
  )
  FetchContent_Populate(imgui)
  set(IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}")
else()
  message(STATUS "Using local Dear ImGui source at ${IMGUI_SOURCE_DIR}")
endif()

set(VKB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vkb" CACHE PATH "VkBootstrap source directory")
set(USE_FETCHED_VKB OFF)
if(EXISTS "${VKB_SOURCE_DIR}/VkBootstrap.h")
  message(STATUS "Using local VkBootstrap header at ${VKB_SOURCE_DIR}")
else()
  message(STATUS "Local VkBootstrap not found; fetching from GitHub")
  set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "Build VkBootstrap tests" FORCE)
  get_target_property(VULKAN_INCLUDE_DIRS Vulkan::Vulkan INTERFACE_INCLUDE_DIRECTORIES)
  if(VULKAN_INCLUDE_DIRS)
    set(VK_BOOTSTRAP_VULKAN_HEADER_DIR "${VULKAN_INCLUDE_DIRS}" CACHE PATH "Vulkan headers directory" FORCE)
  endif()
  FetchContent_Declare(
    vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG main
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(vk_bootstrap)
  set(USE_FETCHED_VKB ON)
endif()

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_CMAKE_PATH "${SHADER_OUTPUT_DIR}/mesh.vert.spv" SHADER_VERT_PATH)
file(TO_CMAKE_PATH "${SHADER_OUTPUT_DIR}/mesh.frag.spv" SHADER_FRAG_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/data/subdivided_triangle.mesh" MESH_PATH)

add_custom_command(
  OUTPUT ${SHADER_OUTPUT_DIR}/mesh.vert.spv
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
  COMMAND ${GLSLC} -o ${SHADER_OUTPUT_DIR}/mesh.vert.spv ${SHADER_DIR}/mesh.vert
  DEPENDS ${SHADER_DIR}/mesh.vert
  VERBATIM
)

add_custom_command(
  OUTPUT ${SHADER_OUTPUT_DIR}/mesh.frag.spv
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
  COMMAND ${GLSLC} -o ${SHADER_OUTPUT_DIR}/mesh.frag.spv ${SHADER_DIR}/mesh.frag
  DEPENDS ${SHADER_DIR}/mesh.frag
  VERBATIM
)

add_custom_target(shaders DEPENDS
  ${SHADER_OUTPUT_DIR}/mesh.vert.spv
  ${SHADER_OUTPUT_DIR}/mesh.frag.spv
)

add_executable(min_vulkan_win32 WIN32
  src/main.cpp
  src/interaction/edit_mode_controller.cpp
  src/interaction/phys_mode_controller.cpp
  src/mesh.cpp
  src/physics/physics_solver.cpp
  src/validation_bridge/validation_bridge.cpp
  src/triangle_orientation_analyzer.cpp
  src/viewport_transform.cpp
  src/render/vulkan_renderer.cpp
  src/window/sdl_window.cpp
)

if(WIN32)
  target_sources(min_vulkan_win32 PRIVATE src/validation_layer/validation_layer.cpp)
else()
  target_sources(min_vulkan_win32 PRIVATE src/validation_layer/validation_layer_stub.cpp)
endif()

add_library(imgui_static STATIC
  ${IMGUI_SOURCE_DIR}/imgui.cpp
  ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
  ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
  ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui_static PUBLIC
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends
)

target_link_libraries(imgui_static PUBLIC Vulkan::Vulkan)
target_link_libraries(imgui_static PUBLIC SDL3::SDL3)

add_library(eigen_headers INTERFACE)
target_include_directories(eigen_headers INTERFACE
  ${EIGEN_INCLUDE_DIR}
)

add_dependencies(min_vulkan_win32 shaders)

if(USE_FETCHED_VKB)
  set(VKB_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_vkb_include")
  file(MAKE_DIRECTORY "${VKB_COMPAT_INCLUDE_DIR}/vkb")
  file(WRITE "${VKB_COMPAT_INCLUDE_DIR}/vkb/VkBootstrap.h" "#pragma once\n#include <VkBootstrap.h>\n")
  target_include_directories(min_vulkan_win32 PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated_vkb_include
  )
endif()

target_include_directories(min_vulkan_win32 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(min_vulkan_win32 PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  SHADER_VERT_PATH="${SHADER_VERT_PATH}"
  SHADER_FRAG_PATH="${SHADER_FRAG_PATH}"
  MESH_PATH="${MESH_PATH}"
)

target_link_libraries(min_vulkan_win32
  PRIVATE
    eigen_headers
    imgui_static
    SDL3::SDL3
    Vulkan::Vulkan
)

if(WIN32)
  target_compile_definitions(min_vulkan_win32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(min_vulkan_win32 PRIVATE advapi32 user32 gdi32 shell32)
endif()

if(USE_FETCHED_VKB)
  target_link_libraries(min_vulkan_win32 PRIVATE vk-bootstrap::vk-bootstrap)
endif()
