from __future__ import annotations

import hashlib
import json
import os
import subprocess
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
LOCK_PATH = Path(__file__).with_name("toolchain.lock.json")


def _toolchain_lock() -> dict[str, object]:
    return json.loads(LOCK_PATH.read_text(encoding="utf-8"))


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _clang_cl(toolchain_root: Path) -> Path:
    return toolchain_root / "bin" / "clang-cl.exe"


def _cache_root() -> Path:
    configured = os.environ.get("ALGOFORGE_TOOLCHAIN_CACHE")
    if configured:
        return Path(configured).expanduser()
    local_app_data = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    return local_app_data / "AlgoForge" / "toolchains"


def _existing_toolchain_roots(version: str) -> list[Path]:
    configured = os.environ.get("ALGOFORGE_TOOLCHAIN_ROOT")
    roots = []
    if configured:
        roots.append(Path(configured).expanduser())
    roots.append(ROOT / ".toolchains" / f"llvm-{version}")
    roots.append(ROOT.parent / ".toolchains" / f"llvm-{version}")
    roots.append(_cache_root() / f"llvm-{version}")
    return roots


def ensure_windows_clang_toolchain() -> Path:
    lock = _toolchain_lock()
    version = str(lock["version"])
    for root in _existing_toolchain_roots(version):
        if _clang_cl(root).is_file():
            return root

    package = dict(lock["windows_x64"])
    cache_root = _cache_root()
    cache_root.mkdir(parents=True, exist_ok=True)
    installer = cache_root / str(package["asset"])
    if not installer.is_file():
        urllib.request.urlretrieve(str(package["url"]), installer)
    if _sha256(installer) != str(package["sha256"]):
        raise RuntimeError(f"LLVM installer checksum mismatch: {installer}")

    toolchain_root = cache_root / f"llvm-{version}"
    toolchain_root.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(installer), "/S", f"/D={toolchain_root}"], check=True)
    if not _clang_cl(toolchain_root).is_file():
        raise RuntimeError(f"LLVM installer did not produce {_clang_cl(toolchain_root)}")
    return toolchain_root
