from __future__ import annotations

import json
import os
import shutil
import subprocess
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOT = ROOT / "algorithmLib" / "algorithmSrc"
DEPENDENCY_MODES = ("auto", "on", "off")


@dataclass(frozen=True)
class AlgorithmRequirements:
    algorithms: tuple[str, ...]
    physx: bool
    cuda_sources: bool


@dataclass(frozen=True)
class DependencyPlan:
    algorithms: tuple[str, ...]
    physx_required: bool
    physx_enabled: bool
    physx_root: str
    cuda_sources: bool
    cuda_enabled: bool
    cuda_root: str
    nvcc: str
    nvcc_version: str


def normalize_mode(value: str, option_name: str) -> str:
    mode = value.strip().lower()
    if mode not in DEPENDENCY_MODES:
        raise ValueError(
            f"{option_name} must be one of {', '.join(DEPENDENCY_MODES)}, got {value!r}."
        )
    return mode


def _find_algorithm_directory(algorithm_name: str) -> Path:
    matches = sorted(
        manifest.parent
        for manifest in SOURCE_ROOT.rglob("manifest.json")
        if manifest.parent.name == algorithm_name
    )
    if not matches:
        raise FileNotFoundError(f"Algorithm package was not found: {algorithm_name}")
    if len(matches) > 1:
        rendered = "\n".join(f"  - {path}" for path in matches)
        raise RuntimeError(
            f"Algorithm name {algorithm_name!r} is ambiguous. Matching package roots:\n{rendered}"
        )
    return matches[0]


def _source_mentions_physx(path: Path) -> bool:
    if path.suffix.lower() not in {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".cu"}:
        return False
    try:
        return "PxPhysicsAPI.h" in path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return False


def inspect_algorithm_requirements(algorithm_names: Iterable[str]) -> AlgorithmRequirements:
    names = tuple(algorithm_names)
    roots = tuple(_find_algorithm_directory(name) for name in names)
    if not roots:
        roots = (SOURCE_ROOT,)

    physx = False
    cuda_sources = False
    for root in roots:
        for source in root.rglob("*"):
            if not source.is_file():
                continue
            if source.suffix.lower() == ".cu":
                cuda_sources = True
            if not physx and _source_mentions_physx(source):
                physx = True
            if physx and cuda_sources:
                break
        if physx and cuda_sources:
            break

    return AlgorithmRequirements(names, physx, cuda_sources)


def _existing_executable(candidate: str | Path | None) -> Path | None:
    if not candidate:
        return None
    candidate_path = Path(candidate).expanduser()
    if candidate_path.is_file():
        return candidate_path.resolve()
    located = shutil.which(str(candidate))
    return Path(located).resolve() if located else None


def _cuda_root_candidates(environment: dict[str, str]) -> list[Path]:
    candidates: list[Path] = []
    for name in ("CUDAToolkit_ROOT", "CUDA_PATH", "CUDA_HOME"):
        value = environment.get(name, "").strip()
        if value:
            candidates.append(Path(value).expanduser())

    if os.name == "nt":
        program_files = environment.get("ProgramFiles", r"C:\Program Files")
        toolkit_root = Path(program_files) / "NVIDIA GPU Computing Toolkit" / "CUDA"
        if toolkit_root.is_dir():
            candidates.extend(sorted(toolkit_root.glob("v*"), reverse=True))
    else:
        candidates.append(Path("/usr/local/cuda"))
        candidates.extend(sorted(Path("/usr/local").glob("cuda-*"), reverse=True))
    return candidates


def find_cuda_toolkit(environment: dict[str, str]) -> tuple[Path | None, Path | None]:
    nvcc = _existing_executable(environment.get("CUDACXX"))
    if nvcc:
        return nvcc.parent.parent, nvcc

    for root in _cuda_root_candidates(environment):
        executable_name = "nvcc.exe" if os.name == "nt" else "nvcc"
        candidate = _existing_executable(root / "bin" / executable_name)
        if candidate:
            return root.resolve(), candidate

    nvcc = _existing_executable("nvcc")
    if nvcc:
        return nvcc.parent.parent, nvcc
    return None, None


