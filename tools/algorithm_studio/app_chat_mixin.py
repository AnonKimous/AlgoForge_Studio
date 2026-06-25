from __future__ import annotations

import mimetypes
import os
import re
import time
from pathlib import Path
import tkinter as tk
from typing import Any
from urllib.parse import unquote, urlparse

try:
    from .shared import COLORS, PROJECT_ROOT
except ImportError:
    from shared import COLORS, PROJECT_ROOT


class AlgorithmStudioChatMixin:
    def _cancel_chat_message_hide(self, message_id: int) -> None:
        after_id = self.chat_controls_hide_after_ids.pop(message_id, None)
        if after_id is None:
            return
        self.root.after_cancel(after_id)

    def _set_chat_message_controls_visible(self, message_id: int, visible: bool) -> None:
        controls = self.chat_message_controls.get(message_id)
        if controls is None:
            return
        buttons: list[tk.Widget] = []
        for widget in controls.get("buttons", []):
            if not isinstance(widget, tk.Widget):
                continue
            if not widget.winfo_exists():
                continue
            buttons.append(widget)
        if visible:
            if bool(controls.get("visible")):
                return
            for index, button in enumerate(buttons):
                if not button.winfo_manager():
                    button.pack(side="left", padx=(0, 6) if index < len(buttons) - 1 else (0, 0))
            controls["visible"] = True
            return
        if not bool(controls.get("visible")):
            return
        for button in buttons:
            if button.winfo_manager():
                button.pack_forget()
        controls["visible"] = False

    def _show_chat_message_controls(self, message_id: int) -> None:
        self._cancel_chat_message_hide(message_id)
        self._set_chat_message_controls_visible(message_id, True)

    def _hide_chat_message_controls(self, message_id: int) -> None:
        self._cancel_chat_message_hide(message_id)
        self._set_chat_message_controls_visible(message_id, False)

    def _schedule_hide_chat_message_controls(self, message_id: int, delay_ms: int = 120) -> None:
        self._cancel_chat_message_hide(message_id)

        def hide() -> None:
            self.chat_controls_hide_after_ids.pop(message_id, None)
            self._set_chat_message_controls_visible(message_id, False)

        self.chat_controls_hide_after_ids[message_id] = self.root.after(delay_ms, hide)

    def _bind_chat_message_controls_hover(self, message_id: int, message_tag: str, widgets: list[tk.Widget]) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        self.chat_history_text.tag_bind(message_tag, "<Enter>", lambda _event, current_id=message_id: self._show_chat_message_controls(current_id))
        self.chat_history_text.tag_bind(message_tag, "<Leave>", lambda _event, current_id=message_id: self._schedule_hide_chat_message_controls(current_id))
        for widget in widgets:
            widget.bind("<Enter>", lambda _event, current_id=message_id: self._show_chat_message_controls(current_id))
            widget.bind("<Leave>", lambda _event, current_id=message_id: self._schedule_hide_chat_message_controls(current_id))

    def _open_chat_attachment(self, path_text: str) -> None:
        path = Path(path_text)
        if not path.exists():
            raise FileNotFoundError(f"Attachment not found: {path}")
        os.startfile(str(path))

    def _resolve_chat_output_attachment(self, candidate: str) -> dict[str, str] | None:
        path_text = str(candidate).strip().strip("`").strip()
        if not path_text:
            return None
        if path_text.startswith("<") and path_text.endswith(">"):
            path_text = path_text[1:-1].strip()
        lowered = path_text.lower()
        if "://" in path_text and not lowered.startswith("file://"):
            return None
        if lowered.startswith("file://"):
            parsed = urlparse(path_text)
            parsed_path = unquote(parsed.path or "").strip()
            if parsed_path.startswith("/") and len(parsed_path) > 2 and parsed_path[2] == ":":
                parsed_path = parsed_path[1:]
            path_text = parsed_path
        path = Path(path_text)
        if not path.is_absolute():
            path = (PROJECT_ROOT / path_text).resolve()
        if not path.exists():
            return None
        return {
            "path": str(path),
            "name": path.name,
            "kind": self._chat_attachment_kind(path),
            "mime_type": mimetypes.guess_type(str(path))[0] or "application/octet-stream",
        }

    def _insert_chat_attachment_link(self, tag: str, attachment: dict[str, str], index: int) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        line_tag = f"{tag}_attachment_{index}_{int(time.time() * 1000)}"
        label = f"[{attachment['kind']}] {attachment['name']}"
        start_index = self.chat_history_text.index(tk.END)
        self.chat_history_text.insert(tk.END, label + "\n", (tag,))
        end_index = self.chat_history_text.index(tk.END)
        self.chat_history_text.tag_add(line_tag, start_index, end_index)
        self.chat_history_text.tag_configure(
            line_tag,
            foreground=COLORS["accent"] if attachment["kind"] == "image" else COLORS["muted"],
            underline=True,
        )
        self.chat_history_text.tag_bind(line_tag, "<Button-1>", lambda _event, path=attachment["path"]: self._open_chat_attachment(path))

    def _insert_chat_attachment_preview(self, attachment: dict[str, str]) -> None:
        if not self.chat_history_text or attachment["kind"] != "image":
            return
        try:
            image = tk.PhotoImage(file=attachment["path"])
        except tk.TclError:
            return
        max_width = 280
        max_height = 180
        factor_x = max(1, (image.width() + max_width - 1) // max_width)
        factor_y = max(1, (image.height() + max_height - 1) // max_height)
        factor = max(factor_x, factor_y)
        if factor > 1:
            image = image.subsample(factor, factor)
        self.chat_rendered_images.append(image)
        self.chat_history_text.image_create(tk.END, image=image, padx=16, pady=4)
        self.chat_history_text.insert(tk.END, "\n")

    def _insert_chat_code_block(self, tag: str, language: str, code: str) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        block_name = language.strip().lower() or "code"
        self.chat_history_text.insert(tk.END, f"[{block_name}]\n", (tag, "chat_code_label"))
        block_text = code.rstrip("\n")
        if block_text:
            self.chat_history_text.insert(tk.END, block_text + "\n", (tag, "chat_code"))

    def _render_chat_message_content(self, tag: str, content: str) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        if not content:
            return
        code_block_pattern = re.compile(r"```([^\n`]*)\n(.*?)```", re.DOTALL)
        cursor = 0
        for match in code_block_pattern.finditer(content):
            plain_text = content[cursor:match.start()]
            if plain_text:
                self.chat_history_text.insert(tk.END, plain_text, (tag,))
            self._insert_chat_code_block(tag, match.group(1), match.group(2))
            cursor = match.end()
        tail = content[cursor:]
        if tail:
            self.chat_history_text.insert(tk.END, tail, (tag,))

    def _detect_output_attachments(self, content: str) -> list[dict[str, str]]:
        attachments: list[dict[str, str]] = []
        seen_paths: set[str] = set()

        def add_candidate(candidate: str) -> None:
            attachment = self._resolve_chat_output_attachment(candidate)
            if attachment is None:
                return
            normalized = attachment["path"]
            if normalized in seen_paths:
                return
            seen_paths.add(normalized)
            attachments.append(attachment)

        for match in re.finditer(r"!\[[^\]]*\]\(([^)]+)\)", content):
            add_candidate(match.group(1))
        for match in re.finditer(r"(?<!!)\[[^\]]+\]\(([^)]+)\)", content):
            add_candidate(match.group(1))
        for raw_line in content.splitlines():
            line = raw_line.strip().strip("`")
            if not line:
                continue
            lowered = line.lower()
            if Path(line).is_absolute() or lowered.startswith("file://") or line.startswith("./") or line.startswith(".\\") or line.startswith("../") or line.startswith("..\\"):
                add_candidate(line)
        return attachments

    def _copy_chat_message_to_clipboard(self, message_id: int) -> None:
        message = next((item for item in self.chat_history if int(item.get("id", -1)) == message_id), None)
        if message is None:
            raise AssertionError(f"Chat message {message_id} was not found.")
        lines = [str(message.get("content") or "")]
        attachments = message.get("attachments") or []
        if attachments:
            lines.append("")
            lines.append("Attachments:")
            for attachment in attachments:
                lines.append(str(attachment.get("path") or attachment.get("name") or ""))
        text = "\n".join(lines).strip()
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        self.status_var.set("Copied message to the clipboard.")

    def _terminate_active_chat_request(self) -> None:
        if not self.chat_busy:
            self.status_var.set("No active chat request to terminate.")
            return
        if self.pending_chat_request_message_id is not None:
            controls = self.chat_message_controls.get(self.pending_chat_request_message_id)
            terminate_button = None if controls is None else controls.get("terminate")
            if terminate_button is not None:
                terminate_button.configure(state="disabled")
        cancelled = self.agent_client.request_cancel()
        if cancelled:
            self._log("Requested early termination for the active chat session.")
            self.status_var.set("Termination requested; waiting for the agent to stop.")
        else:
            self._log("Early termination was requested, but the current provider could not be interrupted immediately.")
            self.status_var.set("Termination requested; the provider will stop after the current step.")

    def _insert_chat_message_controls(self, message: dict[str, Any], message_tag: str) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        message_id = int(message["id"])
        role = str(message.get("role") or "system")
        controls_frame = tk.Frame(self.chat_history_text, bg=COLORS["canvas"])
        copy_button = tk.Button(
            controls_frame,
            text="Copy",
            command=lambda current_id=message_id: self._copy_chat_message_to_clipboard(current_id),
            bg=COLORS["canvas"],
            fg="#9fd3ff",
            activebackground=COLORS["canvas"],
            activeforeground="#d8eeff",
            relief="flat",
            bd=0,
            highlightthickness=0,
            font=("Segoe UI", 8, "underline"),
            padx=1,
            pady=0,
            cursor="hand2",
        )
        controls: dict[str, Any] = {"copy": copy_button}
        buttons: list[tk.Widget] = [copy_button]
        if role == "user" and self.pending_chat_request_message_id == message_id:
            terminate_button = tk.Button(
                controls_frame,
                text="Terminate",
                command=self._terminate_active_chat_request,
                bg=COLORS["canvas"],
                fg="#9fd3ff",
                activebackground=COLORS["canvas"],
                activeforeground="#d8eeff",
                relief="flat",
                bd=0,
                highlightthickness=0,
                font=("Segoe UI", 8, "underline"),
                padx=1,
                pady=0,
                cursor="hand2",
            )
            controls["terminate"] = terminate_button
            buttons.append(terminate_button)
        self.chat_embedded_widgets.append(controls_frame)
        self.chat_embedded_widgets.extend(buttons)
        controls["frame"] = controls_frame
        controls["buttons"] = buttons
        controls["visible"] = False
        self.chat_message_controls[message_id] = controls
        self.chat_history_text.window_create(tk.END, window=controls_frame)
        self.chat_history_text.insert(tk.END, "\n")
        self._bind_chat_message_controls_hover(message_id, message_tag, [controls_frame, *buttons])
        self._set_chat_message_controls_visible(message_id, False)

    def _refresh_pending_chat_message_controls(self) -> None:
        for message_id, controls in list(self.chat_message_controls.items()):
            terminate_button = controls.get("terminate")
            if not terminate_button:
                continue
            if self.pending_chat_request_message_id == message_id and self.chat_busy:
                terminate_button.configure(state="normal")
                continue
            try:
                terminate_button.destroy()
            except tk.TclError:
                pass
            del controls["terminate"]

    def _append_chat_message(
        self,
        role: str,
        content: str,
        *,
        attachments: list[dict[str, str]] | None = None,
    ) -> int:
        message_attachments = [dict(item) for item in attachments or []]
        if role == "assistant" and not message_attachments:
            message_attachments = self._detect_output_attachments(content)
        self.chat_message_serial += 1
        message = {
            "id": self.chat_message_serial,
            "role": role,
            "content": content,
            "attachments": message_attachments,
        }
        self.chat_history.append(message)
        if not self.chat_history_text:
            return int(message["id"])
        was_pinned = self._chat_history_is_pinned_to_bottom()
        self.chat_history_text.configure(state="normal")
        tag = role if role in {"user", "assistant", "system", "error"} else "system"
        message_tag = f"chat_message_{int(message['id'])}"
        start_index = self.chat_history_text.index(tk.END)
        rendered_content = content.rstrip()
        if rendered_content:
            self._render_chat_message_content(tag, rendered_content)
            self.chat_history_text.insert(tk.END, "\n", (tag,))
        for index, attachment in enumerate(message_attachments):
            self._insert_chat_attachment_link(tag, attachment, index)
            self._insert_chat_attachment_preview(attachment)
        end_index = self.chat_history_text.index(tk.END)
        self.chat_history_text.tag_add(message_tag, start_index, end_index)
        if role in {"user", "assistant"}:
            self._insert_chat_message_controls(message, message_tag)
        self.chat_history_text.insert(tk.END, "\n", (tag,))
        self.chat_history_text.configure(state="disabled")
        self._restore_chat_history_bottom_pin(was_pinned)
        self._refresh_pending_chat_message_controls()
        return int(message["id"])

    def _chat_history_transcript_line(self, item: dict[str, Any]) -> str:
        role = str(item.get("role") or "system").capitalize()
        content = str(item.get("content") or "").strip() or "(no text)"
        attachments = item.get("attachments") or []
        if attachments:
            names = ", ".join(str(entry.get("name") or Path(str(entry.get("path") or "")).name) for entry in attachments)
            return f"{role}: {content} [Attachments: {names}]"
        return f"{role}: {content}"
