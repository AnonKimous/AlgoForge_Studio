cmake_minimum_required(VERSION 3.20)

project(debugTool LANGUAGES C CXX)

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

find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)
include(FetchContent)

set(DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "Root path for third-party sources")

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

set(SDL3_INCLUDE_DIR "" CACHE PATH "SDL3 include directory")
set(SDL3_LIBRARY_DEBUG "" CACHE FILEPATH "SDL3 debug import/static library")
set(SDL3_LIBRARY_RELEASE "" CACHE FILEPATH "SDL3 release import/static library")
set(SDL3_DLL_DEBUG "" CACHE FILEPATH "SDL3 debug runtime DLL")
set(SDL3_DLL_RELEASE "" CACHE FILEPATH "SDL3 release runtime DLL")

set(EIGEN_INCLUDE_DIR "" CACHE PATH "Eigen include directory")
set(IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui source directory")
set(VKB_SOURCE_DIR "" CACHE PATH "VkBootstrap source directory")
set(VKB_GITHUB_ARCHIVE_URL "https://github.com/charles-lunarg/vk-bootstrap/archive/refs/heads/main.zip" CACHE STRING "VkBootstrap GitHub archive URL")
set(VMA_INCLUDE_DIR "" CACHE PATH "VMA include directory")
set(SPIRV_REFLECT_SOURCE_DIR "" CACHE PATH "SPIRV-Reflect source directory")
set(CJSON_SOURCE_DIR "" CACHE PATH "cJSON source directory")

resolve_existing_path(_SDL3_INCLUDE_DIR SDL3/SDL.h
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL-main/include"
  "D:/VulkanSDK/1.4.341.1/Include"
  "D:/VulkanSDK/1.4.328.1/Include"
)
if(_SDL3_INCLUDE_DIR)
  set(SDL3_INCLUDE_DIR "${_SDL3_INCLUDE_DIR}" CACHE PATH "SDL3 include directory" FORCE)
  resolve_existing_file(_SDL3_LIBRARY_DEBUG
    "D:/VulkanSDK/1.4.341.1/Lib/SDL3.lib"
    "D:/VulkanSDK/1.4.328.1/Lib/SDL3.lib"
  )
  resolve_existing_file(_SDL3_LIBRARY_RELEASE
    "D:/VulkanSDK/1.4.341.1/Lib/SDL3.lib"
    "D:/VulkanSDK/1.4.328.1/Lib/SDL3.lib"
  )
  resolve_existing_file(_SDL3_DLL_DEBUG
    "D:/VulkanSDK/1.4.341.1/Bin/SDL3d.dll"
    "D:/VulkanSDK/1.4.328.1/Bin/SDL3d.dll"
  )
  resolve_existing_file(_SDL3_DLL_RELEASE
    "D:/VulkanSDK/1.4.341.1/Bin/SDL3.dll"
    "D:/VulkanSDK/1.4.328.1/Bin/SDL3.dll"
  )
  set(SDL3_LIBRARY_DEBUG "${_SDL3_LIBRARY_DEBUG}" CACHE FILEPATH "SDL3 debug import/static library" FORCE)
  set(SDL3_LIBRARY_RELEASE "${_SDL3_LIBRARY_RELEASE}" CACHE FILEPATH "SDL3 release import/static library" FORCE)
  set(SDL3_DLL_DEBUG "${_SDL3_DLL_DEBUG}" CACHE FILEPATH "SDL3 debug runtime DLL" FORCE)
  set(SDL3_DLL_RELEASE "${_SDL3_DLL_RELEASE}" CACHE FILEPATH "SDL3 release runtime DLL" FORCE)
  set(SDL3_LINK_LIBS
    debug "${SDL3_LIBRARY_DEBUG}"
    optimized "${SDL3_LIBRARY_RELEASE}"
  )
else()
  message(STATUS "SDL3 not found locally; fetching SDL3 from GitHub.")
  FetchContent_Declare(sdl3_source
    URL "https://github.com/libsdl-org/SDL/archive/refs/heads/main.zip"
  )
  FetchContent_MakeAvailable(sdl3_source)
  set(SDL3_INCLUDE_DIR "${sdl3_source_SOURCE_DIR}/include" CACHE PATH "SDL3 include directory" FORCE)
  set(SDL3_LINK_LIBS SDL3::SDL3)
endif()

resolve_existing_path(_EIGEN_INCLUDE_DIR Eigen/Dense
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eigen"
  "D:/aaamyprj/physBased-render/build/_deps/eigen-src"
  "D:/tools/eigen-3.4.0"
  "D:/relyingResourse/vcpkg-master/vcpkg-master/installed/x64-windows/include/eigen3"
  "D:/relyingResourse/vcpkg-master/vcpkg-master/installed/x64-windows/include"
)
if(_EIGEN_INCLUDE_DIR)
  set(EIGEN_INCLUDE_DIR "${_EIGEN_INCLUDE_DIR}" CACHE PATH "Eigen include directory" FORCE)
else()
  message(STATUS "Eigen not found locally; fetching Eigen from GitLab.")
  FetchContent_Declare(eigen_source
    URL "https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip"
  )
  FetchContent_MakeAvailable(eigen_source)
  set(EIGEN_INCLUDE_DIR "${eigen_source_SOURCE_DIR}" CACHE PATH "Eigen include directory" FORCE)
endif()

resolve_existing_path(_IMGUI_SOURCE_DIR imgui.h
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-1.92.8-docking"
  "D:/aaamyprj/physBased-render/build/_deps/imgui-src"
)
if(_IMGUI_SOURCE_DIR)
  set(IMGUI_SOURCE_DIR "${_IMGUI_SOURCE_DIR}" CACHE PATH "Dear ImGui source directory" FORCE)
else()
  message(STATUS "ImGui not found locally; fetching ImGui from GitHub.")
  FetchContent_Declare(imgui_source
    URL "https://github.com/ocornut/imgui/archive/refs/heads/docking.zip"
  )
  FetchContent_Populate(imgui_source)
  set(IMGUI_SOURCE_DIR "${imgui_source_SOURCE_DIR}" CACHE PATH "Dear ImGui source directory" FORCE)
endif()

resolve_existing_path(_VKB_SOURCE_DIR src/VkBootstrap.h
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vkb"
  "D:/aaamyprj/physBased-render/build/_deps/vk_bootstrap-src"
)
if(_VKB_SOURCE_DIR)
  set(VKB_SOURCE_DIR "${_VKB_SOURCE_DIR}" CACHE PATH "VkBootstrap source directory" FORCE)
else()
  message(STATUS "VkBootstrap not found locally; fetching from GitHub.")
  FetchContent_Declare(vkb_source
    URL "${VKB_GITHUB_ARCHIVE_URL}"
  )
  FetchContent_GetProperties(vkb_source)
  if(NOT vkb_source_POPULATED)
    FetchContent_Populate(vkb_source)
  endif()
  set(VKB_SOURCE_DIR "${vkb_source_SOURCE_DIR}" CACHE PATH "VkBootstrap source directory" FORCE)
endif()

resolve_existing_path(_VMA_INCLUDE_DIR vk_mem_alloc.h
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator-3.3.0/include"
  "D:/VulkanSDK/1.4.341.1/Include/vma"
  "D:/aaamyprj/physBased-render/build/_deps/vma-src/include"
)
if(_VMA_INCLUDE_DIR)
  set(VMA_INCLUDE_DIR "${_VMA_INCLUDE_DIR}" CACHE PATH "VMA include directory" FORCE)
else()
  message(STATUS "VMA not found locally; fetching VMA from GitHub.")
  FetchContent_Declare(vma_source
    URL "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.zip"
  )
  FetchContent_Populate(vma_source)
  set(VMA_INCLUDE_DIR "${vma_source_SOURCE_DIR}/include" CACHE PATH "VMA include directory" FORCE)
endif()

resolve_existing_path(_SPIRV_REFLECT_SOURCE_DIR spirv_reflect.h
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Reflect-vulkan-sdk-1.4.350.0"
  "D:/VulkanSDK/1.4.341.1/Source/SPIRV-Reflect"
  "D:/aaamyprj/physBased-render/build/_deps/spirv_reflect-src"
)
if(_SPIRV_REFLECT_SOURCE_DIR)
  set(SPIRV_REFLECT_SOURCE_DIR "${_SPIRV_REFLECT_SOURCE_DIR}" CACHE PATH "SPIRV-Reflect source directory" FORCE)
else()
  message(STATUS "SPIRV-Reflect not found locally; fetching SPIRV-Reflect from GitHub.")
  FetchContent_Declare(spirv_reflect_source
    URL "https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/heads/vulkan-sdk-1.4.350.0.zip"
  )
  FetchContent_Populate(spirv_reflect_source)
  set(SPIRV_REFLECT_SOURCE_DIR "${spirv_reflect_source_SOURCE_DIR}" CACHE PATH "SPIRV-Reflect source directory" FORCE)
endif()

resolve_existing_path(_CJSON_SOURCE_DIR cJSON.c
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson"
  "D:/relyingResourse/vcpkg-master/vcpkg-master/buildtrees/cjson/src/v1.7.18-66cc8df541.clean"
)
if(_CJSON_SOURCE_DIR)
  set(CJSON_SOURCE_DIR "${_CJSON_SOURCE_DIR}" CACHE PATH "cJSON source directory" FORCE)
else()
  message(STATUS "cJSON not found locally; fetching cJSON from GitHub.")
  FetchContent_Declare(cjson_source
    URL "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.zip"
  )
  FetchContent_Populate(cjson_source)
  set(CJSON_SOURCE_DIR "${cjson_source_SOURCE_DIR}" CACHE PATH "cJSON source directory" FORCE)
endif()

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
  ${CJSON_SOURCE_DIR}
  ${SDL3_INCLUDE_DIR}
  ${EIGEN_INCLUDE_DIR}
  ${VMA_INCLUDE_DIR}
  ${SPIRV_REFLECT_SOURCE_DIR}
  ${VKB_SOURCE_DIR}/src
  ${VKB_COMPAT_INCLUDE_DIR}
  ${IMGUI_SOURCE_DIR}
  ${IMGUI_SOURCE_DIR}/backends
)

