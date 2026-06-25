from __future__ import annotations

import re
import threading
import tkinter as tk
from tkinter import messagebox

try:
    from .approval_rules import evaluate_access_rules, load_access_rules
except ImportError:
    from approval_rules import evaluate_access_rules, load_access_rules


class AlgorithmStudioChatRequestMixin:
    def _strip_interface4agents_blocks(self, text: str) -> str:
        return re.sub(r"```interface4agents\s*.*?```", "", str(text or ""), flags=re.IGNORECASE | re.DOTALL)

    def _compact_followup_history_for_model(self) -> str:
        items = self._chat_user_assistant_items()
        if not items:
            return "(empty)"
        first_user: dict[str, object] | None = None
        latest_assistant: dict[str, object] | None = None
        for item in items:
            if str(item.get("role") or "") == "user":
                first_user = item
                break
        for item in reversed(items):
            if str(item.get("role") or "") == "assistant":
                latest_assistant = item
                break
        sections: list[str] = []
        if first_user is not None:
            user_text = self._strip_interface4agents_blocks(str(first_user.get("content") or "")).strip()
            compact_user = self._compact_activity_text(user_text, limit=320)
            if compact_user:
                sections.append("Original teaching goal:\n" + compact_user)
        if latest_assistant is not None:
            assistant_text = self._strip_interface4agents_blocks(str(latest_assistant.get("content") or "")).strip()
            compact_assistant = self._compact_activity_text(assistant_text, limit=420)
            if compact_assistant:
                sections.append("Latest teaching step:\n" + compact_assistant)
        return "\n\n".join(sections) or "(empty)"

    def _send_chat_message_from_event(self, event: tk.Event) -> str:
        self._send_chat_message()
        return "break"

    def _insert_chat_newline_from_event(self, event: tk.Event) -> str:
        if not self.chat_input_text:
            raise AssertionError("Chat input box is not initialized.")
        self.chat_input_text.insert(tk.INSERT, "\n")
        return "break"

    def _build_chat_request_prompt(self, prompt_for_model: str, *, compact_history: bool = False) -> tuple[str, str]:
        self._sync_agent_client_settings()
        selection = self._selection_label()
        context = self._chat_selected_context()
        transcript = self._compact_followup_history_for_model() if compact_history else self._chat_history_for_model()
        prompt_sections = [context]
        if transcript and transcript != "(empty)":
            prompt_sections.extend(
                [
                    "Conversation history:",
                    transcript,
                ]
            )
        prompt_sections.extend(
            [
                "User request:",
                prompt_for_model,
            ]
        )
        final_prompt = "\n\n".join(prompt_sections).strip()
        self.agent_prompt_var.set(final_prompt)
        return selection, final_prompt

    def _start_chat_request(
        self,
        *,
        prompt_for_model: str,
        user_visible_content: str,
        attachments: list[dict[str, str]] | None = None,
        append_user_message: bool,
        status_text: str,
        log_message: str,
        compact_history: bool = False,
    ) -> None:
        if self.chat_busy:
            self._log("Agent request already in progress.")
            return
        resolved_attachments = [dict(item) for item in attachments or []]
        selection, final_prompt = self._build_chat_request_prompt(prompt_for_model, compact_history=compact_history)
        try:
            approved = self._authorize_chat_request(selection, final_prompt)
        except Exception as exc:  # noqa: BLE001
            message = self._compact_activity_text(str(exc), limit=120) or "审批检查失败。"
            self._finish_execution_trace("聊天请求", False, f"审批失败：{message}")
            self._append_chat_message("error", message)
            self._log(f"Approval check failed: {message}")
            self.status_var.set("Approval check failed.")
            messagebox.showerror("Approval error", message)
            return
        if not approved:
            self._finish_execution_trace("聊天请求", False, "审批未通过。")
            return
        if append_user_message:
            self.pending_chat_request_message_id = self.chat_message_serial + 1
        else:
            self.pending_chat_request_message_id = None
        self.chat_busy = True
        if append_user_message:
            self._append_chat_message("user", user_visible_content, attachments=resolved_attachments)
        if self.chat_input_text:
            self.chat_input_text.delete("1.0", tk.END)
        self._clear_chat_attachments()
        if self.chat_send_button:
            self.chat_send_button.configure(state="disabled")
        self.status_var.set(status_text)
        self._log(log_message)

        def emit_event(event: dict[str, str]) -> None:
            self.root.after(0, lambda event=event: self._handle_agent_event(event))

        def worker() -> None:
            try:
                response = self.agent_client.generate(
                    self.project,
                    selection,
                    final_prompt,
                    attachments=resolved_attachments,
                    event_callback=emit_event,
                )
            except Exception as exc:  # noqa: BLE001
                self.root.after(0, lambda exc=exc: self._finish_chat_request_error(exc))
                return
            self.root.after(0, lambda response=response: self._finish_chat_request_success(response))

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()

    def _send_chat_message(self) -> None:
        if not self.chat_input_text:
            raise AssertionError("Chat input box is not initialized.")
        prompt = self.chat_input_text.get("1.0", tk.END).strip()
        attachments = [dict(item) for item in self.chat_attachments]
        if not prompt and not attachments:
            raise AssertionError("Chat prompt is empty.")
        self._start_chat_request(
            prompt_for_model=prompt or "Please inspect the attached content.",
            user_visible_content=prompt or "(attached content)",
            attachments=attachments,
            append_user_message=True,
            status_text=f"Sent to {self.agent_client.provider}; waiting for reply...",
            log_message=f"Chat request sent via {self.agent_client.provider}.",
            compact_history=False,
        )

    def _latest_assistant_message_content(self) -> str:
        for item in reversed(self.chat_history):
            if str(item.get("role") or "") == "assistant":
                return str(item.get("content") or "")
        return ""

    def _should_auto_continue_from_operation_stack(self) -> bool:
        if not bool(self.operation_stack_auto_read_var.get()):
            return False
        if self.chat_busy:
            return False
        if not self.chat_history:
            return False
        latest_item = self.chat_history[-1]
        if str(latest_item.get("role") or "") != "assistant":
            return False
        assistant_text = " ".join(self._latest_assistant_message_content().split()).lower()
        if not assistant_text:
            return False
        trigger_tokens = (
            "操作栈",
            "operation stack",
            "read stack",
            "完成后",
            "完成这一步",
            "确认这一步",
            "我再带你",
            "我会等",
        )
        if not any(token in assistant_text for token in trigger_tokens):
            return False
        return True

    def _operation_stack_auto_followup_prompt(self) -> str:
        return "\n".join(
            [
                "Continue the current step-by-step UI teaching flow.",
                "First read the latest operation stack in the context above.",
                "If the latest user operations complete your previous instruction, move to the next single step.",
                "Before explaining that next step, emit exactly one matching interface4agents highlight for the UI target when supported.",
                "If the latest user operations do not complete your previous instruction, stay on the same step and re-highlight only that same UI target.",
                "Do not ask the user to manually confirm an action if the operation stack already shows it.",
            ]
        )

    def _schedule_operation_stack_auto_followup(self) -> None:
        if not self._should_auto_continue_from_operation_stack():
            return
        signature = self._recent_operation_stack_text(limit=6)
        if signature == self.operation_stack_auto_followup_signature:
            return
        if self.operation_stack_auto_followup_after_id is not None:
            try:
                self.root.after_cancel(self.operation_stack_auto_followup_after_id)
            except Exception:
                pass
            self.operation_stack_auto_followup_after_id = None

        def run_followup(expected_signature: str = signature) -> None:
            self.operation_stack_auto_followup_after_id = None
            if not self._should_auto_continue_from_operation_stack():
                return
            self.operation_stack_auto_followup_signature = expected_signature
            self._start_chat_request(
                prompt_for_model=self._operation_stack_auto_followup_prompt(),
                user_visible_content="",
                attachments=[],
                append_user_message=False,
                status_text=f"Operation stack updated; asking {self.agent_client.provider} to continue...",
                log_message=f"Auto-continue request sent via {self.agent_client.provider} from operation stack.",
                compact_history=True,
            )

        self.operation_stack_auto_followup_after_id = self.root.after(450, run_followup)


    def _authorize_chat_request(self, selection: str, final_prompt: str) -> bool:
        mode = self.approval_mode_var.get().strip().lower() or "manual"
        approval_summary = "\n".join(
            [
                f"Provider: {self.agent_client.provider}",
                f"Model: {self.agent_client.model}",
                f"Selection: {selection}",
                "",
                "Prompt:",
                final_prompt,
            ]
        )
        if mode == "manual":
            self._log("Manual approval mode bypassed for chat request.")
            return True
        if mode == "rules":
            rule_set = load_access_rules(self.access_rules_path)
            decision = evaluate_access_rules(rule_set, approval_summary)
            if decision.outcome == "deny":
                self._log(f"Chat request denied by rules: {decision.reason}")
                self.status_var.set("Request denied by access rules.")
                messagebox.showwarning("Approval denied", decision.reason)
                return False
            if decision.outcome == "manual":
                self._log(f"Rule mode fell back to auto-approve: {decision.reason}")
                return True
            if decision.approved:
                self._log(
                    "Chat request approved by rules"
                    + (f" ({decision.matched_rule})" if decision.matched_rule else ".")
                )
                return True
            raise AssertionError(f"Unsupported approval decision: {decision.outcome}")
        raise AssertionError(f"Unsupported approval mode: {mode}")

    def _release_chat_request(self) -> None:
        self.chat_busy = False
        self.pending_chat_request_message_id = None
        if self.chat_send_button:
            self.chat_send_button.configure(state="normal")
        self._refresh_pending_chat_message_controls()

    def _finish_deepseek_history_compaction_success(self, summary: str, compacted_item_count: int) -> None:
        self.chat_history_compaction_summary = summary.strip()
        self.chat_history_compacted_item_count = compacted_item_count
        self._append_chat_message("system", "DeepSeek conversation history compacted at the 100-turn checkpoint.")
        self._log("DeepSeek history compaction completed.")
        self.status_var.set(f"Agent reply received from {self.agent_client.provider}.")
        self._release_chat_request()

    def _finish_deepseek_history_compaction_error(self, exc: Exception) -> None:
        message = self._compact_activity_text(str(exc), limit=160) or "DeepSeek history compaction failed."
        self._append_chat_message("error", message)
        self._log(f"DeepSeek history compaction failed: {message}")
        self.status_var.set("DeepSeek history compaction failed.")
        self._release_chat_request()

    def _start_deepseek_history_compaction(self) -> None:
        compaction_input = self._chat_history_for_compaction()
        compacted_item_count = len(self._chat_user_assistant_items())
        self.status_var.set("Compacting DeepSeek conversation history...")
        self._log("Starting DeepSeek history compaction checkpoint.")

        def worker() -> None:
            try:
                summary = self.agent_client.compact_history(compaction_input)
            except Exception as exc:  # noqa: BLE001
                self.root.after(0, lambda exc=exc: self._finish_deepseek_history_compaction_error(exc))
                return
            self.root.after(
                0,
                lambda summary=summary, compacted_item_count=compacted_item_count: self._finish_deepseek_history_compaction_success(
                    summary,
                    compacted_item_count,
                ),
            )

        threading.Thread(target=worker, daemon=True).start()

    def _finish_chat_request_success(self, response: str) -> None:
        try:
            visible_response = self._consume_agent_tool_response(response)
        except Exception as exc:  # noqa: BLE001
            self._finish_chat_request_error(exc)
            return
        self._append_chat_message("assistant", visible_response)
        self.agent_output_var.set(visible_response)
        self._finish_execution_trace("聊天请求", True, f"已收到 {len(response.strip())} 个字符的回复。")
        self._log(f"Agent call completed via {self.agent_client.provider}.")
        if self.agent_client.should_compact_deepseek_history(self._chat_request_count()):
            try:
                self._start_deepseek_history_compaction()
            except Exception as exc:  # noqa: BLE001
                self._finish_deepseek_history_compaction_error(exc)
            return
        self.status_var.set(f"Agent reply received from {self.agent_client.provider}.")
        self._release_chat_request()

    def _finish_chat_request_error(self, exc: Exception) -> None:
        self._release_chat_request()
        message = self._compact_activity_text(str(exc), limit=120) or "Agent call failed."
        if message == "Request cancelled.":
            self._append_chat_message("system", "Request cancelled.")
            self.agent_output_var.set(message)
            self._finish_execution_trace("聊天请求", False, "会话已提前终止。")
            self._log("Agent call cancelled by the user.")
            self.status_var.set("Chat request terminated early.")
            return
        self._append_chat_message("error", message)
        self.agent_output_var.set(message)
        self._finish_execution_trace("聊天请求", False, f"执行失败：{message}")
        self._log(f"Agent call failed: {message}")
        self.status_var.set("Agent call failed.")

    def _selection_label(self) -> str:
        if self.selected_container_group_name:
            return f"containerElement:{self.selected_container_group_name}"
        if self.selected_container_name:
            return f"container:{self.selected_container_name}"
        if self.selected_rule_name:
            return f"rule:{self.selected_rule_name}"
        if self.selected_reflector_name:
            return f"reflector:{self.selected_reflector_name}"
        if self.selected_res_node_name:
            return f"resnode:{self.selected_res_node_name}"
        if self.selected_function_name:
            return f"function:{self.selected_function_name}"
        if self.selected_function_text_name:
            return f"functiontext:{self.selected_function_text_name}"
        if self.selected_stage_name:
            return f"interventioner:{self.selected_stage_name}"
        return "project"
