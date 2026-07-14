from __future__ import annotations

import runpy
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "algorithmDevTools"))
runpy.run_module("algorithm_studio.algorithm_studio", run_name="__main__")
