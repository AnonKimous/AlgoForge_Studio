include_guard(GLOBAL)

include(FetchContent)

function(resolve_algoforge_physx_dependency)
  if(TARGET physx_lib)
    return()
  endif()

  set(ALGOFORGE_PHYSX_VERSION "5.9.0" CACHE STRING "PhysX SDK version used by AlgoForge")
  set(ALGOFORGE_PHYSX_TAG
    "110.1-omni-and-physx-5.9.0"
    CACHE STRING
    "NVIDIA-Omniverse/PhysX tag used by AlgoForge")
  set(ALGOFORGE_PHYSX_ROOT
    "$ENV{ALGOFORGE_PHYSX_ROOT}"
    CACHE PATH
    "Optional local PhysX SDK source directory containing include/PxPhysicsAPI.h")

  # The compatibility demo uses CPU rigid bodies only. Build PhysX statically
  # into the algorithm module so a .algo package does not need to guess or
  # hard-code platform shared-library names.
  set(PX_GENERATE_GPU_PROJECTS OFF CACHE BOOL "Build PhysX GPU projects" FORCE)
  set(PX_GENERATE_GPU_PROJECTS_ONLY OFF CACHE BOOL "Build only PhysX GPU projects" FORCE)
  set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "Build static PhysX libraries" FORCE)
  set(PX_GENERATE_GPU_STATIC_LIBRARIES OFF CACHE BOOL "Build static PhysX GPU library" FORCE)
  set(PX_BUILDSNIPPETS OFF CACHE BOOL "Build PhysX snippets" FORCE)
  set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "Build PhysX OmniPVD runtime" FORCE)
  set(PX_SCALAR_MATH OFF CACHE BOOL "Use scalar PhysX math" FORCE)
  set(PHYSX_PRESET "" CACHE STRING "PhysX preset" FORCE)

  if(ALGOFORGE_PHYSX_ROOT)
    get_filename_component(_algoforge_physx_root
      "${ALGOFORGE_PHYSX_ROOT}" ABSOLUTE)
    if(NOT EXISTS "${_algoforge_physx_root}/include/PxPhysicsAPI.h" OR
       NOT EXISTS "${_algoforge_physx_root}/CMakeLists.txt")
      message(FATAL_ERROR
        "ALGOFORGE_PHYSX_ROOT must point to the PhysX SDK directory containing "
        "include/PxPhysicsAPI.h and CMakeLists.txt: ${_algoforge_physx_root}")
    endif()
    message(STATUS "Using local PhysX SDK source: ${_algoforge_physx_root}")
    FetchContent_Declare(algoforge_physx_source
      SOURCE_DIR "${_algoforge_physx_root}"
    )
  else()
    message(STATUS
      "Fetching NVIDIA PhysX SDK ${ALGOFORGE_PHYSX_VERSION} "
      "(${ALGOFORGE_PHYSX_TAG})")
    FetchContent_Declare(algoforge_physx_source
      URL
        "https://github.com/NVIDIA-Omniverse/PhysX/archive/refs/tags/${ALGOFORGE_PHYSX_TAG}.zip"
      SOURCE_SUBDIR physx
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
  endif()

  FetchContent_MakeAvailable(algoforge_physx_source)
  if(NOT TARGET physx_lib)
    message(FATAL_ERROR
      "PhysX source configuration completed without creating the physx_lib target.")
  endif()

  if(ALGOFORGE_PHYSX_ROOT)
    set(_algoforge_physx_sdk_root "${_algoforge_physx_root}")
    get_filename_component(_algoforge_physx_repository_root
      "${_algoforge_physx_sdk_root}" DIRECTORY)
  else()
    set(_algoforge_physx_repository_root "${algoforge_physx_source_SOURCE_DIR}")
    set(_algoforge_physx_sdk_root
      "${algoforge_physx_source_SOURCE_DIR}/physx")
  endif()

  set(ALGOFORGE_PHYSX_SDK_ROOT
    "${_algoforge_physx_sdk_root}"
    CACHE INTERNAL "Resolved PhysX SDK source root" FORCE)
  set(ALGOFORGE_PHYSX_LICENSE_FILE
    "${_algoforge_physx_repository_root}/LICENSE.md"
    CACHE INTERNAL "Resolved PhysX license file" FORCE)

  if(NOT EXISTS "${ALGOFORGE_PHYSX_LICENSE_FILE}")
    message(FATAL_ERROR
      "The resolved PhysX source does not contain LICENSE.md: "
      "${ALGOFORGE_PHYSX_LICENSE_FILE}")
  endif()
endfunction()
