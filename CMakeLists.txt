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

set(DEPS_ROOT "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "Root path for third-party sources and binaries")

set(SDL3_INCLUDE_DIR "${DEPS_ROOT}/sdl3-src/include" CACHE PATH "SDL3 include directory")
set(SDL3_LIBRARY_DEBUG "${DEPS_ROOT}/sdl3-build/Debug/SDL3.lib" CACHE FILEPATH "SDL3 debug import/static library")
set(SDL3_LIBRARY_RELEASE "${DEPS_ROOT}/sdl3-build/Release/SDL3.lib" CACHE FILEPATH "SDL3 release import/static library")
set(SDL3_DLL_DEBUG "${DEPS_ROOT}/sdl3-build/Debug/SDL3.dll" CACHE FILEPATH "SDL3 debug runtime DLL")
set(SDL3_DLL_RELEASE "${DEPS_ROOT}/sdl3-build/Release/SDL3.dll" CACHE FILEPATH "SDL3 release runtime DLL")

set(EIGEN_INCLUDE_DIR "${DEPS_ROOT}/eigen-src" CACHE PATH "Eigen include directory")
set(IMGUI_SOURCE_DIR "${DEPS_ROOT}/imgui-src" CACHE PATH "Dear ImGui source directory")
set(VKB_SOURCE_DIR "${DEPS_ROOT}/vk_bootstrap-src" CACHE PATH "VkBootstrap source directory")
set(VMA_INCLUDE_DIR "${DEPS_ROOT}/vma-src/include" CACHE PATH "VMA include directory")
set(SPIRV_REFLECT_SOURCE_DIR "${DEPS_ROOT}/spirv_reflect-src" CACHE PATH "SPIRV-Reflect source directory")

function(require_file path hint)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing required dependency file: ${path}\nHint: ${hint}")
  endif()
endfunction()

require_file("${SDL3_INCLUDE_DIR}/SDL3/SDL.h" "Set SDL3_INCLUDE_DIR or prepare dependencies under ${DEPS_ROOT}.")
require_file("${SDL3_LIBRARY_DEBUG}" "Set SDL3_LIBRARY_DEBUG to a valid SDL3 import/static library.")
require_file("${EIGEN_INCLUDE_DIR}/Eigen/Dense" "Set EIGEN_INCLUDE_DIR to a valid Eigen root.")
require_file("${IMGUI_SOURCE_DIR}/imgui.h" "Set IMGUI_SOURCE_DIR to a valid Dear ImGui source root.")
require_file("${VKB_SOURCE_DIR}/src/VkBootstrap.h" "Set VKB_SOURCE_DIR to a valid VkBootstrap source root.")
require_file("${VKB_SOURCE_DIR}/src/VkBootstrap.cpp" "Set VKB_SOURCE_DIR to a valid VkBootstrap source root.")
require_file("${VMA_INCLUDE_DIR}/vk_mem_alloc.h" "Set VMA_INCLUDE_DIR to a valid VMA include directory.")
require_file("${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.h" "Set SPIRV_REFLECT_SOURCE_DIR to a valid SPIRV-Reflect source root.")
require_file("${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.c" "Set SPIRV_REFLECT_SOURCE_DIR to a valid SPIRV-Reflect source root.")

set(SDL3_EFFECTIVE_LIBRARY_RELEASE "${SDL3_LIBRARY_RELEASE}")
set(SDL3_EFFECTIVE_DLL_RELEASE "${SDL3_DLL_RELEASE}")
if(NOT EXISTS "${SDL3_EFFECTIVE_LIBRARY_RELEASE}")
  set(SDL3_EFFECTIVE_LIBRARY_RELEASE "${SDL3_LIBRARY_DEBUG}")
endif()
if(NOT EXISTS "${SDL3_EFFECTIVE_DLL_RELEASE}")
  set(SDL3_EFFECTIVE_DLL_RELEASE "${SDL3_DLL_DEBUG}")
endif()

set(VKB_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_vkb_include")
file(MAKE_DIRECTORY "${VKB_COMPAT_INCLUDE_DIR}/vkb")
file(WRITE "${VKB_COMPAT_INCLUDE_DIR}/vkb/VkBootstrap.h" "#pragma once\n#include <VkBootstrap.h>\n")

set(PROJECT_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson
  ${SDL3_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${VMA_INCLUDE_DIR}
  ${SPIRV_REFLECT_SOURCE_DIR}
  ${VKB_SOURCE_DIR}/src
  ${VKB_COMPAT_INCLUDE_DIR}
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends
)

set(SDL3_LINK_LIBS
  debug "${SDL3_LIBRARY_DEBUG}"
  optimized "${SDL3_EFFECTIVE_LIBRARY_RELEASE}"
)

add_library(common_data STATIC
  src/common_data/mesh.cpp
  src/common_data/viewport_transform.cpp
)
target_include_directories(common_data PUBLIC ${PROJECT_INCLUDE_DIRS})

add_library(messaging STATIC
  src/messaging/io_bus.cpp
)
target_include_directories(messaging PUBLIC ${PROJECT_INCLUDE_DIRS})

add_library(cjson STATIC
  third_party/cjson/cJSON.c
)
target_include_directories(cjson PUBLIC ${PROJECT_INCLUDE_DIRS})

add_library(codec STATIC
  src/codec/codec_manager.cpp
)
target_include_directories(codec PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(codec PUBLIC common_data messaging)

add_library(algorithm STATIC
  src/algorithm_library/cpu_physics_algorithm.cpp
  src/algorithm_library/gpu_physics_algorithm.cpp
  src/algorithm_library/algorithm_mng.cpp
  src/algorithm_library/corotated_cpu_algorithm_contract.cpp
  src/algorithm_library/physics_convolution_gpu_algorithm_contract.cpp
  src/algorithm/algorithm_container_manifest.cpp
  ${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.c
)
target_include_directories(algorithm PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(algorithm PUBLIC common_data Vulkan::Vulkan cjson)

add_library(runtime_systems STATIC
  src/runtime_systems/window/sdl_window.cpp
  src/runtime_systems/render/vma_impl.cpp
  src/runtime_systems/render/imgui_vulkan_runtime.cpp
  ${VKB_SOURCE_DIR}/src/VkBootstrap.cpp
  ${IMGUI_SOURCE_DIR}/imgui.cpp
  ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
  ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
  ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(runtime_systems PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(runtime_systems PUBLIC common_data Vulkan::Vulkan ${SDL3_LINK_LIBS})

add_library(agent STATIC
  src/agent/agent.cpp
)
target_include_directories(agent PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(agent PUBLIC common_data algorithm codec)

add_library(agent_execute STATIC
  src/agents/agents.cpp
  src/agents/algorithm_pool.cpp
  src/agents/physics_agent.cpp
  src/agent_execute/agent_execute_runtime.cpp
)
target_include_directories(agent_execute PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(agent_execute PUBLIC common_data runtime_systems codec algorithm agent)

add_library(interact_ui STATIC
  src/interact_ui/interact_ui_runtime.cpp
)
target_include_directories(interact_ui PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(interact_ui PUBLIC agent_execute)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_CMAKE_PATH "${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv" SHADER_PHYSICS_CONV_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/data/subdivided_triangle.mesh" MESH_PATH)

add_custom_command(
  OUTPUT ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
  COMMAND ${GLSLC} -o ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv ${SHADER_DIR}/physics_convolution.comp
  DEPENDS ${SHADER_DIR}/physics_convolution.comp
  VERBATIM
)

add_custom_target(shaders DEPENDS
  ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv
)

add_executable(min_vulkan_win32 WIN32
  src/app_orchestration/main.cpp
)

add_dependencies(min_vulkan_win32 shaders)

target_include_directories(min_vulkan_win32 PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(min_vulkan_win32 PRIVATE
  agent_execute
  interact_ui
  runtime_systems
  codec
  algorithm
  messaging
  common_data
  Vulkan::Vulkan
  ${SDL3_LINK_LIBS}
)

target_compile_definitions(min_vulkan_win32 PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  SHADER_PHYSICS_CONV_PATH="${SHADER_PHYSICS_CONV_PATH}"
  MESH_PATH="${MESH_PATH}"
)

if(WIN32)
  target_link_libraries(min_vulkan_win32 PRIVATE advapi32 user32 gdi32 shell32)

  add_custom_command(TARGET min_vulkan_win32 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<IF:$<CONFIG:Debug>,${SDL3_DLL_DEBUG},${SDL3_EFFECTIVE_DLL_RELEASE}>"
    "$<TARGET_FILE_DIR:min_vulkan_win32>"
  )
endif()
