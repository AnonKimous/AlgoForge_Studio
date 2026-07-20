from __future__ import annotations

import sys


def require_anaconda() -> None:
    # Historical entry-point name retained for compatibility. AlgoForge only
    # requires a supported Python interpreter and the packages in requirements.txt.
    if sys.version_info < (3, 10):
        raise RuntimeError(
            "Python 3.10 or newer is required. Install requirements.txt in the active environment."
        )
