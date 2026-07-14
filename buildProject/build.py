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


ROOT = Path(__file__).resolve().parent.parent
ALGORITHM_ROOT = ROOT / "algorithmLib"
SOURCE_ROOT = ALGORITHM_ROOT / "algorithmSrc"
RUNTIME_ROOT = ALGORITHM_ROOT / "algorithmruntimeLib" / "releaseWithDebugInfo"
SDK_ROOT = ROOT / "sdk"
BOOT_ROOT = ROOT / "boot"


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


def build_environment(toolchain: str) -> dict[str, str]:
    environment = dict(os.environ)
    set_environment_value(environment, "PATH", environment.get("Path", environment.get("PATH", "")))
    if toolchain == "Microsoft":
        if os.name != "nt":
            raise RuntimeError("The Microsoft toolchain is available only on Windows.")
        return visual_studio_environment(environment)

    environment = visual_studio_environment(environment)
    if os.name == "nt":
        llvm_root = ROOT / ".toolchains" / "llvm-22.1.8"
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


def replace_link(link: Path, target: Path) -> None:
    if link.is_symlink() or link.is_file():
        link.unlink()
    elif link.is_dir():
        shutil.rmtree(link)
    link.symlink_to(os.path.relpath(target, link.parent), target_is_directory=target.is_dir())


def refresh_boot_links(toolchain: str) -> None:
    debug_tool = ROOT / "build" / toolchain / "Debug" / ("debugTool.exe" if os.name == "nt" else "debugTool")
    replace_link(BOOT_ROOT / ("debugTool.exe" if os.name == "nt" else "debugTool"), debug_tool)
    replace_link(BOOT_ROOT / "devTools", ROOT / "algorithmDevTools")
    replace_link(BOOT_ROOT / "devTools.py", BOOT_ROOT / "launch_devTools.py")


def export_sdk(toolchain: str) -> None:
    build_debug = ROOT / "build" / toolchain / "Debug"
    python_sdk = SDK_ROOT / "python"
    cpp_sdk = SDK_ROOT / "cpp"
    shutil.rmtree(python_sdk, ignore_errors=True)
    shutil.rmtree(cpp_sdk, ignore_errors=True)
    shutil.copytree(ROOT / "aglopy", python_sdk / "aglopy")
    shutil.copy2(ROOT / "requirements.txt", python_sdk / "requirements.txt")
    shutil.copytree(ROOT / "src" / "sdk", cpp_sdk / "include", ignore=shutil.ignore_patterns("*.cpp", "README.md"))
    sdk_library = next(build_debug.glob("sdk*.lib"), SDK_ROOT / "sdk.lib")
    (cpp_sdk / "lib").mkdir(parents=True, exist_ok=True)
    shutil.copy2(sdk_library, cpp_sdk / "lib" / sdk_library.name)
    dll_sources = list(build_debug.glob("*.dll")) + list((ROOT / "build" / toolchain / "assimp-build" / "bin" / "Debug").glob("*.dll"))
    (cpp_sdk / "bin").mkdir(parents=True, exist_ok=True)
    for dll in dll_sources:
        shutil.copy2(dll, cpp_sdk / "bin" / dll.name)
    (python_sdk / "README.md").write_text(
        "Use the aglopy package with DebugToolRunner to submit algorithms and pipelines.\n",
        encoding="utf-8",
    )
    (cpp_sdk / "README.md").write_text(
        "Headers are under include, the SDK library is under lib, and runtime DLLs are under bin.\n",
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
    run(configure, environment)
    run(["cmake", "--build", str(build_root), "--config", "Debug", "--target", "debugTool", "--parallel"], environment)
    run(["cmake", "--build", str(build_root), "--config", "Debug", "--target", "sdk", "--parallel"], environment)
    export_sdk(toolchain)
    refresh_boot_links(toolchain)


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


def runtime_file_allowed(package_relative_dir: str, relative_file: Path, configuration: str) -> bool:
    if configuration != "RelWithDebInfo":
        return True
    name = relative_file.name
    if name.endswith(".dll"):
        return True
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


def package_runtime(source_root: Path, runtime_root: Path, configuration: str) -> None:
    shutil.copy2(source_root / "algorithm_plugin_api.h", runtime_root / "algorithm_plugin_api.h")
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

            dll_candidates = [runtime_dir / "Debug" / f"{package_name}.dll", runtime_dir / "RelWithDebInfo" / f"{package_name}.dll", runtime_dir / f"{package_name}.dll"]
            next(path for path in dll_candidates if path.is_file())
            nested_dirs = [path for path in all_runtime_dirs if path != runtime_dir and path_is_under(path, runtime_dir)]
            for runtime_file in sorted(path for path in runtime_dir.rglob("*") if path.is_file()):
                relative_file = runtime_file.relative_to(runtime_dir)
                if runtime_file.suffix in {".algo", ".exp", ".lib", ".pdb", ".ilk", ".obj", ".manifest"}:
                    continue
                if "algocache" in runtime_file.parts:
                    continue
                if any(path_is_under(runtime_file, nested_dir) for nested_dir in nested_dirs):
                    continue
                if not runtime_file_allowed(relative_dir.as_posix(), relative_file, configuration):
                    continue
                destination = staging_dir / relative_file
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(runtime_file, destination)

            archive_path = runtime_dir / f"{package_name}.algo"
            write_algo_package(archive_path, staging_dir)
        finally:
            shutil.rmtree(staging_dir)


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
    run(configure, environment)
    for target in targets:
        run(["cmake", "--build", str(build_dir), "--config", "RelWithDebInfo", "--target", target, "--parallel"], environment)
    package_runtime(SOURCE_ROOT, RUNTIME_ROOT, "RelWithDebInfo")
    export_sdk(toolchain)
    refresh_boot_links(toolchain)
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
