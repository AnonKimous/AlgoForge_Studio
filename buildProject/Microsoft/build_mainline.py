from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build import main

raise SystemExit(main(["mainline", "--toolchain", "Microsoft", *sys.argv[1:]]))
