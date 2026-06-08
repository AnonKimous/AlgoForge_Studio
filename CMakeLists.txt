cmake_minimum_required(VERSION 3.20)

project(min_vulkan_win32 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

function(require_sdl3_artifacts)
  if(CMAKE_CONFIGURATION_TYPES)
    return()
  endif()

  if(NOT CMAKE_BUILD_TYPE)
    message(FATAL_ERROR "CMAKE_BUILD_TYPE must be set for single-config generators.")
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    require_file("${SDL3_LIBRARY_DEBUG}" "Provide the SDL3 Debug library for Debug builds.")
    require_file("${SDL3_DLL_DEBUG}" "Provide the SDL3 Debug runtime DLL for Debug builds.")
  elseif(CMAKE_BUILD_TYPE STREQUAL "Release" OR
         CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR
         CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    require_file("${SDL3_LIBRARY_RELEASE}" "Provide the SDL3 Release library for non-Debug builds.")
    require_file("${SDL3_DLL_RELEASE}" "Provide the SDL3 Release runtime DLL for non-Debug builds.")
  else()
    message(FATAL_ERROR "Unsupported CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
  endif()
endfunction()

require_file("${SDL3_INCLUDE_DIR}/SDL3/SDL.h" "Set SDL3_INCLUDE_DIR or prepare dependencies under ${DEPS_ROOT}.")
require_sdl3_artifacts()
require_file("${EIGEN_INCLUDE_DIR}/Eigen/Dense" "Set EIGEN_INCLUDE_DIR to a valid Eigen root.")
require_file("${IMGUI_SOURCE_DIR}/imgui.h" "Set IMGUI_SOURCE_DIR to a valid Dear ImGui source root.")
require_file("${VKB_SOURCE_DIR}/src/VkBootstrap.h" "Set VKB_SOURCE_DIR to a valid VkBootstrap source root.")
require_file("${VKB_SOURCE_DIR}/src/VkBootstrap.cpp" "Set VKB_SOURCE_DIR to a valid VkBootstrap source root.")
require_file("${VMA_INCLUDE_DIR}/vk_mem_alloc.h" "Set VMA_INCLUDE_DIR to a valid VMA include directory.")
require_file("${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.h" "Set SPIRV_REFLECT_SOURCE_DIR to a valid SPIRV-Reflect source root.")
require_file("${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.c" "Set SPIRV_REFLECT_SOURCE_DIR to a valid SPIRV-Reflect source root.")
require_file("${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyobjloader/tiny_obj_loader.h" "Pull tinyobjloader into third_party/tinyobjloader.")
require_file("${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyobjloader/tiny_obj_loader.cc" "Pull tinyobjloader into third_party/tinyobjloader.")

set(CMAKE_HELPER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_cmake")
file(MAKE_DIRECTORY "${CMAKE_HELPER_DIR}")
file(WRITE "${CMAKE_HELPER_DIR}/require_generated_file.cmake"
[[if(NOT DEFINED REQUIRED_FILE)
  message(FATAL_ERROR "REQUIRED_FILE was not provided to require_generated_file.cmake")
endif()
if(NOT EXISTS "${REQUIRED_FILE}")
  message(FATAL_ERROR "Missing required dependency file: ${REQUIRED_FILE}\nHint: ${HINT_TEXT}")
endif()
]])

set(VKB_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_vkb_include")
file(MAKE_DIRECTORY "${VKB_COMPAT_INCLUDE_DIR}/vkb")
file(WRITE "${VKB_COMPAT_INCLUDE_DIR}/vkb/VkBootstrap.h" "#pragma once\n#include <VkBootstrap.h>\n")

set(PROJECT_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
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
  optimized "${SDL3_LIBRARY_RELEASE}"
)

add_library(common_data STATIC
  src/common_data/mesh.cpp
)
target_include_directories(common_data PUBLIC ${PROJECT_INCLUDE_DIRS})

add_library(mesh_io STATIC
  src/capabilities/sidecar/mesh_io.cpp
  third_party/tinyobjloader/tiny_obj_loader.cc
)
target_include_directories(mesh_io PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(mesh_io PUBLIC common_data)

add_library(cjson STATIC
  third_party/cjson/cJSON.c
)
target_include_directories(cjson PUBLIC ${PROJECT_INCLUDE_DIRS})

add_library(codec STATIC
  src/codec/codec_manager.cpp
  src/codec/algorithm_plugin_loader.cpp
  src/codec/codec_decomposer.cpp
  src/codec/codec_reflector.cpp
  src/codec/codec_intervention.cpp
  src/codec/codecset_loader.cpp
)
target_include_directories(codec PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(codec PRIVATE
  ALGORITHM_LIBRARY_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library"
)
target_link_libraries(codec PUBLIC common_data PRIVATE algorithm_management cjson)

add_library(temporary_test_line_motion_plugin MODULE
  src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_plugin.cpp
)
target_include_directories(temporary_test_line_motion_plugin PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(temporary_test_line_motion_plugin PRIVATE
  ALGORITHM_LIBRARY_PLUGIN_BUILD=1
  ALGORITHM_LIBRARY_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library"
)
target_link_libraries(temporary_test_line_motion_plugin PRIVATE algorithm_management cjson common_data)
set_target_properties(temporary_test_line_motion_plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "temporary_test_line_motion"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion"
)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library" ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT)

add_library(algorithm_management STATIC
  src/algorithm_management/algorithm_container_manifest.cpp
  ${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.c
)
target_include_directories(algorithm_management PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(algorithm_management PRIVATE
  ALGORITHM_MANAGEMENT_LAYER_INTERNAL_BUILD=1
  ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT="${ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT}"
)
target_link_libraries(algorithm_management PUBLIC common_data Vulkan::Vulkan cjson)

add_library(runtime_systems STATIC
  src/runtime_systems/window/sdl_window.cpp
  src/runtime_systems/render/vma_impl.cpp
  src/runtime_systems/render/preview_renderer.cpp
  src/runtime_systems/render/imgui_vulkan_runtime.cpp
  src/runtime_systems/runtime_environment.cpp
  ${VKB_SOURCE_DIR}/src/VkBootstrap.cpp
  ${IMGUI_SOURCE_DIR}/imgui.cpp
  ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
  ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
  ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(runtime_systems PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(runtime_systems PRIVATE RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD=1)
target_link_libraries(runtime_systems PUBLIC common_data Vulkan::Vulkan ${SDL3_LINK_LIBS})

add_library(agent STATIC
  src/capabilities/agent/agent.cpp
)
target_include_directories(agent PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(agent PUBLIC common_data algorithm_management codec)

add_library(agent_management STATIC
  src/agent_management/agent_ticker.cpp
  src/agent_management/agent_manager.cpp
)
target_include_directories(agent_management PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(agent_management PRIVATE AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD=1)
target_link_libraries(agent_management PUBLIC common_data agent)

add_library(interact_ui STATIC
  src/interact_ui/interact_ui_panel.cpp
  src/interact_ui/interact_ui_runtime.cpp
)
target_include_directories(interact_ui PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(interact_ui PRIVATE
  PROJECT_DATA_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
target_link_libraries(interact_ui PUBLIC agent_management runtime_systems codec)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_CMAKE_PATH "${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv" SHADER_PHYSICS_CONV_PATH)

add_custom_command(
  OUTPUT ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
  COMMAND ${GLSLC} -o ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv ${SHADER_DIR}/physics_convolution.comp
  DEPENDS ${SHADER_DIR}/physics_convolution.comp
  VERBATIM
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.vert.spv
  COMMAND ${GLSLC} -o ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.vert
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.vert
  VERBATIM
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.frag.spv
  COMMAND ${GLSLC} -o ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.frag
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.frag
  VERBATIM
)

add_custom_target(shaders DEPENDS
  ${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv
  ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.vert.spv
  ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library/temporary_test_line_motion/temporary_test_line_motion_result_render.frag.spv
)

add_executable(min_vulkan_win32 WIN32
  src/app_orchestration/main.cpp
)

add_dependencies(min_vulkan_win32 shaders)
add_dependencies(min_vulkan_win32 temporary_test_line_motion_plugin)

target_link_libraries(min_vulkan_win32 PRIVATE
  interact_ui
  common_data
)

target_compile_definitions(min_vulkan_win32 PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  SHADER_PHYSICS_CONV_PATH="${SHADER_PHYSICS_CONV_PATH}"
)

if(WIN32)
  target_link_libraries(min_vulkan_win32 PRIVATE advapi32 user32 gdi32 shell32)

  add_custom_command(TARGET min_vulkan_win32 PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DREQUIRED_FILE=$<IF:$<CONFIG:Debug>,${SDL3_LIBRARY_DEBUG},${SDL3_LIBRARY_RELEASE}>"
      "-DHINT_TEXT=$<IF:$<CONFIG:Debug>,Provide the SDL3 Debug library for Debug builds.,Provide the SDL3 Release library for non-Debug builds.>"
      -P "${CMAKE_HELPER_DIR}/require_generated_file.cmake"
    COMMAND ${CMAKE_COMMAND}
      "-DREQUIRED_FILE=$<IF:$<CONFIG:Debug>,${SDL3_DLL_DEBUG},${SDL3_DLL_RELEASE}>"
      "-DHINT_TEXT=$<IF:$<CONFIG:Debug>,Provide the SDL3 Debug runtime DLL for Debug builds.,Provide the SDL3 Release runtime DLL for non-Debug builds.>"
      -P "${CMAKE_HELPER_DIR}/require_generated_file.cmake"
  )

  add_custom_command(TARGET min_vulkan_win32 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<IF:$<CONFIG:Debug>,${SDL3_DLL_DEBUG},${SDL3_DLL_RELEASE}>"
    "$<TARGET_FILE_DIR:min_vulkan_win32>"
  )
endif()
