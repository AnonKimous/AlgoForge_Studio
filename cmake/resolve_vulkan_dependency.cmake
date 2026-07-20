include_guard(GLOBAL)

include(FetchContent)

# The Vulkan loader and Vulkan headers have different compatibility constraints.
# A distribution may provide a usable loader while shipping headers older than
# the version required by the project's pinned vk-bootstrap revision. Keep the
# loader selected by CMake, but fetch the matching Khronos headers when needed.
function(resolve_algoforge_vulkan_dependency)
  find_package(Vulkan REQUIRED)

  set(_algoforge_minimum_vulkan_header_version "1.4.0")
  set(_algoforge_use_fetched_headers OFF)
  if(NOT DEFINED Vulkan_VERSION OR
     Vulkan_VERSION VERSION_LESS _algoforge_minimum_vulkan_header_version)
    set(_algoforge_use_fetched_headers ON)
  endif()

  if(NOT _algoforge_use_fetched_headers)
    message(STATUS "Using system Vulkan headers ${Vulkan_VERSION} from ${Vulkan_INCLUDE_DIRS}")
    return()
  endif()

  message(STATUS
    "System Vulkan headers '${Vulkan_VERSION}' are older than "
    "${_algoforge_minimum_vulkan_header_version}; fetching Vulkan-Headers v1.4.350")

  FetchContent_Declare(algoforge_vulkan_headers
    URL "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.350.zip"
  )
  FetchContent_GetProperties(algoforge_vulkan_headers)
  if(NOT algoforge_vulkan_headers_POPULATED)
    FetchContent_Populate(algoforge_vulkan_headers)
  endif()

  set(_algoforge_vulkan_include_dir
    "${algoforge_vulkan_headers_SOURCE_DIR}/include")
  if(NOT EXISTS "${_algoforge_vulkan_include_dir}/vulkan/vulkan_core.h")
    message(FATAL_ERROR
      "Fetched Vulkan-Headers does not contain vulkan/vulkan_core.h: "
      "${_algoforge_vulkan_include_dir}")
  endif()

  if(NOT TARGET Vulkan::Vulkan)
    message(FATAL_ERROR "find_package(Vulkan) did not create Vulkan::Vulkan")
  endif()

  # Preserve the system loader chosen by FindVulkan while replacing only the
  # public header search path used by targets that link Vulkan::Vulkan.
  set_property(TARGET Vulkan::Vulkan PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES "${_algoforge_vulkan_include_dir}")

  set(Vulkan_INCLUDE_DIR "${_algoforge_vulkan_include_dir}" CACHE PATH
    "AlgoForge Vulkan header directory" FORCE)
  set(Vulkan_INCLUDE_DIRS "${_algoforge_vulkan_include_dir}" PARENT_SCOPE)
  set(Vulkan_VERSION "1.4.350" PARENT_SCOPE)
endfunction()
