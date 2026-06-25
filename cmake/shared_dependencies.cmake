include_guard(GLOBAL)

include(FetchContent)

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

function(resolve_sdl3_dependency repo_root)
  set(SDL3_INCLUDE_DIR "" CACHE PATH "SDL3 include directory")
  set(SDL3_LIBRARY_DEBUG "" CACHE FILEPATH "SDL3 debug import/static library")
  set(SDL3_LIBRARY_RELEASE "" CACHE FILEPATH "SDL3 release import/static library")
  set(SDL3_DLL_DEBUG "" CACHE FILEPATH "SDL3 debug runtime DLL")
  set(SDL3_DLL_RELEASE "" CACHE FILEPATH "SDL3 release runtime DLL")

  resolve_existing_path(_SDL3_INCLUDE_DIR SDL3/SDL.h
    "${repo_root}/build/_deps/sdl3_source-src/include"
    "${repo_root}/third_party/SDL-main/include"
    "$ENV{VULKAN_SDK}/Include"
    "$ENV{VULKAN_SDK}/include"
  )
  if(_SDL3_INCLUDE_DIR)
    set(SDL3_INCLUDE_DIR "${_SDL3_INCLUDE_DIR}" CACHE PATH "SDL3 include directory" FORCE)
    resolve_existing_file(_SDL3_LIBRARY_DEBUG
      "$ENV{VULKAN_SDK}/Lib/SDL3.lib"
      "$ENV{VULKAN_SDK}/lib/SDL3.lib"
    )
    resolve_existing_file(_SDL3_LIBRARY_RELEASE
      "$ENV{VULKAN_SDK}/Lib/SDL3.lib"
      "$ENV{VULKAN_SDK}/lib/SDL3.lib"
    )
    resolve_existing_file(_SDL3_DLL_DEBUG
      "$ENV{VULKAN_SDK}/Bin/SDL3d.dll"
      "$ENV{VULKAN_SDK}/bin/SDL3d.dll"
    )
    resolve_existing_file(_SDL3_DLL_RELEASE
      "$ENV{VULKAN_SDK}/Bin/SDL3.dll"
      "$ENV{VULKAN_SDK}/bin/SDL3.dll"
    )
    if(_SDL3_LIBRARY_DEBUG AND _SDL3_LIBRARY_RELEASE)
      set(SDL3_LIBRARY_DEBUG "${_SDL3_LIBRARY_DEBUG}" CACHE FILEPATH "SDL3 debug import/static library" FORCE)
      set(SDL3_LIBRARY_RELEASE "${_SDL3_LIBRARY_RELEASE}" CACHE FILEPATH "SDL3 release import/static library" FORCE)
      set(SDL3_DLL_DEBUG "${_SDL3_DLL_DEBUG}" CACHE FILEPATH "SDL3 debug runtime DLL" FORCE)
      set(SDL3_DLL_RELEASE "${_SDL3_DLL_RELEASE}" CACHE FILEPATH "SDL3 release runtime DLL" FORCE)
      set(SDL3_LINK_LIBS
        debug "${SDL3_LIBRARY_DEBUG}"
        optimized "${SDL3_LIBRARY_RELEASE}"
        PARENT_SCOPE
      )
    else()
      message(STATUS "SDL3 library not found locally; fetching SDL3 from GitHub.")
      FetchContent_Declare(sdl3_source
        URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.8.zip"
      )
      FetchContent_MakeAvailable(sdl3_source)
      set(SDL3_INCLUDE_DIR "${sdl3_source_SOURCE_DIR}/include" CACHE PATH "SDL3 include directory" FORCE)
      set(SDL3_LINK_LIBS SDL3::SDL3 PARENT_SCOPE)
    endif()
  else()
    message(STATUS "SDL3 not found locally; fetching SDL3 from GitHub.")
    FetchContent_Declare(sdl3_source
      URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.8.zip"
    )
    FetchContent_MakeAvailable(sdl3_source)
    set(SDL3_INCLUDE_DIR "${sdl3_source_SOURCE_DIR}/include" CACHE PATH "SDL3 include directory" FORCE)
    set(SDL3_LINK_LIBS SDL3::SDL3 PARENT_SCOPE)
  endif()
endfunction()

