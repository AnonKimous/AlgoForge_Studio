cmake_minimum_required(VERSION 3.20)

project(algorithm_packages LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32)
  if(MSVC)
    message(STATUS "Windows: MSVC is the recommended toolchain for this project.")
    add_compile_options("/utf-8")
  else()
    message(WARNING "Windows: MSVC is the recommended toolchain for this project, but continuing with the current compiler.")
  endif()
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)

set(SDL3_INCLUDE_DIR "${REPO_ROOT}/third_party/SDL-main/include" CACHE PATH "SDL3 include directory")
set(SDL3_LIBRARY_DEBUG "D:/VulkanSDK/1.4.341.1/Lib/SDL3.lib" CACHE FILEPATH "SDL3 debug import/static library")
set(SDL3_LIBRARY_RELEASE "D:/VulkanSDK/1.4.341.1/Lib/SDL3.lib" CACHE FILEPATH "SDL3 release import/static library")
set(EIGEN_INCLUDE_DIR "${REPO_ROOT}/third_party/eigen" CACHE PATH "Eigen include directory")
set(IMGUI_SOURCE_DIR "${REPO_ROOT}/third_party/imgui-1.92.8-docking" CACHE PATH "Dear ImGui source directory")
set(VKB_SOURCE_DIR "${REPO_ROOT}/third_party/vkb" CACHE PATH "VkBootstrap source directory")
set(VMA_INCLUDE_DIR "${REPO_ROOT}/third_party/VulkanMemoryAllocator-3.3.0/include" CACHE PATH "VMA include directory")
set(SPIRV_REFLECT_SOURCE_DIR "${REPO_ROOT}/third_party/SPIRV-Reflect-vulkan-sdk-1.4.350.0" CACHE PATH "SPIRV-Reflect source directory")

function(require_file path hint)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing required dependency file: ${path}\nHint: ${hint}")
  endif()
endfunction()

require_file("${SDL3_INCLUDE_DIR}/SDL3/SDL.h" "Set SDL3_INCLUDE_DIR or prepare dependencies under the repository root.")
require_file("${EIGEN_INCLUDE_DIR}/Eigen/Dense" "Set EIGEN_INCLUDE_DIR to a valid Eigen root.")
require_file("${IMGUI_SOURCE_DIR}/imgui.h" "Set IMGUI_SOURCE_DIR to a valid Dear ImGui source root.")
require_file("${VKB_SOURCE_DIR}/src/VkBootstrap.h" "Set VKB_SOURCE_DIR to a valid VkBootstrap source root.")
require_file("${VKB_SOURCE_DIR}/src/VkBootstrap.cpp" "Set VKB_SOURCE_DIR to a valid VkBootstrap source root.")
require_file("${VMA_INCLUDE_DIR}/vk_mem_alloc.h" "Set VMA_INCLUDE_DIR to a valid VMA include directory.")
require_file("${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.h" "Set SPIRV_REFLECT_SOURCE_DIR to a valid SPIRV-Reflect source root.")
require_file("${SPIRV_REFLECT_SOURCE_DIR}/spirv_reflect.c" "Set SPIRV_REFLECT_SOURCE_DIR to a valid SPIRV-Reflect source root.")

set(PROJECT_INCLUDE_DIRS
  ${REPO_ROOT}/src
  ${REPO_ROOT}/third_party/cjson
  ${SDL3_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${VMA_INCLUDE_DIR}
  ${SPIRV_REFLECT_SOURCE_DIR}
  ${VKB_SOURCE_DIR}/src
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends
)

set(ALGORITHM_LIBRARY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test")

option(BUILD_ALGORITHM_SAMPLE_PLUGIN "Build the temporary sample algorithm plugin." OFF)
set(CORE_BUILD_DIR "${REPO_ROOT}/build" CACHE PATH "Core build directory containing the base libraries for algorithm plugins")

function(import_core_static_library target_name library_name)
  add_library(${target_name} STATIC IMPORTED GLOBAL)
  set_target_properties(${target_name} PROPERTIES
    IMPORTED_LOCATION_DEBUG "${CORE_BUILD_DIR}/Debug/${library_name}"
    IMPORTED_LOCATION_RELEASE "${CORE_BUILD_DIR}/Release/${library_name}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${CORE_BUILD_DIR}/Release/${library_name}"
    IMPORTED_LOCATION_MINSIZEREL "${CORE_BUILD_DIR}/Release/${library_name}"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
  )
endfunction()

function(require_core_library path hint)
  require_file("${path}" "${hint}")
endfunction()

set(CORE_DEBUG_DIR "${CORE_BUILD_DIR}/Debug")
set(CORE_RELEASE_DIR "${CORE_BUILD_DIR}/Release")

if(BUILD_ALGORITHM_SAMPLE_PLUGIN)
  require_core_library("${CORE_DEBUG_DIR}/algorithm_support.lib" "Build the root project before building the sample algorithm plugin.")
  require_core_library("${CORE_DEBUG_DIR}/algorithm_management.lib" "Build the root project before building the sample algorithm plugin.")
  require_core_library("${CORE_DEBUG_DIR}/common_data.lib" "Build the root project before building the sample algorithm plugin.")
  require_core_library("${CORE_DEBUG_DIR}/runtime_systems.lib" "Build the root project before building the sample algorithm plugin.")
endif()

add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion"
  COMMAND ${GLSLC} -o
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.vert"
  COMMAND ${GLSLC} -o
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.frag.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.frag"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.frag"
  VERBATIM
)

add_custom_target(temporary_test_line_motion_gpu_tick_shaders DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.vert.spv"
  "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_gpu_tick.frag.spv"
)

add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion"
  COMMAND ${GLSLC} -o
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.vert"
  COMMAND ${GLSLC} -o
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.frag.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.frag"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.frag"
  VERBATIM
)

