from __future__ import annotations

import sys
import threading
import traceback
from datetime import datetime
from typing import Any
import tkinter as tk

try:
    from .shared import PROJECT_ROOT
except ImportError:
    from shared import PROJECT_ROOT


class AlgorithmStudioRuntimeLoggingMixin:
    def _initialize_runtime_logging(self) -> None:
        log_dir = PROJECT_ROOT / "tools" / "algorithm_studio" / "runtime_logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        self.runtime_log_path = log_dir / f"algorithm_studio_{timestamp}.log"
        self.runtime_log_path.write_text("", encoding="utf-8")
        self._write_runtime_log_line("INFO", "Logging initialized.")

    def _write_runtime_log_line(self, level: str, message: str) -> None:
        if self.runtime_log_path is None:
            raise AssertionError("Runtime log path is not initialized.")
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")
        with self.runtime_log_path.open("a", encoding="utf-8") as handle:
            handle.write(f"[{timestamp}] [{level}] {message}\n")

    def _install_runtime_exception_hooks(self) -> None:
        self.root.report_callback_exception = self._handle_tk_callback_exception
        sys.excepthook = self._handle_process_exception
        threading.excepthook = self._handle_thread_exception

    def _handle_tk_callback_exception(
        self,
        exc_type: type[BaseException],
        exc_value: BaseException,
        exc_traceback: Any,
    ) -> None:
        self._log_exception("Tk callback exception", exc_type, exc_value, exc_traceback)

    def _handle_process_exception(
        self,
        exc_type: type[BaseException],
        exc_value: BaseException,
        exc_traceback: Any,
    ) -> None:
        self._log_exception("Unhandled process exception", exc_type, exc_value, exc_traceback)

    def _handle_thread_exception(self, args: threading.ExceptHookArgs) -> None:
        thread_name = getattr(args.thread, "name", "") or "unnamed"
        self._log_exception(f"Unhandled thread exception [{thread_name}]", args.exc_type, args.exc_value, args.exc_traceback)

    def _log_exception(
        self,
        context: str,
        exc_type: type[BaseException],
        exc_value: BaseException,
        exc_traceback: Any,
    ) -> None:
        summary = f"{context}: {exc_type.__name__}: {exc_value}"
        self._write_runtime_log_line("ERROR", summary)
        for line in traceback.format_exception(exc_type, exc_value, exc_traceback):
            for entry in line.rstrip().splitlines():
                self._write_runtime_log_line("TRACE", entry)
        self.log_lines.append(summary)
        if self.log_text:
            self.log_text.insert(tk.END, summary + "\n")
            self.log_text.see(tk.END)
        self.status_var.set(summary)

    def _log(self, message: str) -> None:
        self.log_lines.append(message)
        self._write_runtime_log_line("INFO", message)
        if hasattr(self, "_record_operation_from_log"):
            self._record_operation_from_log(message)
        if self.log_text:
            self.log_text.insert(tk.END, message + "\n")
            self.log_text.see(tk.END)
        self.status_var.set(message)
