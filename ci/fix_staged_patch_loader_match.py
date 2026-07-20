from __future__ import annotations

from pathlib import Path


path = Path(__file__).resolve().parent / "apply_dependency_bootstrap_and_physx.py"
text = path.read_text(encoding="utf-8")


def replace_section(source: str, start_marker: str, end_marker: str, replacement: str) -> str:
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[:start] + replacement + source[end:]


loader_marker = "replace_once(\n    plugin_cpp,\n    '''  static std::wstring PluginDirectory() {"
foundation_marker = "replace_once(\n    plugin_cpp,\n    '''  PxFoundation* CreateFoundation() {"
physics_marker = "replace_once(\n    plugin_cpp,\n    '''  PxPhysics* CreatePhysics() {"
members_marker = "replace_once(\n    plugin_cpp,\n    '  HMODULE common_module_;\\n'"

loader_code = (
    'plugin_text = plugin_cpp.read_text(encoding="utf-8")\n'
    'loader_begin = plugin_text.index("  static std::wstring PluginDirectory() {")\n'
    'loader_end = plugin_text.index("  PxFoundation* CreateFoundation()", loader_begin)\n'
    'plugin_cpp.write_text(\n'
    '    plugin_text[:loader_begin] + plugin_text[loader_end:],\n'
    '    encoding="utf-8",\n'
    ')\n'
)
text = replace_section(text, loader_marker, foundation_marker, loader_code)

foundation_code = (
    'plugin_text = plugin_cpp.read_text(encoding="utf-8")\n'
    'foundation_begin = plugin_text.index("  PxFoundation* CreateFoundation() {")\n'
    'foundation_end = plugin_text.index("  PxPhysics* CreatePhysics()", foundation_begin)\n'
    'foundation_replacement = (\n'
    "    '  PxFoundation* CreateFoundation() {\\n'\n"
    "    '    trace_ << \"foundation.begin\\\\n\" << std::flush;\\n'\n"
    "    '    PxFoundation* foundation = PxCreateFoundation(\\n'\n"
    "    '      PX_PHYSICS_VERSION,\\n'\n"
    "    '      allocator_,\\n'\n"
    "    '      error_callback_);\\n'\n"
    "    '    if (!foundation) {\\n'\n"
    "    '      throw std::runtime_error(\"PxCreateFoundation failed.\");\\n'\n"
    "    '    }\\n'\n"
    "    '    trace_ << \"foundation.end\\\\n\" << std::flush;\\n'\n"
    "    '    return foundation;\\n'\n"
    "    '  }\\n'\n"
    ')\n'
    'plugin_cpp.write_text(\n'
    '    plugin_text[:foundation_begin] + foundation_replacement + plugin_text[foundation_end:],\n'
    '    encoding="utf-8",\n'
    ')\n'
)
text = replace_section(text, foundation_marker, physics_marker, foundation_code)

physics_code = (
    'plugin_text = plugin_cpp.read_text(encoding="utf-8")\n'
    'physics_begin = plugin_text.index("  PxPhysics* CreatePhysics() {")\n'
    'physics_end = plugin_text.index("  PxScene* CreateScene()", physics_begin)\n'
    'physics_replacement = (\n'
    "    '  PxPhysics* CreatePhysics() {\\n'\n"
    "    '    trace_ << \"physics.begin\\\\n\" << std::flush;\\n'\n"
    "    '    PxPhysics* physics = PxCreatePhysics(\\n'\n"
    "    '      PX_PHYSICS_VERSION,\\n'\n"
    "    '      *foundation_,\\n'\n"
    "    '      PxTolerancesScale(),\\n'\n"
    "    '      false,\\n'\n"
    "    '      nullptr,\\n'\n"
    "    '      nullptr);\\n'\n"
    "    '    if (!physics) {\\n'\n"
    "    '      throw std::runtime_error(\"PxCreatePhysics failed.\");\\n'\n"
    "    '    }\\n'\n"
    "    '    trace_ << \"physics.end\\\\n\" << std::flush;\\n'\n"
    "    '    return physics;\\n'\n"
    "    '  }\\n\\n'\n"
    ')\n'
    'plugin_cpp.write_text(\n'
    '    plugin_text[:physics_begin] + physics_replacement + plugin_text[physics_end:],\n'
    '    encoding="utf-8",\n'
    ')\n'
)
text = replace_section(text, physics_marker, members_marker, physics_code)

path.write_text(text, encoding="utf-8")
