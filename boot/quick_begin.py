from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "buildProject"))
from anaconda import require_anaconda
from dependencies import DEPENDENCY_MODES


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare Python, optional SDK dependencies, and build AlgoForge."
    )
    parser.add_argument(
        "toolchain",
        nargs="?",
        choices=("Microsoft", "OpenSource"),
        default="Microsoft" if sys.platform == "win32" else "OpenSource",
    )
    parser.add_argument(
        "--algorithm",
        action="append",
        default=[],
        dest="algorithms",
        help="Build one algorithm package. Repeat the option to build multiple packages.",
    )
    parser.add_argument("--cuda", choices=DEPENDENCY_MODES, default="auto")
    parser.add_argument("--physx", choices=DEPENDENCY_MODES, default="auto")
    return parser.parse_args()


def main() -> int:
    require_anaconda()
    arguments = parse_arguments()
    subprocess.run(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--no-input",
            "-r",
            str(ROOT / "requirements.txt"),
        ],
        cwd=ROOT,
        check=True,
    )
    print("Python environment is ready.")

    booter = ROOT / "boot" / (
        "booterMSVC.py" if arguments.toolchain == "Microsoft" else "booterNinjaClang.py"
    )
    command = [
        sys.executable,
        str(booter),
        *arguments.algorithms,
        "--cuda",
        arguments.cuda,
        "--physx",
        arguments.physx,
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    target_text = ", ".join(arguments.algorithms) if arguments.algorithms else "all algorithm packages"
    print(
        f"Built the mainline, SDK, and {target_text} with {arguments.toolchain}."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
