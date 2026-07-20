cmake_minimum_required(VERSION 3.20)

project(algorithm_algos LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32)
  if(MSVC)
    message(STATUS "Windows: MSVC is the recommended toolchain for this project.")
    add_compile_options(
      "$<$<COMPILE_LANGUAGE:C>:/utf-8>"
      "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
    )
  else()
    message(WARNING "Windows: MSVC is the recommended toolchain for this project, but continuing with the current compiler.")
  endif()
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

include(FetchContent)
include("${REPO_ROOT}/cmake/resolve_vulkan_dependency.cmake")
resolve_algoforge_vulkan_dependency()
find_program(GLSLC glslc REQUIRED)
include("${REPO_ROOT}/cmake/shared_dependencies.cmake")
resolve_sdl3_dependency("${REPO_ROOT}")
resolve_eigen_dependency("${REPO_ROOT}")
resolve_imgui_dependency("${REPO_ROOT}")
resolve_vkb_dependency("${REPO_ROOT}")
resolve_vma_dependency("${REPO_ROOT}")
resolve_spirv_reflect_dependency("${REPO_ROOT}")
resolve_cjson_dependency("${REPO_ROOT}")

if(0)
set(SDL3_INCLUDE_DIR "${REPO_ROOT}/third_party/SDL-main/include" CACHE PATH "SDL3 include directory")
set(SDL3_LIBRARY_DEBUG "" CACHE FILEPATH "SDL3 debug import/static library")
set(SDL3_LIBRARY_RELEASE "" CACHE FILEPATH "SDL3 release import/static library")
set(EIGEN_INCLUDE_DIR "${REPO_ROOT}/third_party/eigen" CACHE PATH "Eigen include directory")
set(IMGUI_SOURCE_DIR "${REPO_ROOT}/third_party/imgui-1.92.8-docking" CACHE PATH "Dear ImGui source directory")
set(VKB_SOURCE_DIR "${REPO_ROOT}/third_party/vkb" CACHE PATH "VkBootstrap source directory")
set(VMA_INCLUDE_DIR "${REPO_ROOT}/third_party/VulkanMemoryAllocator-3.3.0/include" CACHE PATH "VMA include directory")
set(SPIRV_REFLECT_SOURCE_DIR "" CACHE PATH "SPIRV-Reflect source directory")

function(resolve_existing_path out_var relative_file)
  foreach(candidate IN LISTS ARGN)
    if(EXISTS "${candidate}/${relative_file}")
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(resolve_existing_file out_var)
  foreach(candidate IN LISTS ARGN)
    if(EXISTS "${candidate}")
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

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
set(_SPIRV_REFLECT_LOCAL_DIR "${REPO_ROOT}/third_party/SPIRV-Reflect-vulkan-sdk-1.4.350.0")
if(EXISTS "${_SPIRV_REFLECT_LOCAL_DIR}/spirv_reflect.h")
  message(STATUS "Using local SPIRV-Reflect mirror.")
  set(SPIRV_REFLECT_SOURCE_DIR "${_SPIRV_REFLECT_LOCAL_DIR}" CACHE PATH "SPIRV-Reflect source directory" FORCE)
else()
  message(STATUS "Fetching SPIRV-Reflect from GitHub.")
  FetchContent_Declare(spirv_reflect_source
    URL "https://github.com/KhronosGroup/SPIRV-Reflect/archive/c90b7b781cdcff63cf1b409ffc7ca0a714e0425e.zip"
  )
  FetchContent_Populate(spirv_reflect_source)
  set(SPIRV_REFLECT_SOURCE_DIR "${spirv_reflect_source_SOURCE_DIR}" CACHE PATH "SPIRV-Reflect source directory" FORCE)
endif()

endif()

