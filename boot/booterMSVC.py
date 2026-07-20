from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "buildProject"))
from anaconda import require_anaconda
from dependencies import DEPENDENCY_MODES, configure_optional_dependencies


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build AlgoForge with MSVC, with optional CUDA and PhysX setup."
    )
    parser.add_argument(
        "algorithms",
        nargs="*",
        help="Algorithm package names. With no names, all algorithm packages are built.",
    )
    parser.add_argument(
        "--cuda",
        choices=DEPENDENCY_MODES,
        default="auto",
        help="CUDA policy: auto detects nvcc, on requires it, off excludes CUDA sources.",
    )
    parser.add_argument(
        "--physx",
        choices=DEPENDENCY_MODES,
        default="auto",
        help="PhysX policy: auto enables it for packages that include PhysX headers.",
    )
    parser.add_argument(
        "--skip-mainline",
        action="store_true",
        help="Build only the requested algorithm packages; reuse an existing mainline build.",
    )
    return parser.parse_args()


def main() -> int:
    require_anaconda()
    arguments = parse_arguments()
    environment, _ = configure_optional_dependencies(
        arguments.algorithms,
        cuda_mode=arguments.cuda,
        physx_mode=arguments.physx,
    )

    python = sys.executable
    if not arguments.skip_mainline:
        subprocess.run(
            [python, str(ROOT / "buildProject" / "Microsoft" / "build_mainline.py")],
            cwd=ROOT,
            env=environment,
            check=True,
        )

    if arguments.algorithms:
        for algorithm_name in arguments.algorithms:
            subprocess.run(
                [
                    python,
                    str(ROOT / "buildProject" / "Microsoft" / "build_algorithm.py"),
                    algorithm_name,
                ],
                cwd=ROOT,
                env=environment,
                check=True,
            )
    else:
        subprocess.run(
            [python, str(ROOT / "buildProject" / "build_all_algorithms.py"), "Microsoft"],
            cwd=ROOT,
            env=environment,
            check=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
