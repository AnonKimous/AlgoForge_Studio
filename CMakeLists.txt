cmake_minimum_required(VERSION 3.20)

project(min_vulkan_win32 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
  if(MSVC)
    message(STATUS "Windows: MSVC is the recommended toolchain for this project.")
  else()
    message(WARNING "Windows: MSVC is the recommended toolchain for this project, but continuing with the current compiler.")
  endif()
endif()

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)
include(FetchContent)

set(SDL3_REQUIRED_VERSION "3.5.0")
set(EIGEN_REQUIRED_VERSION "3.4.0")
set(IMGUI_REQUIRED_VERSION_NUM "19280")
set(IMGUI_REQUIRED_TAG "v1.92.8-docking")
set(VKB_REQUIRED_TAG "v1.4.352")
set(VMA_REQUIRED_TAG "v3.3.0")
set(SPIRV_REFLECT_REQUIRED_TAG "vulkan-sdk-1.4.350.0")

set(SDL3_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL-main")
set(SDL3_USE_LOCAL OFF)
if(EXISTS "${SDL3_SOURCE_DIR}/CMakeLists.txt")
  file(READ "${SDL3_SOURCE_DIR}/CMakeLists.txt" SDL3_CMAKE_CONTENTS)
  string(FIND "${SDL3_CMAKE_CONTENTS}" "VERSION \"${SDL3_REQUIRED_VERSION}\"" SDL3_VERSION_INDEX)
  if(SDL3_VERSION_INDEX GREATER -1)
    set(SDL3_USE_LOCAL ON)
  endif()
endif()
if(SDL3_USE_LOCAL)
  message(STATUS "Using local SDL3 source at ${SDL3_SOURCE_DIR}")
  set(SDL_SHARED ON CACHE BOOL "Build shared SDL3" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "Build static SDL3" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3_test library" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "Build SDL test suite" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "Install SDL3" FORCE)
  add_subdirectory("${SDL3_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/sdl3-build" EXCLUDE_FROM_ALL)
else()
  message(STATUS "Using SDL3 ${SDL3_REQUIRED_VERSION} from GitHub")
  FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-${SDL3_REQUIRED_VERSION}
    GIT_SHALLOW TRUE
  )
  set(SDL_SHARED ON CACHE BOOL "Build shared SDL3" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "Build static SDL3" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3_test library" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "Build SDL test suite" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "Install SDL3" FORCE)
  FetchContent_MakeAvailable(sdl3)
endif()

set(EIGEN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen")
if(EXISTS "${EIGEN_SOURCE_DIR}/Eigen/Dense")
  message(STATUS "Using local Eigen source at ${EIGEN_SOURCE_DIR}")
  set(EIGEN_INCLUDE_DIR "${EIGEN_SOURCE_DIR}")
else()
  message(STATUS "Using Eigen ${EIGEN_REQUIRED_VERSION} release archive from GitLab")
  FetchContent_Declare(
    eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/${EIGEN_REQUIRED_VERSION}/eigen-${EIGEN_REQUIRED_VERSION}.tar.gz
  )
  FetchContent_Populate(eigen)
  set(EIGEN_INCLUDE_DIR "${eigen_SOURCE_DIR}")
endif()

set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-1.92.8-docking")
set(IMGUI_USE_LOCAL OFF)
if(EXISTS "${IMGUI_SOURCE_DIR}/imgui.h")
  file(READ "${IMGUI_SOURCE_DIR}/imgui.h" IMGUI_HEADER_CONTENTS)
  string(REGEX MATCH "#define[ \t]+IMGUI_VERSION_NUM[ \t]+([0-9]+)" IMGUI_VERSION_MATCH "${IMGUI_HEADER_CONTENTS}")
  string(FIND "${IMGUI_HEADER_CONTENTS}" "DockSpaceOverViewport" IMGUI_DOCKING_API_INDEX)
  if(CMAKE_MATCH_1 STREQUAL "${IMGUI_REQUIRED_VERSION_NUM}" AND IMGUI_DOCKING_API_INDEX GREATER -1)
    set(IMGUI_USE_LOCAL ON)
  endif()
endif()
if(IMGUI_USE_LOCAL)
  message(STATUS "Using local Dear ImGui source at ${IMGUI_SOURCE_DIR}")
else()
  message(STATUS "Using Dear ImGui ${IMGUI_REQUIRED_TAG} from GitHub")
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG ${IMGUI_REQUIRED_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(imgui)
  set(IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}")
endif()

set(VKB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vkb")
if(EXISTS "${VKB_SOURCE_DIR}/VkBootstrap.h")
  message(STATUS "Using local VkBootstrap header at ${VKB_SOURCE_DIR}")
else()
  message(STATUS "Using VkBootstrap ${VKB_REQUIRED_TAG} from GitHub")
  set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "Build VkBootstrap tests" FORCE)
  get_target_property(VULKAN_INCLUDE_DIRS Vulkan::Vulkan INTERFACE_INCLUDE_DIRECTORIES)
  if(VULKAN_INCLUDE_DIRS)
    set(VK_BOOTSTRAP_VULKAN_HEADER_DIR "${VULKAN_INCLUDE_DIRS}" CACHE PATH "Vulkan headers directory" FORCE)
  endif()
  FetchContent_Declare(
    vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG ${VKB_REQUIRED_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(vk_bootstrap)
endif()

set(VMA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator-3.3.0")
if(EXISTS "${VMA_SOURCE_DIR}/include/vk_mem_alloc.h")
  message(STATUS "Using local VulkanMemoryAllocator source at ${VMA_SOURCE_DIR}")
else()
  message(STATUS "Using VulkanMemoryAllocator ${VMA_REQUIRED_TAG} from GitHub")
  FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG ${VMA_REQUIRED_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_Populate(vma)
  set(VMA_SOURCE_DIR "${vma_SOURCE_DIR}")
endif()

set(SPIRV_REFLECT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Reflect-vulkan-sdk-1.4.350.0")
if(EXISTS "${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.h" AND EXISTS "${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.c")
  message(STATUS "Using local SPIRV-Reflect source at ${SPIRV_REFLECT_SOURCE_DIR}")
else()
  message(STATUS "Using SPIRV-Reflect ${SPIRV_REFLECT_REQUIRED_TAG} from GitHub")
  FetchContent_Declare(
    spirv_reflect
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
    GIT_TAG ${SPIRV_REFLECT_REQUIRED_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_Populate(spirv_reflect)
  set(SPIRV_REFLECT_SOURCE_DIR "${spirv_reflect_SOURCE_DIR}")
endif()

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_CMAKE_PATH "${SHADER_OUTPUT_DIR}/mesh.vert.spv" SHADER_VERT_PATH)
file(TO_CMAKE_PATH "${SHADER_OUTPUT_DIR}/mesh.frag.spv" SHADER_FRAG_PATH)
file(TO_CMAKE_PATH "${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv" SHADER_PHYSICS_CONV_PATH)
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

add_custom_command(
  OUTPUT ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
  COMMAND ${GLSLC} -o ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv ${SHADER_DIR}/physics_convolution.comp
  DEPENDS ${SHADER_DIR}/physics_convolution.comp
  VERBATIM
)

add_custom_target(shaders DEPENDS
  ${SHADER_OUTPUT_DIR}/mesh.vert.spv
  ${SHADER_OUTPUT_DIR}/mesh.frag.spv
  ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv
)

add_executable(min_vulkan_win32 WIN32
  src/main.cpp
  src/app/module_agents.cpp
  src/algorithm/cpu_job_scheduler.cpp
  src/algorithm/cpu_physics_algorithm.cpp
  src/algorithm/data_reflection.cpp
  src/algorithm/gpu_physics_algorithm.cpp
  src/communication/io_bus.cpp
  src/communication/io_protocol.cpp
  src/glue/app_frame_sync_glue.cpp
  src/glue/guide_ui_frame_relay_on_phys_manager_buffers.cpp
  src/algorithm/legacy_corotated_cpu_algorithm_contract.cpp
  src/algorithm/physics_algorithm_pipeline.cpp
  src/algorithm/physics_convolution_gpu_algorithm_contract.cpp
  src/algorithm/triangle_orientation_cpu_algorithm.cpp
  src/algorithm/triangle_orientation_cpu_algorithm_contract.cpp
  src/interaction/guide_ui_controller.cpp
  src/interaction/edit_mode_controller.cpp
  src/interaction/phys_mode_controller.cpp
  src/mesh.cpp
  src/physics/phys_manager.cpp
  src/physics/phys_manager_buffer_reflectors.cpp
  src/validation_bridge/validation_bridge.cpp
  src/viewport_transform.cpp
  src/render/vma_impl.cpp
  src/render/scene_camera.cpp
  src/render/vulkan_renderer.cpp
  src/window/sdl_window.cpp
)

if(WIN32)
  target_sources(min_vulkan_win32 PRIVATE src/validation_layer/validation_layer.cpp)
else()
  target_sources(min_vulkan_win32 PRIVATE src/validation_layer/validation_layer_stub.cpp)
endif()

target_sources(min_vulkan_win32 PRIVATE
  src/validation_layer/validation_script.cpp
)

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

add_library(vma_headers INTERFACE)
target_include_directories(vma_headers INTERFACE
  ${VMA_SOURCE_DIR}/include
)

add_library(spirv_reflect_static STATIC
  ${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.c
)
target_include_directories(spirv_reflect_static PUBLIC
  ${SPIRV_REFLECT_SOURCE_DIR}
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
  SHADER_PHYSICS_CONV_PATH="${SHADER_PHYSICS_CONV_PATH}"
  MESH_PATH="${MESH_PATH}"
)

target_link_libraries(min_vulkan_win32
  PRIVATE
    eigen_headers
    imgui_static
    spirv_reflect_static
    SDL3::SDL3
    Vulkan::Vulkan
    vma_headers
)

if(WIN32)
  target_compile_definitions(min_vulkan_win32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(min_vulkan_win32 PRIVATE advapi32 user32 gdi32 shell32)
endif()

if(USE_FETCHED_VKB)
  target_link_libraries(min_vulkan_win32 PRIVATE vk-bootstrap::vk-bootstrap)
endif()
