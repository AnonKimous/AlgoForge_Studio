from __future__ import annotations

from datetime import datetime
import tkinter as tk

try:
    from .shared import COLORS
except ImportError:
    from shared import COLORS


class AlgorithmStudioOperationStackMixin:
    def _cancel_operation_stack_auto_followup(self) -> None:
        after_id = self.operation_stack_auto_followup_after_id
        if after_id is None:
            return
        self.operation_stack_auto_followup_after_id = None
        try:
            self.root.after_cancel(after_id)
        except Exception:
            return

    def _toggle_selection_view_mode(self) -> None:
        current = str(self.selection_view_mode_var.get() or "selection").strip().lower()
        self.selection_view_mode_var.set("operation_stack" if current == "selection" else "selection")
        self._apply_selection_panel_layout()

    def _toggle_operation_stack_auto_read(self) -> None:
        enabled = not bool(self.operation_stack_auto_read_var.get())
        self.operation_stack_auto_read_var.set(enabled)
        if not enabled:
            self._cancel_operation_stack_auto_followup()
        self._refresh_operation_stack_auto_read_button()
        state = "enabled" if enabled else "disabled"
        self._log(f"Operation stack auto-read {state} for agent context.")

    def _refresh_operation_stack_auto_read_button(self) -> None:
        if not self.operation_stack_auto_read_button:
            return
        enabled = bool(self.operation_stack_auto_read_var.get())
        self.operation_stack_auto_read_button.configure(text=f"Read Stack: {'On' if enabled else 'Off'}")

    def _push_operation_source(self, source: str) -> None:
        normalized = str(source or "").strip().lower()
        if normalized not in {"user", "agent", "system"}:
            raise AssertionError(f"Unsupported operation source: {source}")
        self.operation_source_stack.append(normalized)

    def _pop_operation_source(self) -> None:
        if not self.operation_source_stack:
            raise AssertionError("Operation source stack is empty.")
        self.operation_source_stack.pop()

    def _current_operation_source(self) -> str:
        if not self.operation_source_stack:
            return "user"
        return str(self.operation_source_stack[-1] or "user")

    def _should_record_operation_message(self, source: str, message: str) -> bool:
        normalized_source = str(source or "").strip().lower()
        compact = " ".join(str(message or "").split()).strip()
        if not compact:
            return False
        lowered = compact.lower()
        if normalized_source == "agent":
            return False
        if normalized_source == "system":
            return False
        if lowered.startswith("highlight:"):
            return False
        if lowered.startswith("chat "):
            return False
        if lowered.startswith("chatbox "):
            return False
        if lowered.startswith("function assistant "):
            return False
        if lowered.startswith("request cancelled."):
            return False
        if lowered.startswith("drop ignored outside the chat input."):
            return False
        if lowered.startswith("attached ") and " item(s) via " in lowered:
            return False
        ignored_prefixes = (
            "algorithm studio started.",
            "runtime log file:",
            "chatbox ready.",
            "chat prompt filled from current selection.",
            "chat history cleared.",
            "connected agent source:",
            "switched agent source to",
            "switched model to",
            "switched approval mode to",
            "manual approval mode bypassed",
            "chat request sent via",
            "auto-continue request sent via",
            "agent call completed via",
            "approval check failed:",
            "deepseek history compaction completed.",
            "deepseek history compaction failed:",
            "starting deepseek history compaction checkpoint.",
            "requested early termination for the active chat session.",
            "early termination was requested, but the current provider could not be interrupted immediately.",
            "interface4agents failed:",
            "agent tool failed:",
        )
        return not lowered.startswith(ignored_prefixes)

    def _record_operation_from_log(self, message: str) -> None:
        source = self._current_operation_source()
        if not self._should_record_operation_message(source, message):
            return
        self._record_operation(source, message)

    def _record_operation(self, source: str, message: str) -> None:
        normalized_source = str(source or "").strip().lower()
        if normalized_source not in {"user", "agent", "system"}:
            raise AssertionError(f"Unsupported operation source: {source}")
        compact = " ".join(str(message or "").split()).strip()
        if not compact:
            raise AssertionError("Operation message cannot be empty.")
        if normalized_source == "agent" and compact.lower().startswith("highlight:"):
            return
        entry = {
            "timestamp": datetime.now().strftime("%H:%M:%S"),
            "source": normalized_source,
            "message": compact,
        }
        self.operation_stack_entries.insert(0, entry)
        max_entries = 200
        if len(self.operation_stack_entries) > max_entries:
            del self.operation_stack_entries[max_entries:]
        self._refresh_operation_stack_panel()
        if normalized_source == "user" and hasattr(self, "_schedule_operation_stack_auto_followup"):
            self._schedule_operation_stack_auto_followup()

    def _refresh_operation_stack_panel(self) -> None:
        if not self.operation_stack_text:
            return
        widget = self.operation_stack_text
        widget.configure(state="normal")
        widget.delete("1.0", tk.END)
        if not self.operation_stack_entries:
            widget.insert("1.0", "No operations yet.\n\nUser and agent actions will appear here.", ("empty",))
            widget.configure(state="disabled")
            return
        for index, entry in enumerate(self.operation_stack_entries):
            source = str(entry["source"]).upper()
            timestamp = str(entry["timestamp"])
            message = str(entry["message"])
            tag = f"operation_{entry['source']}"
            widget.insert(tk.END, f"[{timestamp}] {source}\n", ("operation_header", tag))
            widget.insert(tk.END, message + "\n", (tag,))
            if index < len(self.operation_stack_entries) - 1:
                widget.insert(tk.END, "\n")
        widget.configure(state="disabled")

    def _recent_operation_stack_text(self, limit: int = 12) -> str:
        if limit <= 0:
            raise AssertionError("Operation stack context limit must be positive.")
        if not self.operation_stack_entries:
            return "(empty)"
        lines: list[str] = []
        recent_entries = list(reversed(self.operation_stack_entries[:limit]))
        for entry in recent_entries:
            lines.append(
                f"[{entry['timestamp']}] {str(entry['source']).upper()}: {entry['message']}"
            )
        return "\n".join(lines)

    def _operation_stack_context_block(self, limit: int = 12) -> str:
        if not bool(self.operation_stack_auto_read_var.get()):
            return ""
        return "\n".join(
            [
                "Recent operation stack (read this first; use it as the latest source of truth for user progress and the next teaching step):",
                self._recent_operation_stack_text(limit=limit),
            ]
        )
