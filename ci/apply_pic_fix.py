from __future__ import annotations

from pathlib import Path

path = Path(__file__).resolve().parent.parent / "algorithmLib" / "CMakeLists.txt"
text = path.read_text(encoding="utf-8")
old = '''  target_compile_definitions(algorithm_plugin_support_mirror PUBLIC
    ALGOFORGE_PROJECT_ROOT="${REPO_ROOT}"
  )
endif()
'''
new = '''  target_compile_definitions(algorithm_plugin_support_mirror PUBLIC
    ALGOFORGE_PROJECT_ROOT="${REPO_ROOT}"
  )
  set_target_properties(algorithm_plugin_support_mirror PROPERTIES
    POSITION_INDEPENDENT_CODE ON
  )
endif()
'''
if text.count(old) != 1:
    raise RuntimeError("algorithm_plugin_support_mirror definition block was not found exactly once")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
