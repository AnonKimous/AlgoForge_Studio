from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "buildProject"))
from anaconda import require_anaconda


def main() -> int:
    require_anaconda()
    python = sys.executable
    subprocess.run([python, str(ROOT / "buildProject" / "Microsoft" / "build_mainline.py")], cwd=ROOT, check=True)
    if len(sys.argv) > 1:
        subprocess.run([python, str(ROOT / "buildProject" / "Microsoft" / "build_algorithm.py"), sys.argv[1]], cwd=ROOT, check=True)
    else:
        subprocess.run([python, str(ROOT / "buildProject" / "build_all_algorithms.py"), "Microsoft"], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