if(NOT SDL3_LINK_LIBS)
  set(SDL3_LINK_LIBS
    debug "${SDL3_LIBRARY_DEBUG}"
    optimized "${SDL3_LIBRARY_RELEASE}"
  )
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities/algorithm_library" ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT)

add_library(common_data STATIC
  src/common_data/mesh.cpp
)
target_include_directories(common_data PUBLIC ${PROJECT_INCLUDE_DIRS})

add_library(algorithm_management STATIC
  src/algorithm_management/job_cpu.cpp
  src/algorithm_management/job_gpu.cpp
  src/algorithm_support/algorithm_package_location.cpp
  src/algorithm_support/algorithm_support_manager.cpp
  src/algorithm_support/algorithm_intervention.cpp
  src/algorithm_support/algorithm_package_decomposer.cpp
  src/algorithm_support/algorithm_plugin_loader.cpp
  src/algorithm_support/algorithm_package_loader.cpp
  ${CJSON_SOURCE_DIR}/cJSON.c
)
target_include_directories(algorithm_management PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(algorithm_management PRIVATE
  ALGORITHM_MANAGEMENT_LAYER_INTERNAL_BUILD=1
  ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT="${ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT}"
)
target_link_libraries(algorithm_management PUBLIC common_data runtime_systems)

add_library(runtime_systems STATIC
  src/runtime_systems/window/sdl_window.cpp
  src/runtime_systems/algorithm_gpu_context.cpp
  src/runtime_systems/algorithm_gpu_executor.cpp
  src/runtime_systems/render/vma_impl.cpp
  src/runtime_systems/render/preview_renderer.cpp
  src/runtime_systems/render/imgui_vulkan_runtime.cpp
  src/runtime_systems/runtime_environment.cpp
  src/runtime_systems/memory_manager.cpp
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
target_link_libraries(algorithm_management PUBLIC runtime_systems)

add_library(agent STATIC
  src/agent/agent.cpp
)
target_include_directories(agent PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(agent PUBLIC common_data algorithm_management)

add_library(agent_management STATIC
  src/agent_management/agent_ticker.cpp
  src/agent_management/agent_manager.cpp
)
target_include_directories(agent_management PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(agent_management PRIVATE AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD=1)
target_link_libraries(agent_management PUBLIC common_data agent)

add_library(debug_tool_backend STATIC
  src/debug_tool/debug_tool_backend_runtime.cpp
)
target_include_directories(debug_tool_backend PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(debug_tool_backend PUBLIC common_data agent_management runtime_systems algorithm_management)

add_library(debug_tool_frontend STATIC
  src/debug_tool/debug_tool_frontend_panel.cpp
)
target_include_directories(debug_tool_frontend PUBLIC ${PROJECT_INCLUDE_DIRS})
target_compile_definitions(debug_tool_frontend PRIVATE
  PROJECT_DATA_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
target_link_libraries(debug_tool_frontend PUBLIC runtime_systems)

add_library(sdk STATIC
  src/sdk/sdk_kernel.cpp
  src/sdk/sdk_decomposer.cpp
)
target_include_directories(sdk PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(sdk PUBLIC agent_management)

set(SDK_PACKAGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
set(SDK_PACKAGE_INCLUDE_DIR "${SDK_PACKAGE_ROOT}/include")

add_custom_command(TARGET sdk POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${SDK_PACKAGE_INCLUDE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/sdk_kernel.h"
    "${SDK_PACKAGE_INCLUDE_DIR}/sdk_kernel.h"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/sdk_decomposer.h"
    "${SDK_PACKAGE_INCLUDE_DIR}/sdk_decomposer.h"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:sdk>"
    "${SDK_PACKAGE_ROOT}/$<TARGET_FILE_NAME:sdk>"
  VERBATIM
)

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

add_executable(debugTool WIN32
  src/debug_tool/main.cpp
)

target_link_libraries(debugTool PRIVATE
  debug_tool_frontend
  debug_tool_backend
  common_data
)

target_compile_definitions(debugTool PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  SHADER_PHYSICS_CONV_PATH="${SHADER_PHYSICS_CONV_PATH}"
)

if(WIN32)
  target_link_libraries(debugTool PRIVATE advapi32 user32 gdi32 shell32)

  add_custom_command(TARGET debugTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND ${GLSLC} -o "${SHADER_OUTPUT_DIR}/physics_convolution.comp.spv" "${SHADER_DIR}/physics_convolution.comp"
    VERBATIM
  )

  add_custom_command(TARGET debugTool PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DREQUIRED_FILE=$<IF:$<CONFIG:Debug>,${SDL3_LIBRARY_DEBUG},${SDL3_LIBRARY_RELEASE}>"
      "-DHINT_TEXT=$<IF:$<CONFIG:Debug>,Provide the SDL3 Debug library for Debug builds.,Provide the SDL3 Release library for non-Debug builds.>"
      -P "${CMAKE_HELPER_DIR}/require_generated_file.cmake"
    COMMAND ${CMAKE_COMMAND}
      "-DREQUIRED_FILE=$<IF:$<CONFIG:Debug>,${SDL3_DLL_DEBUG},${SDL3_DLL_RELEASE}>"
      "-DHINT_TEXT=$<IF:$<CONFIG:Debug>,Provide the SDL3 Debug runtime DLL for Debug builds.,Provide the SDL3 Release runtime DLL for non-Debug builds.>"
      -P "${CMAKE_HELPER_DIR}/require_generated_file.cmake"
  )

  add_custom_command(TARGET debugTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<IF:$<CONFIG:Debug>,${SDL3_DLL_DEBUG},${SDL3_DLL_RELEASE}>"
    "$<TARGET_FILE_DIR:debugTool>"
  )
endif()
