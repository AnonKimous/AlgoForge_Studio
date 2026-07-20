from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one match in {path}, found {count}: {old!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


build_py = ROOT / "buildProject/build.py"
replace_once(
    build_py,
    '        f"-DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT={RUNTIME_ROOT}",\n'
    '        f"-DCORE_BUILD_DIR={core_build_dir}",\n',
    '        f"-DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT={RUNTIME_ROOT}",\n'
    '        f"-DALGORITHM_LIBRARY_SELECTED_ROOT={algorithm_dir}",\n'
    '        f"-DCORE_BUILD_DIR={core_build_dir}",\n',
)

algorithm_cmake = ROOT / "algorithmLib/CMakeLists.txt"
replace_once(
    algorithm_cmake,
    'set(ALGORITHM_LIBRARY_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmSrc" CACHE PATH "Root directory containing algorithm algo source files")\n'
    'set(ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmruntimeLib" CACHE PATH "Root directory for built algorithm runtime artifacts")\n'
    'file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_SOURCE_ROOT}" ALGORITHM_LIBRARY_SOURCE_ROOT)\n'
    'file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT}" ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT)\n',
    'set(ALGORITHM_LIBRARY_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmSrc" CACHE PATH "Root directory containing algorithm algo source files")\n'
    'set(ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/algorithmruntimeLib" CACHE PATH "Root directory for built algorithm runtime artifacts")\n'
    'set(ALGORITHM_LIBRARY_SELECTED_ROOT "" CACHE PATH "Optional source package root to configure in isolation")\n'
    'file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_SOURCE_ROOT}" ALGORITHM_LIBRARY_SOURCE_ROOT)\n'
    'file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT}" ALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT)\n'
    'if(ALGORITHM_LIBRARY_SELECTED_ROOT)\n'
    '  file(TO_CMAKE_PATH "${ALGORITHM_LIBRARY_SELECTED_ROOT}" ALGORITHM_LIBRARY_SELECTED_ROOT)\n'
    '  file(RELATIVE_PATH _selected_algorithm_relative_root\n'
    '    "${ALGORITHM_LIBRARY_SOURCE_ROOT}"\n'
    '    "${ALGORITHM_LIBRARY_SELECTED_ROOT}")\n'
    '  if(_selected_algorithm_relative_root STREQUAL "" OR\n'
    '     _selected_algorithm_relative_root MATCHES "^\\.\\.")\n'
    '    message(FATAL_ERROR\n'
    '      "ALGORITHM_LIBRARY_SELECTED_ROOT must be inside ALGORITHM_LIBRARY_SOURCE_ROOT: "\n'
    '      "${ALGORITHM_LIBRARY_SELECTED_ROOT}")\n'
    '  endif()\n'
    '  message(STATUS\n'
    '    "Configuring selected algorithm package tree: ${_selected_algorithm_relative_root}")\n'
    'endif()\n',
)
replace_once(
    algorithm_cmake,
    'file(GLOB_RECURSE ALGORITHM_PACKAGE_MANIFESTS CONFIGURE_DEPENDS\n'
    '  "${ALGORITHM_LIBRARY_SOURCE_ROOT}/manifest.json"\n'
    ')\n',
    'if(ALGORITHM_LIBRARY_SELECTED_ROOT)\n'
    '  file(GLOB_RECURSE ALGORITHM_PACKAGE_MANIFESTS CONFIGURE_DEPENDS\n'
    '    "${ALGORITHM_LIBRARY_SELECTED_ROOT}/manifest.json"\n'
    '  )\n'
    'else()\n'
    '  file(GLOB_RECURSE ALGORITHM_PACKAGE_MANIFESTS CONFIGURE_DEPENDS\n'
    '    "${ALGORITHM_LIBRARY_SOURCE_ROOT}/manifest.json"\n'
    '  )\n'
    'endif()\n',
)

runner_header = ROOT / "src/debug_tool/runner_control_socket.h"
replace_once(
    runner_header,
    '#include <algorithm>\n#include <cctype>\n',
    '#include <algorithm>\n#include <array>\n#include <cctype>\n',
)