set(ALGORITHM_LIBRARY_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmSrc" CACHE PATH "Root directory containing algorithm algo source files")
set(ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmruntimeLib" CACHE PATH "Root directory for built algorithm runtime artifacts")
set(ALGORITHM_LIBRARY_SELECTED_ROOT "" CACHE PATH "Optional source package root to configure in isolation")
file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_SOURCE_ROOT}" ALGORITHM_LIBRARY_SOURCE_ROOT)
file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT}" ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT)
if(ALGORITHM_LIBRARY_SELECTED_ROOT)
  file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_SELECTED_ROOT}" ALGORITHM_LIBRARY_SELECTED_ROOT)
  file(RELATIVE_PATH _selected_algorithm_relative_root
    "${ALGORITHM_LIBRARY_SOURCE_ROOT}"
    "${ALGORITHM_LIBRARY_SELECTED_ROOT}")
  if(_selected_algorithm_relative_root STREQUAL "" OR
     _selected_algorithm_relative_root MATCHES "^\.\.")
    message(FATAL_ERROR
      "ALGORITHM_LIBRARY_SELECTED_ROOT must be inside ALGORITHM_LIBRARY_SOURCE_ROOT: "
      "${ALGORITHM_LIBRARY_SELECTED_ROOT}")
  endif()
  message(STATUS
    "Configuring selected algorithm package tree: ${_selected_algorithm_relative_root}")
endif()

require_file("${ALGORITHM_LIBRARY_SOURCE_ROOT}/algorithm_plugin_api.h" "Set ALGORITHM_LIBRARY_SOURCE_ROOT to the mirrored algorithm source root.")