def _nvcc_version(nvcc: Path | None, environment: dict[str, str]) -> str:
    if not nvcc:
        return ""
    try:
        output = subprocess.run(
            [str(nvcc), "--version"],
            check=True,
            capture_output=True,
            text=True,
            env=environment,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""
    return output.splitlines()[-1] if output else ""


def _physx_root(environment: dict[str, str]) -> Path | None:
    configured = environment.get("ALGOFORGE_PHYSX_ROOT", "").strip()
    if configured:
        root = Path(configured).expanduser().resolve()
        if not (root / "include" / "PxPhysicsAPI.h").is_file() or not (root / "CMakeLists.txt").is_file():
            raise RuntimeError(
                "ALGOFORGE_PHYSX_ROOT must point to the PhysX SDK source directory "
                "containing include/PxPhysicsAPI.h and CMakeLists.txt."
            )
        return root

    legacy_include = environment.get("ALGOFORGE_PHYSX_INCLUDE_DIR", "").strip()
    if legacy_include:
        include_root = Path(legacy_include).expanduser().resolve()
        candidate = include_root.parent if include_root.name == "include" else include_root
        if (candidate / "include" / "PxPhysicsAPI.h").is_file() and (candidate / "CMakeLists.txt").is_file():
            return candidate
    return None


def configure_optional_dependencies(
    algorithm_names: Iterable[str],
    *,
    cuda_mode: str = "auto",
    physx_mode: str = "auto",
    base_environment: dict[str, str] | None = None,
) -> tuple[dict[str, str], DependencyPlan]:
    environment = dict(base_environment or os.environ)
    requirements = inspect_algorithm_requirements(algorithm_names)
    cuda_mode = normalize_mode(cuda_mode, "--cuda")
    physx_mode = normalize_mode(physx_mode, "--physx")

    cuda_root, nvcc = find_cuda_toolkit(environment)
    if cuda_mode == "on" and not nvcc:
        raise RuntimeError(
            "CUDA was explicitly enabled, but nvcc was not found. Set CUDAToolkit_ROOT or CUDACXX, "
            "or rerun with --cuda off/auto."
        )
    cuda_enabled = cuda_mode == "on" or (
        cuda_mode == "auto" and requirements.cuda_sources and nvcc is not None
    )

    physx_root = _physx_root(environment)
    if physx_mode == "off" and requirements.physx:
        raise RuntimeError(
            "The selected algorithm requires PhysX, but PhysX was disabled with --physx off."
        )
    physx_enabled = physx_mode == "on" or (
        physx_mode == "auto" and requirements.physx
    )

    environment["ALGOFORGE_ENABLE_CUDA"] = "ON" if cuda_enabled else "OFF"
    environment["ALGOFORGE_ENABLE_PHYSX"] = "ON" if physx_enabled else "OFF"
    environment["ALGOFORGE_CUDA_MODE"] = cuda_mode
    environment["ALGOFORGE_PHYSX_MODE"] = physx_mode

    if cuda_enabled and cuda_root and nvcc:
        environment["CUDAToolkit_ROOT"] = str(cuda_root)
        environment["CUDACXX"] = str(nvcc)
        if os.name == "nt":
            environment["CUDA_PATH"] = str(cuda_root)
    if physx_root:
        environment["ALGOFORGE_PHYSX_ROOT"] = str(physx_root)

    plan = DependencyPlan(
        algorithms=requirements.algorithms,
        physx_required=requirements.physx,
        physx_enabled=physx_enabled,
        physx_root=str(physx_root or ""),
        cuda_sources=requirements.cuda_sources,
        cuda_enabled=cuda_enabled,
        cuda_root=str(cuda_root or ""),
        nvcc=str(nvcc or ""),
        nvcc_version=_nvcc_version(nvcc, environment),
    )

    report_path = ROOT / "testData" / "dependency_probe.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(asdict(plan), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    selected = ", ".join(plan.algorithms) if plan.algorithms else "all algorithms"
    print(f"Dependency plan for {selected}:", flush=True)
    print(
        "  PhysX: "
        + ("enabled (local source)" if plan.physx_enabled and plan.physx_root else
           "enabled (CMake FetchContent)" if plan.physx_enabled else "disabled"),
        flush=True,
    )
    print(
        "  CUDA: "
        + (f"enabled ({plan.nvcc_version or plan.nvcc})" if plan.cuda_enabled else
           "disabled (toolkit not found)" if plan.cuda_sources and not plan.nvcc else "disabled"),
        flush=True,
    )
    print(f"  Report: {report_path}", flush=True)
    return environment, plan
