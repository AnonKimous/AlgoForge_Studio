from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one match in {path}, found {count}: {old[:160]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


# ---------------------------------------------------------------------------
# Python build orchestration
# ---------------------------------------------------------------------------
build_py = ROOT / "buildProject/build.py"
replace_once(
    build_py,
    "from anaconda import require_anaconda\nfrom toolchain import ensure_windows_clang_toolchain\n",
    "from anaconda import require_anaconda\n"
    "from dependencies import inspect_algorithm_requirements\n"
    "from toolchain import ensure_windows_clang_toolchain\n",
)
replace_once(
    build_py,
    "def flatten_runtime_configuration(runtime_dir: Path) -> None:\n"
    "    configuration_dir = runtime_dir / BUILD_CONFIGURATION\n",
    "def flatten_runtime_configuration(runtime_dir: Path, configuration: str) -> None:\n"
    "    configuration_dir = runtime_dir / configuration\n",
)
old_build_algorithm = '''def build_algorithm(toolchain: str, algorithm_name: str) -> None:
    environment = build_environment(toolchain)
    algorithm_dir = find_algorithm(algorithm_name)
    build_dir = ALGORITHM_ROOT / (".build_" + toolchain.lower())
    core_build_dir = ROOT / "build" / toolchain
    targets = algorithm_targets(algorithm_dir)
    shutil.rmtree(build_dir, ignore_errors=True)
    shutil.rmtree(RUNTIME_ROOT / algorithm_dir.relative_to(SOURCE_ROOT), ignore_errors=True)
    RUNTIME_ROOT.mkdir(parents=True, exist_ok=True)
    generator, generator_args = configure_generator(toolchain)
    configure = [
        "cmake", "-S", str(ALGORITHM_ROOT), "-B", str(build_dir), "--fresh", "-G", generator,
        *generator_args,
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file(toolchain)}",
        f"-DALGORITHM_LIBRARY_SOURCE_ROOT={SOURCE_ROOT}",
        f"-DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT={RUNTIME_ROOT}",
        f"-DALGORITHM_LIBRARY_SELECTED_ROOT={algorithm_dir}",
        f"-DCORE_BUILD_DIR={core_build_dir}",
        "-DBUILD_ALGORITHM_SAMPLE_PLUGIN=ON",
    ]
    if toolchain == "OpenSource":
        configure.append(f"-DCMAKE_MAKE_PROGRAM={environment['NINJA_EXE']}")
        configure.append(f"-DALGOFORGE_CLANG_CL={environment['CXX']}")
    run(configure, environment)
    for target in targets:
        run(["cmake", "--build", str(build_dir), "--config", BUILD_CONFIGURATION, "--target", target, "--parallel"], environment)
    runtime_algorithm_root = RUNTIME_ROOT / algorithm_dir.relative_to(SOURCE_ROOT)
    for manifest in sorted(algorithm_dir.rglob("manifest.json")):
        runtime_directory = runtime_algorithm_root / manifest.parent.relative_to(algorithm_dir)
        flatten_runtime_configuration(runtime_directory)
    package_runtime(
      algorithm_dir,
      runtime_algorithm_root,
        BUILD_CONFIGURATION,
        SOURCE_ROOT,
    )
    export_sdk(toolchain)
    shutil.rmtree(build_dir)
'''
new_build_algorithm = '''def _environment_feature(environment: dict[str, str], name: str, default: bool) -> bool:
    raw_value = environment.get(name)
    if raw_value is None:
        return default
    normalized = raw_value.strip().lower()
    if normalized in {"1", "on", "true", "yes"}:
        return True
    if normalized in {"0", "off", "false", "no"}:
        return False
    raise RuntimeError(f"{name} must be ON or OFF, got {raw_value!r}.")


def build_algorithm(toolchain: str, algorithm_name: str) -> None:
    environment = build_environment(toolchain)
    algorithm_dir = find_algorithm(algorithm_name)
    requirements = inspect_algorithm_requirements([algorithm_name])
    physx_enabled = _environment_feature(
        environment,
        "ALGOFORGE_ENABLE_PHYSX",
        requirements.physx,
    )
    cuda_enabled = _environment_feature(environment, "ALGOFORGE_ENABLE_CUDA", False)
    if requirements.physx and not physx_enabled:
        raise RuntimeError(
            f"Algorithm {algorithm_name!r} requires PhysX, but ALGOFORGE_ENABLE_PHYSX is OFF."
        )

    # PhysX's official FetchContent entry point uses the lowercase custom
    # configurations debug/checked/profile/release. Other algorithms retain the
    # project's standard RelWithDebInfo package configuration.
    configuration = "release" if requirements.physx else BUILD_CONFIGURATION

    build_dir = ALGORITHM_ROOT / (".build_" + toolchain.lower())
    core_build_dir = ROOT / "build" / toolchain
    targets = algorithm_targets(algorithm_dir)
    shutil.rmtree(build_dir, ignore_errors=True)
    shutil.rmtree(RUNTIME_ROOT / algorithm_dir.relative_to(SOURCE_ROOT), ignore_errors=True)
    RUNTIME_ROOT.mkdir(parents=True, exist_ok=True)
    generator, generator_args = configure_generator(toolchain)
    configure = [
        "cmake", "-S", str(ALGORITHM_ROOT), "-B", str(build_dir), "--fresh", "-G", generator,
        *generator_args,
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file(toolchain)}",
        f"-DALGORITHM_LIBRARY_SOURCE_ROOT={SOURCE_ROOT}",
        f"-DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT={RUNTIME_ROOT}",
        f"-DALGORITHM_LIBRARY_SELECTED_ROOT={algorithm_dir}",
        f"-DCORE_BUILD_DIR={core_build_dir}",
        "-DBUILD_ALGORITHM_SAMPLE_PLUGIN=ON",
        f"-DALGOFORGE_ENABLE_PHYSX={'ON' if physx_enabled else 'OFF'}",
        f"-DALGOFORGE_ENABLE_CUDA={'ON' if cuda_enabled else 'OFF'}",
        f"-DBUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS={'ON' if cuda_enabled else 'OFF'}",
    ]
    physx_root = environment.get("ALGOFORGE_PHYSX_ROOT", "").strip()
    if physx_root:
        configure.append(f"-DALGOFORGE_PHYSX_ROOT={physx_root}")
    cuda_root = environment.get("CUDAToolkit_ROOT", "").strip()
    if cuda_root:
        configure.append(f"-DCUDAToolkit_ROOT={cuda_root}")
    cuda_compiler = environment.get("CUDACXX", "").strip()
    if cuda_compiler:
        configure.append(f"-DCMAKE_CUDA_COMPILER={cuda_compiler}")
    if toolchain == "OpenSource":
        configure.append(f"-DCMAKE_MAKE_PROGRAM={environment['NINJA_EXE']}")
        configure.append(f"-DALGOFORGE_CLANG_CL={environment['CXX']}")
    run(configure, environment)
    for target in targets:
        run([
            "cmake", "--build", str(build_dir), "--config", configuration,
            "--target", target, "--parallel",
        ], environment)
    runtime_algorithm_root = RUNTIME_ROOT / algorithm_dir.relative_to(SOURCE_ROOT)
    for manifest in sorted(algorithm_dir.rglob("manifest.json")):
        runtime_directory = runtime_algorithm_root / manifest.parent.relative_to(algorithm_dir)
        flatten_runtime_configuration(runtime_directory, configuration)
    package_runtime(
        algorithm_dir,
        runtime_algorithm_root,
        configuration,
        SOURCE_ROOT,
    )
    export_sdk(toolchain)
    shutil.rmtree(build_dir)
'''
replace_once(build_py, old_build_algorithm, new_build_algorithm)


# ---------------------------------------------------------------------------
# Algorithm CMake dependency selection and package output
# ---------------------------------------------------------------------------
algorithm_cmake = ROOT / "algorithmLib/CMakeLists.txt"
replace_once(
    algorithm_cmake,
    '  if(_selected_algorithm_relative_root STREQUAL "" OR\n'
    '     _selected_algorithm_relative_root MATCHES "^\\.\\.")\n'
    '    message(FATAL_ERROR\n'
    '      "ALGORITHM_LIBRARY_SELECTED_ROOT must be inside ALGORITHM_LIBRARY_SOURCE_ROOT: "\n'
    '      "${ALGORITHM_LIBRARY_SELECTED_ROOT}")\n'
    '  endif()\n',
    '  file(REAL_PATH "${ALGORITHM_LIBRARY_SOURCE_ROOT}" _algorithm_source_root_real)\n'
    '  file(REAL_PATH "${ALGORITHM_LIBRARY_SELECTED_ROOT}" _algorithm_selected_root_real)\n'
    '  cmake_path(IS_PREFIX _algorithm_source_root_real\n'
    '    "${_algorithm_selected_root_real}" NORMALIZE _algorithm_selected_is_inside)\n'
    '  if(NOT _algorithm_selected_is_inside)\n'
    '    message(FATAL_ERROR\n'
    '      "ALGORITHM_LIBRARY_SELECTED_ROOT must be inside ALGORITHM_LIBRARY_SOURCE_ROOT: "\n'
    '      "${ALGORITHM_LIBRARY_SELECTED_ROOT}")\n'
    '  endif()\n',
)
replace_once(
    algorithm_cmake,
    'option(BUILD_ALGORITHM_SAMPLE_PLUGIN "Build the temporary sample algorithm plugin." OFF)\n'
    'option(BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS "Build algorithm plugin algos that require CUDA sources." OFF)\n'
    'set(CORE_BUILD_DIR "${REPO_ROOT}/build" CACHE PATH "Core build directory containing the base libraries for algorithm plugins")\n'
    'set(PHYSX_INCLUDE_DIR "$ENV{ALGOFORGE_PHYSX_INCLUDE_DIR}" CACHE PATH "PhysX include directory for compatibility algorithm plugins")\n'
    'set(PHYSX_RUNTIME_DIR "$ENV{ALGOFORGE_PHYSX_RUNTIME_DIR}" CACHE PATH "PhysX runtime DLL directory for compatibility algorithm plugins")\n',
    'option(BUILD_ALGORITHM_SAMPLE_PLUGIN "Build the temporary sample algorithm plugin." OFF)\n'
    'option(BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS "Build algorithm plugin algos that require CUDA sources." OFF)\n'
    'option(ALGOFORGE_ENABLE_CUDA "Enable CUDA sources for selected algorithm packages." OFF)\n'
    'option(ALGOFORGE_ENABLE_PHYSX "Enable PhysX for selected algorithm packages." OFF)\n'
    'set(ALGOFORGE_PHYSX_ROOT "$ENV{ALGOFORGE_PHYSX_ROOT}" CACHE PATH\n'
    '  "Optional local PhysX SDK source directory")\n'
    'set(CORE_BUILD_DIR "${REPO_ROOT}/build" CACHE PATH "Core build directory containing the base libraries for algorithm plugins")\n',
)
# Add selected dependency probing before the common include list is consumed by targets.
replace_once(
    algorithm_cmake,
    'require_file("${ALGORITHM_LIBRARY_SOURCE_ROOT}/algorithm_plugin_api.h" "Set ALGORITHM_LIBRARY_SOURCE_ROOT to the mirrored algorithm source root.")\n\n'
    'set(PROJECT_INCLUDE_DIRS\n',
    'require_file("${ALGORITHM_LIBRARY_SOURCE_ROOT}/algorithm_plugin_api.h" "Set ALGORITHM_LIBRARY_SOURCE_ROOT to the mirrored algorithm source root.")\n\n'
    'set(_algoforge_dependency_probe_root "${ALGORITHM_LIBRARY_SOURCE_ROOT}")\n'
    'if(ALGORITHM_LIBRARY_SELECTED_ROOT)\n'
    '  set(_algoforge_dependency_probe_root "${ALGORITHM_LIBRARY_SELECTED_ROOT}")\n'
    'endif()\n'
    'file(GLOB_RECURSE _algoforge_dependency_probe_sources CONFIGURE_DEPENDS\n'
    '  "${_algoforge_dependency_probe_root}/*.c"\n'
    '  "${_algoforge_dependency_probe_root}/*.cc"\n'
    '  "${_algoforge_dependency_probe_root}/*.cpp"\n'
    '  "${_algoforge_dependency_probe_root}/*.cxx"\n'
    '  "${_algoforge_dependency_probe_root}/*.h"\n'
    '  "${_algoforge_dependency_probe_root}/*.hpp"\n'
    '  "${_algoforge_dependency_probe_root}/*.cu"\n'
    ')\n'
    'set(ALGOFORGE_SELECTED_REQUIRES_PHYSX OFF)\n'
    'set(ALGOFORGE_SELECTED_HAS_CUDA OFF)\n'
    'foreach(_algoforge_probe_source IN LISTS _algoforge_dependency_probe_sources)\n'
    '  if(_algoforge_probe_source MATCHES "\\.cu$")\n'
    '    set(ALGOFORGE_SELECTED_HAS_CUDA ON)\n'
    '  endif()\n'
    '  if(NOT ALGOFORGE_SELECTED_REQUIRES_PHYSX)\n'
    '    file(STRINGS "${_algoforge_probe_source}" _algoforge_physx_reference\n'
    '      REGEX "PxPhysicsAPI\\.h" LIMIT_COUNT 1)\n'
    '    if(_algoforge_physx_reference)\n'
    '      set(ALGOFORGE_SELECTED_REQUIRES_PHYSX ON)\n'
    '    endif()\n'
    '  endif()\n'
    'endforeach()\n\n'
    'if(ALGOFORGE_SELECTED_REQUIRES_PHYSX)\n'
    '  if(NOT ALGOFORGE_ENABLE_PHYSX)\n'
    '    message(FATAL_ERROR\n'
    '      "The selected algorithm package requires PhysX. Re-run the boot script "\n'
    '      "with --physx auto or --physx on.")\n'
    '  endif()\n'
    '  include("${REPO_ROOT}/cmake/resolve_physx_dependency.cmake")\n'
    '  resolve_algoforge_physx_dependency()\n'
    'endif()\n\n'
    'if(ALGOFORGE_SELECTED_HAS_CUDA AND ALGOFORGE_ENABLE_CUDA)\n'
    '  include(CheckLanguage)\n'
    '  check_language(CUDA)\n'
    '  if(NOT CMAKE_CUDA_COMPILER)\n'
    '    message(FATAL_ERROR\n'
    '      "CUDA sources were enabled, but CMake could not find nvcc. "\n'
    '      "Set CUDAToolkit_ROOT/CUDACXX or use --cuda off.")\n'
    '  endif()\n'
    '  enable_language(CUDA)\n'
    '  find_package(CUDAToolkit REQUIRED)\n'
    '  set(BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS ON CACHE BOOL\n'
    '    "Build algorithm CUDA sources" FORCE)\n'
    'else()\n'
    '  set(BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS OFF CACHE BOOL\n'
    '    "Build algorithm CUDA sources" FORCE)\n'
    'endif()\n\n'
    'set(PROJECT_INCLUDE_DIRS\n',
)
# Probe the concrete plugin instead of inferring PhysX from a package name.
replace_once(
    algorithm_cmake,
    '  list(SORT plugin_sources_for_build)\n'
    '  list(LENGTH plugin_sources_for_build plugin_source_count)\n',
    '  list(SORT plugin_sources_for_build)\n'
    '  list(LENGTH plugin_sources_for_build plugin_source_count)\n'
    '  set(plugin_requires_physx OFF)\n'
    '  foreach(plugin_source IN LISTS plugin_sources_for_build)\n'
    '    if(NOT plugin_requires_physx)\n'
    '      file(STRINGS "${plugin_source}" plugin_physx_reference\n'
    '        REGEX "PxPhysicsAPI\\.h" LIMIT_COUNT 1)\n'
    '      if(plugin_physx_reference)\n'
    '        set(plugin_requires_physx ON)\n'
    '      endif()\n'
    '    endif()\n'
    '  endforeach()\n',
)
old_physx_block = '''    if(algorithm_name MATCHES "physx|physics_sdk_compat_demo")
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
'''
new_physx_block = '''    if(plugin_requires_physx)
      if(NOT TARGET physx_lib)
        message(FATAL_ERROR
          "The plugin includes PxPhysicsAPI.h, but the PhysX CMake target is unavailable.")
      endif()
      target_link_libraries("${algorithm_target_name}_plugin" PRIVATE physx_lib)
      set(physx_license_relative_path "licenses/PhysX-LICENSE.md")
      add_custom_command(TARGET "${algorithm_target_name}_plugin" POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
          "${algorithm_output_dir}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ALGOFORGE_PHYSX_LICENSE_FILE}"
          "${algorithm_output_dir}/${physx_license_relative_path}"
        VERBATIM
      )
      list(APPEND algorithm_runtime_files "${physx_license_relative_path}")
    endif()
'''
replace_once(algorithm_cmake, old_physx_block, new_physx_block)
replace_once(
    algorithm_cmake,
    '    if(plugin_has_cuda_source AND BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS)\n'
    '      target_link_libraries("${algorithm_target_name}_plugin" PRIVATE CUDA::cudart)\n'
    '    endif()\n',
    '    if(plugin_has_cuda_source AND BUILD_ALGORITHM_CUDA_SAMPLE_PLUGIN_ALGOS)\n'
    '      target_link_libraries("${algorithm_target_name}_plugin" PRIVATE CUDA::cudart)\n'
    '      set_target_properties("${algorithm_target_name}_plugin" PROPERTIES\n'
    '        CUDA_STANDARD 17\n'
    '        CUDA_STANDARD_REQUIRED ON\n'
    '      )\n'
    '    endif()\n',
)


# ---------------------------------------------------------------------------
# PhysX compatibility algorithm: link through CMake, no platform loader API.
# ---------------------------------------------------------------------------
plugin_cpp = ROOT / "algorithmLib/algorithmSrc/norm/physics_sdk_compat_demo/physx_compatibility_demo_plugin.cpp"
replace_once(
    plugin_cpp,
    '#define PX_SIMD_DISABLED 1\n#include <PxPhysicsAPI.h>\n',
    '#include <PxPhysicsAPI.h>\n',
)
replace_once(plugin_cpp, '#include <vector>\n#include <windows.h>\n', '#include <vector>\n#include <stdexcept>\n')
replace_once(
    plugin_cpp,
    '    common_module_ = LoadPhysXModule(L"PhysXCommon_64.dll");\n'
    '    foundation_module_ = LoadPhysXModule(L"PhysXFoundation_64.dll");\n'
    '    physics_module_ = LoadPhysXModule(L"PhysX_64.dll");\n'
    '    foundation_ = CreateFoundation();\n',
    '    foundation_ = CreateFoundation();\n',
)
replace_once(
    plugin_cpp,
    '    FreeLibrary(physics_module_);\n'
    '    FreeLibrary(foundation_module_);\n'
    '    FreeLibrary(common_module_);\n',
    '',
)
replace_once(
    plugin_cpp,
    '''  static std::wstring PluginDirectory() {
    wchar_t module_path[MAX_PATH]{};
    HMODULE module{};
    GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&PluginDirectory),
      &module);
    GetModuleFileNameW(module, module_path, MAX_PATH);
    std::wstring path(module_path);
    path.resize(path.find_last_of(L"\\/"));
    return path;
  }

  static HMODULE LoadPhysXModule(const wchar_t* name) {
    return LoadLibraryW((PluginDirectory() + L"\\" + name).c_str());
  }

''',
    '',
)
replace_once(
    plugin_cpp,
    '''  PxFoundation* CreateFoundation() {
    trace_ << "foundation.begin\n" << std::flush;
    using CreateFoundationFn = PxFoundation* (PX_CALL_CONV*)(PxU32, PxAllocatorCallback&, PxErrorCallback&);
    const auto create_foundation = reinterpret_cast<CreateFoundationFn>(GetProcAddress(foundation_module_, "PxCreateFoundation"));
    PxFoundation* foundation = create_foundation(PX_PHYSICS_VERSION, allocator_, error_callback_);
    trace_ << "foundation.end\n" << std::flush;
    return foundation;
  }
''',
    '''  PxFoundation* CreateFoundation() {
    trace_ << "foundation.begin\n" << std::flush;
    PxFoundation* foundation = PxCreateFoundation(
      PX_PHYSICS_VERSION,
      allocator_,
      error_callback_);
    if (!foundation) {
      throw std::runtime_error("PxCreateFoundation failed.");
    }
    trace_ << "foundation.end\n" << std::flush;
    return foundation;
  }
''',
)
replace_once(
    plugin_cpp,
    '''  PxPhysics* CreatePhysics() {
    trace_ << "physics.begin\n" << std::flush;
    using CreatePhysicsFn = PxPhysics* (PX_CALL_CONV*)(PxU32, PxFoundation&, const PxTolerancesScale&, bool, PxPvd*, PxOmniPvd*);
    const auto create_physics = reinterpret_cast<CreatePhysicsFn>(GetProcAddress(physics_module_, "PxCreatePhysics"));
    PxPhysics* physics = create_physics(PX_PHYSICS_VERSION, *foundation_, PxTolerancesScale(), false, nullptr, nullptr);
    trace_ << "physics.end\n" << std::flush;
    return physics;
  }
''',
    '''  PxPhysics* CreatePhysics() {
    trace_ << "physics.begin\n" << std::flush;
    PxPhysics* physics = PxCreatePhysics(
      PX_PHYSICS_VERSION,
      *foundation_,
      PxTolerancesScale(),
      false,
      nullptr,
      nullptr);
    if (!physics) {
      throw std::runtime_error("PxCreatePhysics failed.");
    }
    trace_ << "physics.end\n" << std::flush;
    return physics;
  }
''',
)
replace_once(
    plugin_cpp,
    '  HMODULE common_module_;\n'
    '  HMODULE foundation_module_;\n'
    '  HMODULE physics_module_;\n',
    '',
)

# Documentation must describe one portable package contract, not Windows DLLs.
physx_doc = ROOT / "algorithmLib/algorithmSrc/norm/physics_sdk_compat_demo/physx_compatibility_demo.md"
physx_doc.write_text('''# PhysX compatibility demo

This package is a small but real PhysX rigid-body compatibility example.

The plugin owns a zero-gravity PhysX scene containing a static ground actor and two dynamic box actors. At the beginning of every round, the algorithm generates a random collision point, two random starting positions, and two different velocities aimed at that same point with the same arrival time. This preserves randomized motion while guaranteeing a collision. Each Compatibility execution advances PhysX by one fixed time step, fetches the results, reads both actors' poses and velocities, and writes two body states into the `a1` algorithm array. Contact relative speed drives a small visual squash-and-stretch deformation in the state written for the result-render stage. The round is regenerated every five seconds.

The mainline does not know about PhysX types or physics objects. The algorithm build resolves the official PhysX source through CMake, links the CPU SDK statically into the platform plugin module, and packages that module together with shaders, the manifest, and the PhysX license. The `.algo` format therefore remains identical on every platform; only the CMake-selected plugin binary differs (`.dll`, `.so`, or the platform equivalent).

Build through the boot entry point so dependency detection is applied:

```bash
python boot/booterNinjaClang.py physics_sdk_compat_demo --physx auto
```

A local PhysX source tree can be used by setting `ALGOFORGE_PHYSX_ROOT` to the SDK directory containing `include/PxPhysicsAPI.h` and `CMakeLists.txt`. Without that variable, CMake fetches the pinned official PhysX release. Run the package with the `compatibility` execution preference.
''', encoding="utf-8")
