from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "buildProject"))
from anaconda import require_anaconda


def main() -> int:
    require_anaconda()
    subprocess.run([sys.executable, "-m", "pip", "install", "--disable-pip-version-check", "--no-input", "-r", str(ROOT / "requirements.txt")], cwd=ROOT, check=True)
    print("Python environment is ready.")
    toolchain = sys.argv[1] if len(sys.argv) > 1 else ("Microsoft" if sys.platform == "win32" else "OpenSource")
    if toolchain not in {"Microsoft", "OpenSource"}:
        raise ValueError("quick_begin.py accepts Microsoft or OpenSource as its optional toolchain")
    booter = ROOT / "boot" / ("booterMSVC.py" if toolchain == "Microsoft" else "booterNinjaClang.py")
    subprocess.run([sys.executable, str(booter)], cwd=ROOT, check=True)
    print(f"Built the mainline, SDK, and all algorithm packages with {toolchain}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
