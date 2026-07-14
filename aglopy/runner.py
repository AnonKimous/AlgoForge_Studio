"""A small Python wrapper around the debugTool runner protocol."""

from __future__ import annotations

import os
import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Literal, Sequence

Execution = Literal["jobs", "vk", "cuda"]


class AglopyError(RuntimeError):
    """Raised when the debugTool runner cannot be started or returns an error."""


@dataclass(frozen=True)
class RunnerResult:
    """Result returned by one algorithm or pipeline runner request."""

    kind: Literal["algorithm", "pipeline"]
    algorithm: str
    execution: Execution
    ticks: int
    response: str
    client_stdout: str
    client_stderr: str
    preview_output: Path | None
    log_path: Path

    @property
    def ok(self) -> bool:
        return self.response.startswith("OK ")

    @property
    def preview_pixels(self) -> int | None:
        match = re.search(r"preview_pixels=(\d+)", self.log_text)
        return int(match.group(1)) if match else None

    @property
    def log_text(self) -> str:
        try:
            return self.log_path.read_text(encoding="utf-8")
        except OSError:
            return ""

    def require_ok(self) -> "RunnerResult":
        if not self.ok:
            raise AglopyError(
                f"debugTool returned {self.response!r} for {self.algorithm!r}.\n"
                f"stdout:\n{self.client_stdout}\n"
                f"stderr:\n{self.client_stderr}"
            )
        return self


