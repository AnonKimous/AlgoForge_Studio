from __future__ import annotations

from pathlib import Path

path = Path(__file__).resolve().parent / "apply_dependency_bootstrap_and_physx.py"
text = path.read_text(encoding="utf-8")
start_marker = '''replace_once(
    plugin_cpp,
    ''' + "'''  static std::wstring PluginDirectory() {"
end_marker = '''replace_once(
    plugin_cpp,
    ''' + "'''  PxFoundation* CreateFoundation() {"
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = '''plugin_text = plugin_cpp.read_text(encoding="utf-8")
loader_begin = plugin_text.index("  static std::wstring PluginDirectory() {")
loader_end = plugin_text.index("  PxFoundation* CreateFoundation()", loader_begin)
plugin_cpp.write_text(
    plugin_text[:loader_begin] + plugin_text[loader_end:],
    encoding="utf-8",
)
'''
path.write_text(text[:start] + replacement + text[end:], encoding="utf-8")
