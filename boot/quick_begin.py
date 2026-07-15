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
    print("Run booterMSVC.py or booterNinjaClang.py next.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