set(PROJECT_INCLUDE_DIRS
  ${REPO_ROOT}/src
  ${CJSON_SOURCE_DIR}
  ${SDL3_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${VMA_INCLUDE_DIR}
  ${SPIRV_REFLECT_SOURCE_DIR}
  ${VKB_SOURCE_DIR}/src
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends
)

option(BUILD_ALGORITHM_SAMPLE_PLUGIN "Build the temporary sample algorithm plugin." OFF)
option(BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS "Build algorithm plugin algos that require CUDA sources." OFF)
set(CORE_BUILD_DIR "${REPO_ROOT}/build" CACHE PATH "Core build directory containing the base libraries for algorithm plugins")
set(PHYSX_INCLUDE_DIR "$ENV{ALGOFORGE_PHYSX_INCLUDE_DIR}" CACHE PATH "PhysX include directory for compatibility algorithm plugins")
set(PHYSX_RUNTIME_DIR "$ENV{ALGOFORGE_PHYSX_RUNTIME_DIR}" CACHE PATH "PhysX runtime DLL directory for compatibility algorithm plugins")

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

function(append_unique_global_property_value property_name property_value)
  get_property(existing_values GLOBAL PROPERTY "${property_name}")
  if(existing_values STREQUAL "NOTFOUND")
    set(existing_values "")
  endif()
  list(FIND existing_values "${property_value}" existing_index)
  if(existing_index EQUAL -1)
    set_property(GLOBAL APPEND PROPERTY "${property_name}" "${property_value}")
  endif()
endfunction()

set(CORE_DEBUG_DIR "${CORE_BUILD_DIR}/Debug")
if(BUILD_ALGORITHM_SAMPLE_PLUGIN)
  set(ALGORITHM_PLUGIN_MIRROR_SRC_ROOT "${CMAKE_CURRENT_BINARY_DIR}/plugin_support_mirror/src")
  file(MAKE_DIRECTORY "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}")
  file(COPY "${REPO_ROOT}/src/algomanager/bridge" DESTINATION "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}/algomanager")
  file(COPY "${REPO_ROOT}/src/algomanager/catalog" DESTINATION "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}/algomanager")
  file(COPY "${REPO_ROOT}/src/common_data" DESTINATION "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}")

  add_library(algorithm_plugin_support_mirror STATIC
    "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}/algomanager/catalog/algorithm_package_location.cpp"
    "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}/algomanager/catalog/algorithm_plugin_support_minimal.cpp"
    "${CJSON_SOURCE_DIR}/cJSON.c"
  )
  target_include_directories(algorithm_plugin_support_mirror PUBLIC
    "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}"
    "${CJSON_SOURCE_DIR}"
  )
  target_compile_definitions(algorithm_plugin_support_mirror PUBLIC
    ALGOFORGE_PROJECT_ROOT="${REPO_ROOT}"
  )
endif()

add_custom_target(algorithm_algos)
set_property(GLOBAL PROPERTY ALGORITHM_ALGO_TARGETS "")

function(register_algorithm_algo algo_manifest)
  get_filename_component(algorithm_dir "${algo_manifest}" DIRECTORY)
  get_filename_component(algorithm_name "${algorithm_dir}" NAME)
  file(RELATIVE_PATH algorithm_relative_dir "${ALGORITHM_LIBRARY_SOURCE_ROOT}" "${algorithm_dir}")
  if(algorithm_relative_dir STREQUAL "" OR algorithm_relative_dir MATCHES "^\\.\\.")
    message(FATAL_ERROR "Algorithm algo directory ${algorithm_dir} is outside ${ALGORITHM_LIBRARY_SOURCE_ROOT}")
  endif()
  string(REPLACE "\\" "/" algorithm_relative_dir "${algorithm_relative_dir}")
  string(MAKE_C_IDENTIFIER "${algorithm_relative_dir}" algorithm_target_name)
  set(algorithm_output_dir "${ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT}/${algorithm_relative_dir}")
  set(algorithm_source_dir "${algorithm_dir}")
  file(GLOB algorithm_shader_sources CONFIGURE_DEPENDS
    "${algorithm_source_dir}/*.vert"
    "${algorithm_source_dir}/*.frag"
  )
  file(GLOB plugin_sources CONFIGURE_DEPENDS
    "${algorithm_source_dir}/*_plugin.cpp"
    "${algorithm_source_dir}/*.cu"
  )
  if(NOT algorithm_shader_sources AND NOT plugin_sources)
    set(stage0_source_dir "${algorithm_dir}/${algorithm_name}_stage0")
    if(EXISTS "${stage0_source_dir}")
      set(algorithm_source_dir "${stage0_source_dir}")
      file(GLOB algorithm_shader_sources CONFIGURE_DEPENDS
        "${algorithm_source_dir}/*.vert"
        "${algorithm_source_dir}/*.frag"
      )
      file(GLOB plugin_sources CONFIGURE_DEPENDS
        "${algorithm_source_dir}/*_plugin.cpp"
        "${algorithm_source_dir}/*.cu"
      )
    endif()
  endif()

  set(plugin_sources_for_build ${plugin_sources})
  set(plugin_has_cuda_source OFF)
  foreach(plugin_source IN LISTS plugin_sources)
    if(plugin_source MATCHES "\\.cu$")
      set(plugin_has_cuda_source ON)
    endif()
  endforeach()
  if(plugin_has_cuda_source AND NOT BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS)
    list(FILTER plugin_sources_for_build EXCLUDE REGEX "\\.cu$")
  endif()
  list(SORT plugin_sources_for_build)
  list(LENGTH plugin_sources_for_build plugin_source_count)

  file(MAKE_DIRECTORY "${algorithm_output_dir}")

  list(SORT algorithm_shader_sources)
  if(NOT algorithm_shader_sources)
    message(FATAL_ERROR "No shader sources found in ${algorithm_source_dir}")
  endif()

  set(algorithm_shader_outputs)
  foreach(shader_source IN LISTS algorithm_shader_sources)
    get_filename_component(shader_name "${shader_source}" NAME)
    file(RELATIVE_PATH shader_relative_path "${algorithm_dir}" "${shader_source}")
    set(shader_output "${algorithm_output_dir}/${shader_relative_path}.spv")
    get_filename_component(shader_output_dir "${shader_output}" DIRECTORY)
    file(MAKE_DIRECTORY "${shader_output_dir}")
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

  if(plugin_source_count GREATER 0 AND BUILD_ALGORITHM_SAMPLE_PLUGIN)
    set(algorithm_runtime_files "")
    add_library("${algorithm_target_name}_plugin" MODULE ${plugin_sources_for_build})
    target_include_directories("${algorithm_target_name}_plugin" PUBLIC
      "${ALGORITHM_PLUGIN_MIRROR_SRC_ROOT}"
      ${PROJECT_INCLUDE_DIRS}
    )
    if(algorithm_name MATCHES "physx|physics_sdk_compat_demo")
      if(NOT PHYSX_INCLUDE_DIR OR NOT EXISTS "${PHYSX_INCLUDE_DIR}/PxPhysicsAPI.h")
        message(FATAL_ERROR
          "PHYSX_INCLUDE_DIR must point to a PhysX include directory containing PxPhysicsAPI.h "
          "when building a PhysX compatibility plugin.")
      endif()
      if(NOT PHYSX_RUNTIME_DIR)
        message(FATAL_ERROR
          "PHYSX_RUNTIME_DIR must point to the PhysX runtime DLL directory "
          "when building a PhysX compatibility plugin.")
      endif()
      target_include_directories("${algorithm_target_name}_plugin" PRIVATE "${PHYSX_INCLUDE_DIR}")
      foreach(physx_runtime_name IN ITEMS
          PhysXCommon_64.dll
          PhysXFoundation_64.dll
          PhysX_64.dll)
        if(NOT EXISTS "${PHYSX_RUNTIME_DIR}/${physx_runtime_name}")
          message(FATAL_ERROR
            "Missing PhysX runtime DLL: ${PHYSX_RUNTIME_DIR}/${physx_runtime_name}")
        endif()
        add_custom_command(TARGET "${algorithm_target_name}_plugin" POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PHYSX_RUNTIME_DIR}/${physx_runtime_name}"
            "${algorithm_output_dir}/${physx_runtime_name}"
          VERBATIM
        )
        list(APPEND algorithm_runtime_files "${physx_runtime_name}")
      endforeach()
      if(algorithm_name MATCHES "^physx_lance_(compat|cuda)_demo$")
        foreach(physx_link_name IN ITEMS
            PhysX_64.lib
            PhysXCommon_64.lib
            PhysXFoundation_64.lib)
          if(NOT EXISTS "${PHYSX_RUNTIME_DIR}/${physx_link_name}")
            message(FATAL_ERROR
              "Missing PhysX import library: ${PHYSX_RUNTIME_DIR}/${physx_link_name}")
          endif()
          target_link_libraries("${algorithm_target_name}_plugin" PRIVATE
            "${PHYSX_RUNTIME_DIR}/${physx_link_name}")
        endforeach()
      endif()
    endif()
    target_compile_definitions("${algorithm_target_name}_plugin" PRIVATE
      ALGORITHM_LIBRARY_PLUGIN_BUILD=1
      ALGORITHM_LIBRARY_RESOURCE_ROOT="${ALGORITHM_LIBRARY_SOURCE_ROOT}"
    )
    if(plugin_has_cuda_source AND BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS)
      target_compile_definitions("${algorithm_target_name}_plugin" PRIVATE
        ALGORITHM_LIBRARY_PLUGIN_HAS_CUDA=1
      )
    endif()
    target_link_libraries("${algorithm_target_name}_plugin" PRIVATE
      algorithm_plugin_support_mirror
    )
    if(plugin_has_cuda_source AND BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS)
      target_link_libraries("${algorithm_target_name}_plugin" PRIVATE CUDA::cudart)
    endif()
    set_target_properties("${algorithm_target_name}_plugin" PROPERTIES
      PREFIX ""
      OUTPUT_NAME "${algorithm_name}"
      WINDOWS_EXPORT_ALL_SYMBOLS ON
      RUNTIME_OUTPUT_DIRECTORY "${algorithm_output_dir}"
      LIBRARY_OUTPUT_DIRECTORY "${algorithm_output_dir}"
      ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/algorithm_import_libraries/${algorithm_relative_dir}"
      PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    add_custom_command(TARGET "${algorithm_target_name}_plugin" POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        "-DOUTPUT_FILE=${algorithm_output_dir}/runtime.artifacts"
        "-DPLUGIN_MODULE=$<TARGET_FILE_NAME:${algorithm_target_name}_plugin>"
        "-DRUNTIME_FILES=${algorithm_runtime_files}"
        -P "${REPO_ROOT}/cmake/write_algorithm_runtime_artifacts.cmake"
      VERBATIM
    )
    add_dependencies("${algorithm_target_name}_plugin" "${shader_target}")
  endif()

  set(algo_target "${algorithm_target_name}_algo")
  add_custom_target("${algo_target}" DEPENDS "${shader_target}")
  if(TARGET "${algorithm_target_name}_plugin")
    add_dependencies("${algo_target}" "${algorithm_target_name}_plugin")
  endif()

  set(algorithm_bundle_root "${algorithm_relative_dir}")
  string(FIND "${algorithm_relative_dir}" "/" algorithm_bundle_root_separator)
  if(NOT algorithm_bundle_root_separator EQUAL -1)
    string(SUBSTRING "${algorithm_relative_dir}" 0 "${algorithm_bundle_root_separator}" algorithm_bundle_root)
  endif()
  string(MAKE_C_IDENTIFIER "${algorithm_bundle_root}" algorithm_bundle_key)
  append_unique_global_property_value(ALGORITHM_BUNDLE_KEYS "${algorithm_bundle_key}")
  set_property(GLOBAL APPEND PROPERTY "ALGORITHM_BUNDLE_TARGETS_${algorithm_bundle_key}" "${algo_target}")
  if(algorithm_relative_dir STREQUAL algorithm_bundle_root)
    set_property(GLOBAL PROPERTY "ALGORITHM_BUNDLE_ROOT_TARGET_${algorithm_bundle_key}" "${algo_target}")
  endif()

  set_property(GLOBAL APPEND PROPERTY ALGORITHM_ALGO_TARGETS "${algo_target}")
endfunction()

if(ALGORITHM_LIBRARY_SELECTED_ROOT)
  file(GLOB_RECURSE ALGORITHM_PACKAGE_MANIFESTS CONFIGURE_DEPENDS
    "${ALGORITHM_LIBRARY_SELECTED_ROOT}/manifest.json"
  )
else()
  file(GLOB_RECURSE ALGORITHM_PACKAGE_MANIFESTS CONFIGURE_DEPENDS
    "${ALGORITHM_LIBRARY_SOURCE_ROOT}/manifest.json"
  )
endif()
list(SORT ALGORITHM_PACKAGE_MANIFESTS)
set(ALGORITHM_ALGO_COUNT 0)
foreach(algo_manifest IN LISTS ALGORITHM_PACKAGE_MANIFESTS)
  if(NOT EXISTS "${algo_manifest}")
    continue()
  endif()
  register_algorithm_algo("${algo_manifest}")
  math(EXPR ALGORITHM_ALGO_COUNT "${ALGORITHM_ALGO_COUNT} + 1")
endforeach()

if(ALGORITHM_ALGO_COUNT EQUAL 0)
  message(FATAL_ERROR "No algorithm manifests found in ${ALGORITHM_LIBRARY_SOURCE_ROOT}")
endif()

get_property(ALGORITHM_BUNDLE_KEYS GLOBAL PROPERTY ALGORITHM_BUNDLE_KEYS)
list(REMOVE_DUPLICATES ALGORITHM_BUNDLE_KEYS)
foreach(bundle_key IN LISTS ALGORITHM_BUNDLE_KEYS)
  get_property(bundle_root_target GLOBAL PROPERTY "ALGORITHM_BUNDLE_ROOT_TARGET_${bundle_key}")
  get_property(bundle_targets GLOBAL PROPERTY "ALGORITHM_BUNDLE_TARGETS_${bundle_key}")
  if(bundle_root_target AND bundle_targets)
    list(REMOVE_ITEM bundle_targets "${bundle_root_target}")
    if(bundle_targets)
      add_dependencies("${bundle_root_target}" ${bundle_targets})
    endif()
  endif()
endforeach()

get_property(ALGORITHM_ALGO_TARGETS GLOBAL PROPERTY ALGORITHM_ALGO_TARGETS)
if(ALGORITHM_ALGO_TARGETS)
  add_dependencies(algorithm_algos ${ALGORITHM_ALGO_TARGETS})
endif()
