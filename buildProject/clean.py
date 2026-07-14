from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build import main

raise SystemExit(main(["clean", *sys.argv[1:]]))