class RunnerServer:
    """Persistent debugTool runner server.

    The server accepts multiple requests, which makes it suitable for scripts
    that repeatedly submit ordinary algorithms and pipelines in one process.
    """

    def __init__(
        self,
        root: str | os.PathLike[str],
        debugtool: str | os.PathLike[str] | None = None,
        endpoint: str = "127.0.0.1:0",
        startup_timeout: float = 15.0,
    ) -> None:
        self.root = Path(root).resolve()
        default_debugtool = self.root / "boot" / ("debugTool.exe" if os.name == "nt" else "debugTool")
        self.debugtool = Path(debugtool).resolve() if debugtool else default_debugtool.resolve()
        self.endpoint = endpoint
        self.startup_timeout = startup_timeout
        self._process: subprocess.Popen[str] | None = None

    @property
    def process_environment(self) -> dict[str, str]:
        environment = dict(os.environ)
        path_name = next(name for name in environment if name.lower() == "path")
        runtime_paths = [self.debugtool.parent]
        build_root = self.debugtool.parent.parent.parent
        runtime_paths.append(build_root / "assimp-build" / "bin" / "Debug")
        runtime_paths_text = os.pathsep.join(str(path) for path in runtime_paths)
        environment[path_name] = runtime_paths_text + os.pathsep + environment[path_name]
        return environment

    @property
    def running(self) -> bool:
        return self._process is not None and self._process.poll() is None

    @property
    def endpoint_file(self) -> Path:
        return self.root / "testData" / "runner_control" / "endpoint.txt"

    def start(self) -> "RunnerServer":
        if self.running:
            return self
        if not self.debugtool.is_file():
            raise AglopyError(f"debugTool executable does not exist: {self.debugtool}")

        self.endpoint_file.parent.mkdir(parents=True, exist_ok=True)
        self.endpoint_file.unlink(missing_ok=True)
        start_time = time.monotonic()
        self._process = subprocess.Popen(
            [
                str(self.debugtool),
                "--runner-server",
                "--runner-endpoint",
                self.endpoint,
            ],
            cwd=self.root,
            env=self.process_environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        while time.monotonic() - start_time < self.startup_timeout:
            if self._process.poll() is not None:
                output = self._process.stdout.read() if self._process.stdout else ""
                raise AglopyError(f"runner server exited during startup:\n{output}")
            if self.endpoint_file.is_file():
                endpoint_text = self.endpoint_file.read_text(encoding="utf-8").strip()
                if endpoint_text and not endpoint_text.endswith(":0"):
                    return self
            time.sleep(0.05)
        self.stop()
        raise AglopyError(f"runner server did not publish an endpoint: {self.endpoint_file}")

    def stop(self) -> None:
        if self._process is None:
            return
        if self._process.poll() is None:
            try:
                endpoint = self.endpoint_file.read_text(encoding="utf-8").strip()
                subprocess.run(
                    [str(self.debugtool), "--runner-shutdown", "--runner-endpoint", endpoint],
                    cwd=self.root,
                    env=self.process_environment,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    check=False,
                    timeout=5,
                )
            except (OSError, subprocess.TimeoutExpired):
                self._process.terminate()
            try:
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
        if self._process.stdout:
            self._process.stdout.close()
        self._process = None

    def __enter__(self) -> "RunnerServer":
        return self.start()

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.stop()

    @staticmethod
    def _extract_response(stdout: str) -> str:
        for line in reversed(stdout.splitlines()):
            if line.startswith("OK ") or line.startswith("ERR "):
                return line.strip()
        return ""

    def request(self, tokens: Sequence[str]) -> tuple[str, str, str]:
        if not self.running:
            self.start()
        endpoint = self.endpoint_file.read_text(encoding="utf-8").strip()
        completed = subprocess.run(
            [str(self.debugtool), *tokens, "--runner-endpoint", endpoint],
            cwd=self.root,
            env=self.process_environment,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        return self._extract_response(completed.stdout), completed.stdout, completed.stderr


class DebugToolRunner:
    """High-level runner facade for Python tests and experiments."""

    def __init__(
        self,
        root: str | os.PathLike[str],
        debugtool: str | os.PathLike[str] | None = None,
    ) -> None:
        self.root = Path(root).resolve()
        self.server = RunnerServer(self.root, debugtool=debugtool)

    def close(self) -> None:
        self.server.stop()

    def __enter__(self) -> "DebugToolRunner":
        self.server.start()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()

    def run_algorithm(
        self,
        algorithm: str,
        *,
        ticks: int = 1,
        execution: Execution = "jobs",
        preview_output: str | os.PathLike[str] | None = None,
        preview_width: int = 640,
        preview_height: int = 480,
    ) -> RunnerResult:
        return self._run(
            "algorithm",
            algorithm,
            ticks=ticks,
            execution=execution,
            preview_output=preview_output,
            preview_width=preview_width,
            preview_height=preview_height,
        )

    def run_pipeline(
        self,
        pipeline: str,
        *,
        ticks: int = 1,
        execution: Execution = "jobs",
        preview_output: str | os.PathLike[str] | None = None,
        preview_width: int = 640,
        preview_height: int = 480,
    ) -> RunnerResult:
        return self._run(
            "pipeline",
            pipeline,
            ticks=ticks,
            execution=execution,
            preview_output=preview_output,
            preview_width=preview_width,
            preview_height=preview_height,
        )

    def _run(
        self,
        kind: Literal["algorithm", "pipeline"],
        algorithm: str,
        *,
        ticks: int,
        execution: Execution,
        preview_output: str | os.PathLike[str] | None,
        preview_width: int,
        preview_height: int,
    ) -> RunnerResult:
        if ticks <= 0:
            raise ValueError("ticks must be positive")
        if execution not in ("jobs", "vk", "cuda"):
            raise ValueError(f"unsupported execution backend: {execution}")

        output_path = Path(preview_output).resolve() if preview_output else None
        if output_path:
            output_path.parent.mkdir(parents=True, exist_ok=True)
        option = "--algorithm-runner" if kind == "algorithm" else "--pipeline-runner"
        output_option = "--preview-output" if kind == "algorithm" else "--preview-output"
        tokens = [
            option,
            "--algorithm",
            algorithm,
            "--ticks",
            str(ticks),
            "--execution",
            execution,
            "--preview-width",
            str(preview_width),
            "--preview-height",
            str(preview_height),
        ]
        if output_path:
            tokens.extend([output_option, str(output_path)])
        response, stdout, stderr = self.server.request(tokens)
        log_path = self.root / "testData" / ("norm" if kind == "algorithm" else "pipeline") / "debugInfo" / "last_run.log"
        return RunnerResult(
            kind=kind,
            algorithm=algorithm,
            execution=execution,
            ticks=ticks,
            response=response,
            client_stdout=stdout,
            client_stderr=stderr,
            preview_output=output_path,
            log_path=log_path,
        )