function(resolve_eigen_dependency repo_root)
  set(EIGEN_INCLUDE_DIR "" CACHE PATH "Eigen include directory")

  resolve_existing_path(_EIGEN_INCLUDE_DIR Eigen/Dense
    "${repo_root}/build/_deps/eigen_source-src"
    "${repo_root}/third_party/eigen"
  )
  if(_EIGEN_INCLUDE_DIR)
    set(EIGEN_INCLUDE_DIR "${_EIGEN_INCLUDE_DIR}" CACHE PATH "Eigen include directory" FORCE)
  else()
    message(STATUS "Eigen not found locally; fetching Eigen from GitLab via git.")
    FetchContent_Declare(eigen_source
      GIT_REPOSITORY "https://gitlab.com/libeigen/eigen.git"
      GIT_TAG "3.4.0"
      GIT_SHALLOW TRUE
      GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(eigen_source)
    set(EIGEN_INCLUDE_DIR "${eigen_source_SOURCE_DIR}" CACHE PATH "Eigen include directory" FORCE)
  endif()
endfunction()

function(resolve_imgui_dependency repo_root)
  set(IMGUI_SOURCE_DIR "" CACHE PATH "Dear ImGui source directory")

  resolve_existing_path(_IMGUI_SOURCE_DIR imgui.h
    "${repo_root}/build/_deps/imgui_source-src"
    "${repo_root}/third_party/imgui-1.92.8-docking"
  )
  if(_IMGUI_SOURCE_DIR)
    set(IMGUI_SOURCE_DIR "${_IMGUI_SOURCE_DIR}" CACHE PATH "Dear ImGui source directory" FORCE)
  else()
    message(STATUS "ImGui not found locally; fetching ImGui from GitHub.")
    FetchContent_Declare(imgui_source
      URL "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.6-docking.zip"
    )
    FetchContent_Populate(imgui_source)
    set(IMGUI_SOURCE_DIR "${imgui_source_SOURCE_DIR}" CACHE PATH "Dear ImGui source directory" FORCE)
  endif()
endfunction()

function(resolve_vkb_dependency repo_root)
  set(VKB_SOURCE_DIR "" CACHE PATH "VkBootstrap source directory")

  resolve_existing_path(_VKB_SOURCE_DIR src/VkBootstrap.h
    "${repo_root}/build/_deps/vkb_source-src"
    "${repo_root}/third_party/vkb"
  )
  if(_VKB_SOURCE_DIR)
    set(VKB_SOURCE_DIR "${_VKB_SOURCE_DIR}" CACHE PATH "VkBootstrap source directory" FORCE)
  else()
    message(STATUS "VkBootstrap not found locally; fetching from GitHub.")
    FetchContent_Declare(vkb_source
      URL "https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v1.4.353.zip"
    )
    FetchContent_GetProperties(vkb_source)
    if(NOT vkb_source_POPULATED)
      FetchContent_Populate(vkb_source)
    endif()
    set(VKB_SOURCE_DIR "${vkb_source_SOURCE_DIR}" CACHE PATH "VkBootstrap source directory" FORCE)
  endif()
endfunction()

function(resolve_vma_dependency repo_root)
  set(VMA_INCLUDE_DIR "" CACHE PATH "VMA include directory")

  resolve_existing_path(_VMA_INCLUDE_DIR vk_mem_alloc.h
    "${repo_root}/build/_deps/vma_source-src/include"
    "${repo_root}/third_party/VulkanMemoryAllocator-3.3.0/include"
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
endfunction()

function(resolve_spirv_reflect_dependency repo_root)
  set(SPIRV_REFLECT_SOURCE_DIR "" CACHE PATH "SPIRV-Reflect source directory")

  set(_SPIRV_REFLECT_LOCAL_DIR "${repo_root}/third_party/SPIRV-Reflect-vulkan-sdk-1.4.350.0")
  if(EXISTS "${repo_root}/build/_deps/spirv_reflect_source-src/spirv_reflect.h")
    message(STATUS "Using local SPIRV-Reflect from main build cache.")
    set(SPIRV_REFLECT_SOURCE_DIR "${repo_root}/build/_deps/spirv_reflect_source-src" CACHE PATH "SPIRV-Reflect source directory" FORCE)
  elseif(EXISTS "${_SPIRV_REFLECT_LOCAL_DIR}/spirv_reflect.h")
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
endfunction()

function(resolve_cjson_dependency repo_root)
  set(CJSON_SOURCE_DIR "" CACHE PATH "cJSON source directory")

  resolve_existing_path(_CJSON_SOURCE_DIR cJSON.c
    "${repo_root}/build/_deps/cjson_source-src"
    "${repo_root}/third_party/cjson"
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
endfunction()

function(resolve_mimalloc_dependency repo_root)
  set(MIMALLOC_SOURCE_DIR "" CACHE PATH "mimalloc source directory")
  set(MI_BUILD_SHARED OFF CACHE BOOL "Build mimalloc shared library" FORCE)
  set(MI_BUILD_STATIC ON CACHE BOOL "Build mimalloc static library" FORCE)
  set(MI_BUILD_OBJECT OFF CACHE BOOL "Build mimalloc object library" FORCE)
  set(MI_BUILD_TESTS OFF CACHE BOOL "Build mimalloc tests" FORCE)
  set(MI_OVERRIDE OFF CACHE BOOL "Override the standard malloc interface" FORCE)

  resolve_existing_path(_MIMALLOC_SOURCE_DIR include/mimalloc.h
    "${repo_root}/build/_deps/mimalloc_source-src"
    "${repo_root}/third_party/mimalloc"
  )
  if(_MIMALLOC_SOURCE_DIR)
    set(MIMALLOC_SOURCE_DIR "${_MIMALLOC_SOURCE_DIR}" CACHE PATH "mimalloc source directory" FORCE)
  else()
    message(STATUS "mimalloc not found locally; fetching mimalloc from GitHub.")
    FetchContent_Declare(mimalloc_source
      URL "https://github.com/microsoft/mimalloc/archive/refs/tags/v3.3.2.zip"
    )
    FetchContent_Populate(mimalloc_source)
    set(MIMALLOC_SOURCE_DIR "${mimalloc_source_SOURCE_DIR}" CACHE PATH "mimalloc source directory" FORCE)
  endif()

  add_subdirectory(
    "${MIMALLOC_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/mimalloc-build"
    EXCLUDE_FROM_ALL
  )
  if(TARGET mimalloc-static)
    set(MIMALLOC_TARGET mimalloc-static PARENT_SCOPE)
  elseif(TARGET mimalloc)
    set(MIMALLOC_TARGET mimalloc PARENT_SCOPE)
  else()
    message(FATAL_ERROR "mimalloc target was not generated by the third-party project.")
  endif()
endfunction()

function(resolve_eastl_dependency repo_root)
  set(EASTL_SOURCE_DIR "" CACHE PATH "EASTL source directory")
  set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "Enable generation of build files for benchmark" FORCE)
  set(EASTL_BUILD_TESTS OFF CACHE BOOL "Enable generation of build files for tests" FORCE)
  set(EASTL_STD_ITERATOR_CATEGORY_ENABLED OFF CACHE BOOL "Enable compatibility with std:: iterator categories" FORCE)
  set(EASTL_DISABLE_APRIL_2024_DEPRECATIONS OFF CACHE BOOL "Enable use of API marked for removal in April 2024." FORCE)
  set(EASTL_DISABLE_SEPT_2024_DEPRECATIONS OFF CACHE BOOL "Enable use of API marked for removal in September 2024." FORCE)
  set(EASTL_DISABLE_APRIL_2025_DEPRECATIONS OFF CACHE BOOL "Enable use of API marked for removal in April 2025." FORCE)

  resolve_existing_path(_EASTL_SOURCE_DIR include/EASTL/vector.h
    "${repo_root}/build/_deps/eastl_source-src"
    "${repo_root}/third_party/EASTL"
  )
  if(_EASTL_SOURCE_DIR)
    set(EASTL_SOURCE_DIR "${_EASTL_SOURCE_DIR}" CACHE PATH "EASTL source directory" FORCE)
  else()
    message(STATUS "EASTL not found locally; fetching EASTL from GitHub.")
    FetchContent_Declare(eastl_source
      URL "https://github.com/electronicarts/EASTL/archive/refs/tags/3.27.01.zip"
    )
    FetchContent_Populate(eastl_source)
    set(EASTL_SOURCE_DIR "${eastl_source_SOURCE_DIR}" CACHE PATH "EASTL source directory" FORCE)
  endif()

  add_subdirectory(
    "${EASTL_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/eastl-build"
    EXCLUDE_FROM_ALL
  )
endfunction()
