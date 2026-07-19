from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from anaconda import require_anaconda
from build import SOURCE_ROOT, build_algorithm


def algorithm_names() -> list[str]:
    names = set()
    for manifest in SOURCE_ROOT.rglob("manifest.json"):
        relative_parts = manifest.parent.relative_to(SOURCE_ROOT).parts
        if len(relative_parts) >= 2:
            names.add(relative_parts[1])
    return sorted(names)


def main() -> int:
    require_anaconda()
    toolchain = sys.argv[1]
    for algorithm_name in algorithm_names():
        print(f"Building algorithm: {algorithm_name}", flush=True)
        build_algorithm(toolchain, algorithm_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
