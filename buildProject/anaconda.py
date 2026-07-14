from __future__ import annotations

import os
import sys
from pathlib import Path


def require_anaconda() -> None:
    conda_prefix_text = os.environ.get("CONDA_PREFIX", "")
    if not conda_prefix_text:
        raise RuntimeError(
            "Anaconda is required. Activate an Anaconda/Miniconda environment before using this entry point."
        )

    conda_prefix = Path(conda_prefix_text).resolve()
    interpreter = Path(sys.executable).resolve()
    if Path(sys.prefix).resolve() != conda_prefix:
        raise RuntimeError(
            "Anaconda is required. Run this command with the Python interpreter from the activated conda environment."
        )
    if not (conda_prefix / "conda-meta").is_dir():
        raise RuntimeError(
            "Anaconda is required. The active Python interpreter is not inside a conda environment."
        )

    expected_interpreter = conda_prefix / ("python.exe" if os.name == "nt" else Path("bin") / "python")
    if interpreter != expected_interpreter.resolve():
        raise RuntimeError(
            "Anaconda is required. Use the Python executable belonging to CONDA_PREFIX."
        )
