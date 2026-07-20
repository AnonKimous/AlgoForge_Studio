from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
path = ROOT / "algorithmLib" / "CMakeLists.txt"
text = path.read_text(encoding="utf-8")
old = '''      target_link_libraries("${algorithm_target_name}_plugin" PRIVATE physx_lib)
      set(physx_license_relative_path "licenses/PhysX-LICENSE.md")
'''
new = '''      target_link_libraries("${algorithm_target_name}_plugin" PRIVATE physx_lib)
      target_compile_definitions("${algorithm_target_name}_plugin" PRIVATE
        PX_PHYSX_STATIC_LIB=1
      )
      set(physx_license_relative_path "licenses/PhysX-LICENSE.md")
'''
if old not in text:
    raise RuntimeError("The portable PhysX link block was not produced by the primary patch.")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