add_custom_target(temporary_test_line_motion_result_render_shaders DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.vert.spv"
  "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion/temporary_test_line_motion_result_render.frag.spv"
)

add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.frag.spv"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test"
  COMMAND ${GLSLC} -o
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.vert.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.vert"
  COMMAND ${GLSLC} -o
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.frag.spv"
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.frag"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.frag"
  VERBATIM
)

add_custom_target(v6a2_triangle_collision_runtime_test_result_render_shaders DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.vert.spv"
  "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_result_render.frag.spv"
)

add_custom_target(algorithm_packages)
add_dependencies(algorithm_packages
  temporary_test_line_motion_gpu_tick_shaders
  temporary_test_line_motion_result_render_shaders
  v6a2_triangle_collision_runtime_test_result_render_shaders
)

if(BUILD_ALGORITHM_SAMPLE_PLUGIN)
  import_core_static_library(algorithm_support_core algorithm_support.lib)
  import_core_static_library(algorithm_management_core algorithm_management.lib)
  import_core_static_library(common_data_core common_data.lib)
  import_core_static_library(runtime_systems_core runtime_systems.lib)

  add_library(SDL3::SDL3 UNKNOWN IMPORTED GLOBAL)
  set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION_DEBUG "${SDL3_LIBRARY_DEBUG}"
    IMPORTED_LOCATION_RELEASE "${SDL3_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_RELWITHDEBINFO "${SDL3_LIBRARY_RELEASE}"
    IMPORTED_LOCATION_MINSIZEREL "${SDL3_LIBRARY_RELEASE}"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
  )

  add_library(temporary_test_line_motion_plugin MODULE
    temporary_test_line_motion/temporary_test_line_motion_plugin.cpp
  )
  target_include_directories(temporary_test_line_motion_plugin PUBLIC ${PROJECT_INCLUDE_DIRS})
  target_compile_definitions(temporary_test_line_motion_plugin PRIVATE
    ALGORITHM_LIBRARY_PLUGIN_BUILD=1
    ALGORITHM_LIBRARY_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(temporary_test_line_motion_plugin PRIVATE
    algorithm_management_core
    algorithm_support_core
    common_data_core
    runtime_systems_core
    Vulkan::Vulkan
    SDL3::SDL3
  )
  set_target_properties(temporary_test_line_motion_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "temporary_test_line_motion"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/temporary_test_line_motion"
  )
  add_dependencies(algorithm_packages temporary_test_line_motion_plugin)

  add_library(v6a2_triangle_collision_runtime_test_plugin MODULE
    v6a2_triangle_collision_runtime_test/v6a2_triangle_collision_runtime_test_plugin.cpp
  )
  target_include_directories(v6a2_triangle_collision_runtime_test_plugin PUBLIC ${PROJECT_INCLUDE_DIRS})
  target_compile_definitions(v6a2_triangle_collision_runtime_test_plugin PRIVATE
    ALGORITHM_LIBRARY_PLUGIN_BUILD=1
    ALGORITHM_LIBRARY_RESOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(v6a2_triangle_collision_runtime_test_plugin PRIVATE
    algorithm_management_core
    algorithm_support_core
    common_data_core
  )
  set_target_properties(v6a2_triangle_collision_runtime_test_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "v6a2_triangle_collision_runtime_test"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test"
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/v6a2_triangle_collision_runtime_test"
  )
  add_dependencies(algorithm_packages v6a2_triangle_collision_runtime_test_plugin)
endif()
