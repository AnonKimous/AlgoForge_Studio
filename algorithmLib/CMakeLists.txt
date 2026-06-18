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

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

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

set(ALGORITHM_LIBRARY_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmSrc" CACHE PATH "Root directory containing algorithm package source files")
set(ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmruntimeLib" CACHE PATH "Root directory for built algorithm runtime artifacts")
file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_SOURCE_ROOT}" ALGORITHM_LIBRARY_SOURCE_ROOT)
file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT}" ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT)

require_file("${ALGORITHM_LIBRARY_SOURCE_ROOT}/algorithm_catalog.json" "Set ALGORITHM_LIBRARY_SOURCE_ROOT to the mirrored algorithm source root.")
require_file("${ALGORITHM_LIBRARY_SOURCE_ROOT}/algorithm_plugin_api.h" "Set ALGORITHM_LIBRARY_SOURCE_ROOT to the mirrored algorithm source root.")

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
if(BUILD_ALGORITHM_SAMPLE_PLUGIN)
  require_core_library("${CORE_DEBUG_DIR}/algorithm_management.lib" "Build the root project before building the sample algorithm plugin.")
  require_core_library("${CORE_DEBUG_DIR}/common_data.lib" "Build the root project before building the sample algorithm plugin.")
  require_core_library("${CORE_DEBUG_DIR}/runtime_systems.lib" "Build the root project before building the sample algorithm plugin.")
endif()

if(BUILD_ALGORITHM_SAMPLE_PLUGIN)
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
endif()

add_custom_target(algorithm_packages)
set_property(GLOBAL PROPERTY ALGORITHM_PACKAGE_TARGETS "")

function(register_algorithm_package package_manifest)
  get_filename_component(algorithm_dir "${package_manifest}" DIRECTORY)
  get_filename_component(algorithm_name "${algorithm_dir}" NAME)
  file(RELATIVE_PATH algorithm_relative_dir "${ALGORITHM_LIBRARY_SOURCE_ROOT}" "${algorithm_dir}")
  if(algorithm_relative_dir STREQUAL "" OR algorithm_relative_dir MATCHES "^\\.\\.")
    message(FATAL_ERROR "Algorithm package directory ${algorithm_dir} is outside ${ALGORITHM_LIBRARY_SOURCE_ROOT}")
  endif()
  string(REPLACE "\\" "/" algorithm_relative_dir "${algorithm_relative_dir}")
  string(MAKE_C_IDENTIFIER "${algorithm_relative_dir}" algorithm_target_name)
  set(algorithm_output_dir "${ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT}/${algorithm_relative_dir}")

  file(MAKE_DIRECTORY "${algorithm_output_dir}")

  file(GLOB algorithm_shader_sources
    "${algorithm_dir}/*.vert"
    "${algorithm_dir}/*.frag"
  )
  list(SORT algorithm_shader_sources)
  if(NOT algorithm_shader_sources)
    message(FATAL_ERROR "No shader sources found in ${algorithm_dir}")
  endif()

  set(algorithm_shader_outputs)
  foreach(shader_source IN LISTS algorithm_shader_sources)
    get_filename_component(shader_name "${shader_source}" NAME)
    set(shader_output "${algorithm_output_dir}/${shader_name}.spv")
    add_custom_command(
      OUTPUT "${shader_output}"
      COMMAND ${GLSLC} -o "${shader_output}" "${shader_source}"
      DEPENDS "${shader_source}"
      VERBATIM
    )
    list(APPEND algorithm_shader_outputs "${shader_output}")
  endforeach()

  set(shader_target "${algorithm_target_name}_shaders")
  add_custom_target("${shader_target}" DEPENDS ${algorithm_shader_outputs})

  file(GLOB plugin_sources
    "${algorithm_dir}/*_plugin.cpp"
  )
  list(SORT plugin_sources)
  list(LENGTH plugin_sources plugin_source_count)
  if(plugin_source_count GREATER 1)
    message(FATAL_ERROR "Multiple plugin sources found in ${algorithm_dir}. Keep one plugin source per algorithm folder.")
  endif()

  if(plugin_source_count EQUAL 1)
    list(GET plugin_sources 0 plugin_source)
    if(BUILD_ALGORITHM_SAMPLE_PLUGIN)
      add_library("${algorithm_target_name}_plugin" MODULE "${plugin_source}")
      target_include_directories("${algorithm_target_name}_plugin" PUBLIC ${PROJECT_INCLUDE_DIRS})
      target_compile_definitions("${algorithm_target_name}_plugin" PRIVATE
        ALGORITHM_LIBRARY_PLUGIN_BUILD=1
        ALGORITHM_LIBRARY_RESOURCE_ROOT="${ALGORITHM_LIBRARY_SOURCE_ROOT}"
      )
      target_link_libraries("${algorithm_target_name}_plugin" PRIVATE
        algorithm_management_core
        common_data_core
        runtime_systems_core
        Vulkan::Vulkan
        SDL3::SDL3
      )
      set_target_properties("${algorithm_target_name}_plugin" PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${algorithm_name}"
        RUNTIME_OUTPUT_DIRECTORY "${algorithm_output_dir}"
        LIBRARY_OUTPUT_DIRECTORY "${algorithm_output_dir}"
        ARCHIVE_OUTPUT_DIRECTORY "${algorithm_output_dir}"
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
      )
      add_dependencies("${algorithm_target_name}_plugin" "${shader_target}")
    endif()
  endif()

  set(package_target "${algorithm_target_name}_package")
  add_custom_target("${package_target}" DEPENDS "${shader_target}")
  if(TARGET "${algorithm_target_name}_plugin")
    add_dependencies("${package_target}" "${algorithm_target_name}_plugin")
  endif()

  set_property(GLOBAL APPEND PROPERTY ALGORITHM_PACKAGE_TARGETS "${package_target}")
endfunction()

file(GLOB_RECURSE ALGORITHM_PACKAGE_MANIFESTS
  "${ALGORITHM_LIBRARY_SOURCE_ROOT}/*_package.json"
)
list(SORT ALGORITHM_PACKAGE_MANIFESTS)
set(ALGORITHM_PACKAGE_COUNT 0)
foreach(package_manifest IN LISTS ALGORITHM_PACKAGE_MANIFESTS)
  if(NOT EXISTS "${package_manifest}")
    continue()
  endif()
  register_algorithm_package("${package_manifest}")
  math(EXPR ALGORITHM_PACKAGE_COUNT "${ALGORITHM_PACKAGE_COUNT} + 1")
endforeach()

if(ALGORITHM_PACKAGE_COUNT EQUAL 0)
  message(FATAL_ERROR "No algorithm package manifests found in ${ALGORITHM_LIBRARY_SOURCE_ROOT}")
endif()

get_property(ALGORITHM_PACKAGE_TARGETS GLOBAL PROPERTY ALGORITHM_PACKAGE_TARGETS)
if(ALGORITHM_PACKAGE_TARGETS)
  add_dependencies(algorithm_packages ${ALGORITHM_PACKAGE_TARGETS})
endif()
