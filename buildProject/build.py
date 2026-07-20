from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from anaconda import require_anaconda
from toolchain import ensure_windows_clang_toolchain


ROOT = Path(__file__).resolve().parent.parent
ALGORITHM_ROOT = ROOT / "algorithmLib"
SOURCE_ROOT = ALGORITHM_ROOT / "algorithmSrc"
RUNTIME_ROOT = ALGORITHM_ROOT / "algorithmruntimeLib"
BUILD_CONFIGURATION = os.environ.get("ALGOFORGE_ALGORITHM_CONFIGURATION", "RelWithDebInfo")
SDK_ROOT = ROOT / "sdk"


def set_environment_value(environment: dict[str, str], name: str, value: str) -> None:
    for existing_name in list(environment):
        if existing_name.lower() == name.lower():
            del environment[existing_name]
    environment[name] = value


def environment_value(environment: dict[str, str], name: str) -> str:
    return next(value for existing_name, value in environment.items() if existing_name.lower() == name.lower())


def visual_studio_environment(environment: dict[str, str]) -> dict[str, str]:
    if os.name != "nt":
        return environment

    vswhere = Path(environment_value(environment, "ProgramFiles(x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    installation = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip().splitlines()[0]
    developer_command = Path(installation) / "Common7" / "Tools" / "VsDevCmd.bat"
    command = f'call "{developer_command}" -arch=x64 -host_arch=x64 >nul && set'
    output = subprocess.run(command, shell=True, check=True, capture_output=True, text=True, env=environment).stdout
    for line in output.splitlines():
        if "=" in line:
            name, value = line.split("=", 1)
            set_environment_value(environment, name, value)
    return environment


def windows_resource_environment(environment: dict[str, str]) -> dict[str, str]:
    if os.name != "nt":
        return environment

    sdk_root = Path(environment_value(environment, "UniversalCRTSdkDir")).resolve()
    include_root = sdk_root / "Include"
    configured_version = environment.get("WindowsSDKVersion", "").strip("\\/")
    version_roots = []
    if configured_version:
        version_roots.append(include_root / configured_version)
    version_roots.extend(sorted((path for path in include_root.iterdir() if path.is_dir()), reverse=True))
    sdk_version_root = next(
        path for path in version_roots if (path / "um" / "winres.h").is_file()
    )
    sdk_version = sdk_version_root.name
    resource_compiler = sdk_root / "bin" / sdk_version / "x64" / "rc.exe"
    if not resource_compiler.is_file():
        raise RuntimeError(f"Windows SDK resource compiler is missing: {resource_compiler}")

    include_entries = [
        sdk_version_root / "ucrt",
        sdk_version_root / "um",
        sdk_version_root / "shared",
        sdk_version_root / "winrt",
        sdk_version_root / "cppwinrt",
    ]
    inherited_include = environment.get("INCLUDE", "")
    set_environment_value(
        environment,
        "INCLUDE",
        ";".join(str(path) for path in include_entries) + (";" + inherited_include if inherited_include else ""),
    )
    set_environment_value(environment, "WindowsSdkDir", str(sdk_root))
    set_environment_value(environment, "WindowsSDKVersion", sdk_version + "\\")
    return environment


def build_environment(toolchain: str) -> dict[str, str]:
    environment = dict(os.environ)
    set_environment_value(environment, "PATH", environment.get("Path", environment.get("PATH", "")))
    if toolchain == "Microsoft":
        if os.name != "nt":
            raise RuntimeError("The Microsoft toolchain is available only on Windows.")
        return visual_studio_environment(environment)

    environment = visual_studio_environment(environment)
    environment = windows_resource_environment(environment)
    if os.name == "nt":
        llvm_root = ensure_windows_clang_toolchain()
        environment["CC"] = str(llvm_root / "bin" / "clang-cl.exe")
        environment["CXX"] = str(llvm_root / "bin" / "clang-cl.exe")
        environment["NINJA_EXE"] = str(Path(environment_value(environment, "VSINSTALLDIR")) / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "Ninja" / "ninja.exe")
    else:
        environment["CC"] = shutil.which("clang") or "clang"
        environment["CXX"] = shutil.which("clang++") or "clang++"
        environment["NINJA_EXE"] = shutil.which("ninja") or "ninja"
    return environment


def run(command: list[str], environment: dict[str, str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def export_sdk(toolchain: str) -> None:
    _ = toolchain
    python_sdk = SDK_ROOT / "python"
    cpp_sdk = SDK_ROOT / "cpp"
    cpp_include = cpp_sdk / "include"
    cpp_library = cpp_sdk / "lib"
    shutil.rmtree(python_sdk, ignore_errors=True)
    shutil.rmtree(cpp_include, ignore_errors=True)
    shutil.copytree(ROOT / "aglopy", python_sdk / "aglopy")
    shutil.copy2(ROOT / "requirements.txt", python_sdk / "requirements.txt")
    shutil.copytree(ROOT / "src" / "sdk", cpp_include, ignore=shutil.ignore_patterns("*.cpp", "README.md"))
    if not cpp_library.is_dir() or not any(path.is_file() for path in cpp_library.iterdir()):
        raise RuntimeError(
            "The SDK target did not export a library into sdk/cpp/lib. "
            "CMake must copy $<TARGET_FILE:sdk> during the sdk POST_BUILD step."
        )
    (python_sdk / "README.md").write_text(
        "Use the aglopy package with DebugToolRunner to submit algorithms and pipelines.\n",
        encoding="utf-8",
    )
    (cpp_sdk / "README.md").write_text(
        "Headers are under include and the platform-selected SDK library is under lib.\n",
        encoding="utf-8",
    )


def toolchain_file(toolchain: str) -> Path:
    return Path(__file__).resolve().parent / toolchain / "toolchain.cmake"


def configure_generator(toolchain: str) -> tuple[str, list[str]]:
    if toolchain == "Microsoft":
        return "Visual Studio 17 2022", ["-A", "x64"]
    return "Ninja Multi-Config", []


def build_mainline(toolchain: str) -> None:
    environment = build_environment(toolchain)
    build_root = ROOT / "build" / toolchain
    generator, generator_args = configure_generator(toolchain)
    configure = [
        "cmake", "-S", str(ROOT), "-B", str(build_root), "--fresh", "-G", generator,
        *generator_args,
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file(toolchain)}",
    ]
    if toolchain == "OpenSource":
        configure.append(f"-DCMAKE_MAKE_PROGRAM={environment['NINJA_EXE']}")
        configure.append(f"-DALGOFORGE_CLANG_CL={environment['CXX']}")
    run(configure, environment)
    run(["cmake", "--build", str(build_root), "--config", BUILD_CONFIGURATION, "--target", "debugTool", "--parallel"], environment)
    run(["cmake", "--build", str(build_root), "--config", BUILD_CONFIGURATION, "--target", "sdk", "--parallel"], environment)
    export_sdk(toolchain)


def c_identifier(text: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", text)
    if result[0].isdigit():
        result = "_" + result
    return result


def find_algorithm(algorithm_name: str) -> Path:
    manifests = sorted(path for path in SOURCE_ROOT.rglob("manifest.json") if path.parent.name == algorithm_name)
    return manifests[0].parent


def algorithm_targets(algorithm_dir: Path) -> list[str]:
    return [
        c_identifier(path.parent.relative_to(SOURCE_ROOT).as_posix()) + "_algo"
        for path in sorted(algorithm_dir.rglob("manifest.json"))
    ]


def path_is_under(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def read_runtime_artifacts(runtime_dir: Path) -> set[Path]:
    artifact_manifest = runtime_dir / "runtime.artifacts"
    if not artifact_manifest.is_file():
        return set()

    format_version = None
    plugin_path = None
    artifact_paths: set[Path] = {Path("runtime.artifacts")}
    for line_number, raw_line in enumerate(artifact_manifest.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not key.strip() or not value.strip():
            raise RuntimeError(
                f"Invalid runtime.artifacts entry at {artifact_manifest}:{line_number}: {raw_line!r}"
            )
        key = key.strip()
        value = value.strip()
        if key == "format":
            if format_version is not None:
                raise RuntimeError(f"Duplicate format entry in {artifact_manifest}")
            format_version = value
            continue
        if key not in {"plugin", "runtime"}:
            raise RuntimeError(f"Unknown runtime.artifacts key {key!r} in {artifact_manifest}")
        relative_path = Path(value)
        if relative_path.is_absolute() or not relative_path.parts or ".." in relative_path.parts:
            raise RuntimeError(f"Unsafe runtime artifact path {value!r} in {artifact_manifest}")
        relative_path = Path(*relative_path.parts)
        artifact_path = runtime_dir / relative_path
        if not artifact_path.is_file():
            raise RuntimeError(
                f"Runtime artifact {relative_path.as_posix()!r} declared by {artifact_manifest} does not exist"
            )
        artifact_paths.add(relative_path)
        if key == "plugin":
            if plugin_path is not None:
                raise RuntimeError(f"Duplicate plugin entry in {artifact_manifest}")
            plugin_path = relative_path

    if format_version != "1":
        raise RuntimeError(f"Unsupported runtime.artifacts format in {artifact_manifest}: {format_version!r}")
    if plugin_path is None:
        raise RuntimeError(f"Missing plugin entry in {artifact_manifest}")
    return artifact_paths


def runtime_file_allowed(
    package_relative_dir: str,
    relative_file: Path,
    configuration: str,
    runtime_artifact_paths: set[Path],
) -> bool:
    if relative_file in runtime_artifact_paths:
        return True
    if configuration != "RelWithDebInfo":
        return True
    name = relative_file.name
    if package_relative_dir.startswith("norm/"):
        return "_exec." in name or "_result_render." in name
    if package_relative_dir.startswith("pipeline/"):
        if package_relative_dir.endswith("_stageBegin"):
            return "_exec." in name
        if package_relative_dir.endswith("_stageEnd"):
            return "_exec." in name or "_result_render." in name
        if re.search(r"_stage[0-9]+$", package_relative_dir):
            return True
    return True


def write_algo_package(archive_path: Path, staging_dir: Path) -> None:
    files = sorted(path for path in staging_dir.rglob("*") if path.is_file())
    with archive_path.open("wb") as archive:
        archive.write(b"EALGO001")
        archive.write(struct.pack("<II", 1, len(files)))
        for path in files:
            relative = path.relative_to(staging_dir).as_posix().encode("utf-8")
            data_size = path.stat().st_size
            archive.write(struct.pack("<IQ", len(relative), data_size))
            archive.write(relative)
            with path.open("rb") as source:
                shutil.copyfileobj(source, archive)


def package_runtime(source_root: Path, runtime_root: Path, configuration: str, api_source_root: Path) -> None:
    shutil.copy2(api_source_root / "algorithm_plugin_api.h", runtime_root / "algorithm_plugin_api.h")
    entries = []
    for manifest in sorted(source_root.rglob("manifest.json")):
        relative_dir = manifest.parent.relative_to(source_root)
        runtime_dir = runtime_root / relative_dir
        if runtime_dir.is_dir():
            entries.append((manifest, manifest.parent, relative_dir, runtime_dir))
    all_runtime_dirs = [entry[3] for entry in entries]

    for manifest, source_dir, relative_dir, runtime_dir in sorted(entries, key=lambda entry: len(entry[2].parts), reverse=True):
        package_name = source_dir.name
        staging_dir = Path(tempfile.mkdtemp(prefix=".algo_pack_"))
        try:
            shutil.copy2(manifest, runtime_dir / "manifest.json")
            shutil.copy2(manifest, staging_dir / "manifest.json")
            default_json = source_dir / "default.json"
            if default_json.is_file():
                shutil.copy2(default_json, runtime_dir / "default.json")
                shutil.copy2(default_json, staging_dir / "default.json")

            runtime_artifact_paths = read_runtime_artifacts(runtime_dir)
            nested_dirs = [path for path in all_runtime_dirs if path != runtime_dir and path_is_under(path, runtime_dir)]
            for runtime_file in sorted(path for path in runtime_dir.rglob("*") if path.is_file()):
                relative_file = runtime_file.relative_to(runtime_dir)
                if runtime_file.name == f"{package_name}.algo":
                    continue
                if "algocache" in runtime_file.parts:
                    continue
                if any(path_is_under(runtime_file, nested_dir) for nested_dir in nested_dirs):
                    continue
                if not runtime_file_allowed(
                    relative_dir.as_posix(),
                    relative_file,
                    configuration,
                    runtime_artifact_paths,
                ):
                    continue
                destination = staging_dir / relative_file
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(runtime_file, destination)

            archive_path = runtime_dir / f"{package_name}.algo"
            write_algo_package(archive_path, staging_dir)
        finally:
            shutil.rmtree(staging_dir)


def flatten_runtime_configuration(runtime_dir: Path) -> None:
    configuration_dir = runtime_dir / BUILD_CONFIGURATION
    if not configuration_dir.is_dir():
        return
    for path in configuration_dir.iterdir():
        shutil.move(str(path), str(runtime_dir / path.name))
    configuration_dir.rmdir()


def build_algorithm(toolchain: str, algorithm_name: str) -> None:
    environment = build_environment(toolchain)
    algorithm_dir = find_algorithm(algorithm_name)
    build_dir = ALGORITHM_ROOT / (".build_" + toolchain.lower())
    core_build_dir = ROOT / "build" / toolchain
    targets = algorithm_targets(algorithm_dir)
    shutil.rmtree(build_dir, ignore_errors=True)
    shutil.rmtree(RUNTIME_ROOT / algorithm_dir.relative_to(SOURCE_ROOT), ignore_errors=True)
    RUNTIME_ROOT.mkdir(parents=True, exist_ok=True)
    generator, generator_args = configure_generator(toolchain)
    configure = [
        "cmake", "-S", str(ALGORITHM_ROOT), "-B", str(build_dir), "--fresh", "-G", generator,
        *generator_args,
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file(toolchain)}",
        f"-DALGORITHM_LIBRARY_SOURCE_ROOT={SOURCE_ROOT}",
        f"-DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT={RUNTIME_ROOT}",
        f"-DCORE_BUILD_DIR={core_build_dir}",
        "-DBUILD_ALGORITHM_SAMPLE_PLUGIN=ON",
    ]
    if toolchain == "OpenSource":
        configure.append(f"-DCMAKE_MAKE_PROGRAM={environment['NINJA_EXE']}")
        configure.append(f"-DALGOFORGE_CLANG_CL={environment['CXX']}")
    run(configure, environment)
    for target in targets:
        run(["cmake", "--build", str(build_dir), "--config", BUILD_CONFIGURATION, "--target", target, "--parallel"], environment)
    runtime_algorithm_root = RUNTIME_ROOT / algorithm_dir.relative_to(SOURCE_ROOT)
    for manifest in sorted(algorithm_dir.rglob("manifest.json")):
        runtime_directory = runtime_algorithm_root / manifest.parent.relative_to(algorithm_dir)
        flatten_runtime_configuration(runtime_directory)
    package_runtime(
      algorithm_dir,
      runtime_algorithm_root,
        BUILD_CONFIGURATION,
        SOURCE_ROOT,
    )
    export_sdk(toolchain)
    shutil.rmtree(build_dir)


def clean() -> None:
    run(["git", "clean", "-fdx", "-e", "buildProject/"], os.environ.copy())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("mainline", "algorithm"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--toolchain", choices=("Microsoft", "OpenSource"), required=True)
        if command == "algorithm":
            subparser.add_argument("algorithm_name")
    subparsers.add_parser("clean")
    arguments = parser.parse_args(argv)
    require_anaconda()
    if arguments.command == "mainline":
        build_mainline(arguments.toolchain)
    elif arguments.command == "algorithm":
        build_algorithm(arguments.toolchain, arguments.algorithm_name)
    else:
        clean()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
