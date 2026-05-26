cmake_minimum_required(VERSION 3.20)

project(min_vulkan_win32 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)

include(FetchContent)

set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-master" CACHE PATH "Dear ImGui source directory")
if(EXISTS "${IMGUI_SOURCE_DIR}/imgui.h")
  message(STATUS "Using local Dear ImGui source at ${IMGUI_SOURCE_DIR}")
else()
  message(STATUS "Local Dear ImGui not found; fetching from GitHub")
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
    GIT_SHALLOW TRUE
  )
  FetchContent_Populate(imgui)
  set(IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}")
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
  src/triangle_orientation_analyzer.cpp
  src/viewport_transform.cpp
  src/render/vulkan_renderer.cpp
  src/win32_window.cpp
)

add_library(imgui_static STATIC
  ${IMGUI_SOURCE_DIR}/imgui.cpp
  ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
  ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
  ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui_static PUBLIC
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends
)

target_compile_definitions(imgui_static PUBLIC
  VK_USE_PLATFORM_WIN32_KHR
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_link_libraries(imgui_static PUBLIC Vulkan::Vulkan)

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
  VK_USE_PLATFORM_WIN32_KHR
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  SHADER_VERT_PATH="${SHADER_VERT_PATH}"
  SHADER_FRAG_PATH="${SHADER_FRAG_PATH}"
  MESH_PATH="${MESH_PATH}"
)

target_link_libraries(min_vulkan_win32
  PRIVATE
    imgui_static
    Vulkan::Vulkan
    user32
    gdi32
)

if(USE_FETCHED_VKB)
  target_link_libraries(min_vulkan_win32 PRIVATE vk-bootstrap::vk-bootstrap)
endif()
