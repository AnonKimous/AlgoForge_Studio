from __future__ import annotations

import json
import os
import shutil
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
SETTINGS_PATH = Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex"))) / "algorithm_studio_settings.json"


def _resolve_codex_command(command: str) -> str:
    search_roots: list[Path] = []
    localappdata = os.environ.get("LOCALAPPDATA", "").strip()
    if localappdata:
        search_roots.extend(
            [
                Path(localappdata) / "OpenAI" / "Codex" / "bin",
                Path(localappdata) / "Programs" / "OpenAI" / "Codex" / "bin",
            ]
        )
    install_dir = os.environ.get("CODEX_INSTALL_DIR", "").strip()
    if install_dir:
        search_roots.append(Path(install_dir))
    for root in search_roots:
        if root.is_dir():
            matches = sorted(root.rglob("codex.exe"))
            if matches:
                return str(matches[0])
    env_command = os.environ.get("CODEX_COMMAND", "").strip()
    if env_command:
        which_env = shutil.which(env_command)
        if which_env is not None:
            return which_env
    resolved = command.strip() or "codex"
    which_resolved = shutil.which(resolved)
    if which_resolved is not None:
        return which_resolved
    return env_command or resolved


def _resolve_default_template_path() -> Path:
    candidates = [
        PROJECT_ROOT / "algorithmLib" / "algorithmSrc" / "algorithm_package_example.json",
        PROJECT_ROOT / "src" / "capabilities" / "algorithm_library" / "algorithm_package_example.json",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


DEFAULT_TEMPLATE_PATH = _resolve_default_template_path()


def _load_algorithm_studio_settings() -> dict[str, str]:
    if SETTINGS_PATH.exists():
        return json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
    return {}


def _save_algorithm_studio_settings(settings: dict[str, str]) -> None:
    SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS_PATH.write_text(json.dumps(settings, indent=2, ensure_ascii=False), encoding="utf-8")


NODE_WIDTH = 154
NODE_HEIGHT = 64
SPECIAL_CARD_WIDTH = 210
SPECIAL_CARD_HEIGHT = 108
BLUEPRINT_NODE_WIDTH = 360
BLUEPRINT_NODE_MIN_HEIGHT = 160
CANVAS_PADDING = 24
SIDEBAR_EXPANDED_WIDTH = 420
SIDEBAR_COLLAPSED_WIDTH = 36

COLORS = {
    "window": "#111418",
    "panel": "#161b22",
    "panel_alt": "#1b212b",
    "canvas": "#0f1319",
    "grid": "#202632",
    "text": "#edf2f7",
    "muted": "#8b98a7",
    "accent": "#6ee7ff",
    "accent_2": "#f59e0b",
    "good": "#34d399",
    "bad": "#f87171",
    "container": "#1d4ed8",
    "container_array": "#7c3aed",
    "stage": "#0f766e",
    "agent": "#b45309",
    "edge": "#9aa4b2",
    "preview": "#0b1020",
}
