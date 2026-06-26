from __future__ import annotations

import base64
import json
import mimetypes
import os
import re
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any, Callable
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

try:
    from .backend import ProjectState
    from .interface4agents import build_interface4agents_prompt
    from .shared import PROJECT_ROOT, _resolve_codex_command
except ImportError:
    from backend import ProjectState
    from interface4agents import build_interface4agents_prompt
    from shared import PROJECT_ROOT, _resolve_codex_command


MAX_INLINE_ATTACHMENT_BYTES = 4 * 1024 * 1024
PLAN_SUMMARY_PROMPT_CHAR_THRESHOLD = 6000
PLAN_SUMMARY_MANIFEST_CHAR_THRESHOLD = 12000
PLAN_SUMMARY_COMBINED_CHAR_THRESHOLD = 18000
OPENAI_RESPONSES_COMPACT_THRESHOLD = 64000
DEEPSEEK_HISTORY_COMPACTION_INTERVAL = 100
AgentEventCallback = Callable[[dict[str, str]], None]
LOCAL_AGENTS_PATH = Path(__file__).resolve().parents[1] / "AGENTS.md"


def _emit_agent_event(callback: AgentEventCallback | None, event_type: str, **payload: str) -> None:
    if callback is None:
        return
    event = {"type": event_type}
    event.update({key: str(value) for key, value in payload.items() if value is not None})
    callback(event)


class MockAgentClient:
    def __init__(self) -> None:
        self.provider = "codex"
        self.model = "gpt-5.4-mini"
        self.approval_mode = "manual"
        self.base_url = ""
        self.api_key = ""
        self.codex_command = "codex"
        self.timeout_seconds: float | None = None
        self.staged_execution_enabled = False
        self.context_compression_enabled = True
        self._provider_session_signature: tuple[str, str, str] | None = None
        self._openai_previous_response_id: str | None = None
        self._codex_session_id: str | None = None
        self._active_process: subprocess.Popen[str] | None = None
        self._cancel_requested = False

    def reset_session_state(self) -> None:
        self._provider_session_signature = None
        self._openai_previous_response_id = None
        self._codex_session_id = None

    def request_cancel(self) -> bool:
        self._cancel_requested = True
        process = self._active_process
        if process is None or process.poll() is not None:
            return False
        process.terminate()
        return True

    def _provider_signature(self) -> tuple[str, str, str]:
        mode = self.provider.strip().lower()
        if mode == "codex":
            return (
                mode,
                self.codex_command.strip(),
                self.model.strip(),
            )
        return (
            mode,
            self.base_url.strip(),
            self.model.strip(),
        )

    def _sync_provider_session_state(self) -> None:
        signature = self._provider_signature()
        if signature != self._provider_session_signature:
            self._provider_session_signature = signature
            self._openai_previous_response_id = None
            self._codex_session_id = None

    def _api_provider_kind(self) -> str:
        base_url = self.base_url.strip()
        if not base_url:
            return "generic"
        parsed = urlparse(base_url)
        host = (parsed.netloc or parsed.path or "").strip().lower()
        if host.endswith("openai.com") or ".openai.com" in host:
            return "openai"
        if "deepseek.com" in host:
            return "deepseek"
        return "generic"

    def conversation_history_mode(self) -> str:
        mode = self.provider.strip().lower()
        if mode == "codex" and self._codex_session_id:
            return "server_managed"
        if mode != "api":
            return "local_adaptive"
        api_kind = self._api_provider_kind()
        if api_kind == "openai":
            return "server_managed"
        if api_kind == "deepseek":
            return "checkpoint_summary"
        return "local_adaptive"

    def should_compact_deepseek_history(self, completed_request_count: int) -> bool:
        return (
            self.provider.strip().lower() == "api"
            and self._api_provider_kind() == "deepseek"
            and completed_request_count > 0
            and completed_request_count % DEEPSEEK_HISTORY_COMPACTION_INTERVAL == 0
        )

    def _project_snapshot(self, project: ProjectState) -> str:
        lines = [
            f"algorithm={project.algorithm_name}",
            f"package={project.package_name}",
        ]
        if project.containers:
            containers = []
            for item in project.containers[:8]:
                prefix = "v" if item.kind == "variable" else "a"
                containers.append(f"{prefix}:{item.name}")
            suffix = "" if len(project.containers) <= 8 else f" ... ({len(project.containers)} total)"
            lines.append("containers=" + ", ".join(containers) + suffix)
        else:
            lines.append("containers=(none)")
        if project.container_groups:
            names = ", ".join(item.name for item in project.container_groups[:6])
            suffix = "" if len(project.container_groups) <= 6 else f" ... ({len(project.container_groups)} total)"
            lines.append("container_groups=" + names + suffix)
        if project.decomposer_rules:
            names = ", ".join(item.name for item in project.decomposer_rules[:6])
            suffix = "" if len(project.decomposer_rules) <= 6 else f" ... ({len(project.decomposer_rules)} total)"
            lines.append("rules=" + names + suffix)
        if project.reflector_items:
            names = ", ".join(item.name for item in project.reflector_items[:6])
            suffix = "" if len(project.reflector_items) <= 6 else f" ... ({len(project.reflector_items)} total)"
            lines.append("reflectors=" + names + suffix)
        if project.function_frames:
            functions = []
            for item in project.function_frames[:6]:
                snippet = " ".join(part.strip() for part in item.script.splitlines() if part.strip())[:48]
                if snippet:
                    functions.append(f"{item.name}<{item.input_name}->{item.output_name}>:{snippet}")
                else:
                    functions.append(f"{item.name}<{item.input_name}->{item.output_name}>")
            suffix = "" if len(project.function_frames) <= 6 else f" ... ({len(project.function_frames)} total)"
            lines.append("functions=" + " | ".join(functions) + suffix)
        if project.intervention_stages:
            stages = []
            for item in project.intervention_stages[:6]:
                stages.append(f"{item.name}<{item.kind}> funcs={','.join(item.functions) or '-'}")
            suffix = "" if len(project.intervention_stages) <= 6 else f" ... ({len(project.intervention_stages)} total)"
            lines.append("stages=" + " | ".join(stages) + suffix)
        if project.connections:
            preview = []
            for item in project.connections[:8]:
                preview.append(f"{item.source_kind}:{item.source_name}.{item.source_port}->{item.target_kind}:{item.target_name}.{item.target_port}")
            suffix = "" if len(project.connections) <= 8 else f" ... ({len(project.connections)} total)"
            lines.append("connections=" + " | ".join(preview) + suffix)
        return "\n".join(lines)

    def _extract_visible_text(self, response: str) -> str:
        tool_pattern = re.compile(r"```algorithm-studio-tool\s*(.*?)\s*```", re.IGNORECASE | re.DOTALL)
        command_pattern = re.compile(r"```interface4agents\s*(.*?)\s*```", re.IGNORECASE | re.DOTALL)
        stripped = tool_pattern.sub("", str(response or ""))
        stripped = command_pattern.sub("", stripped).strip()
        if stripped.startswith("{") and stripped.endswith("}"):
            try:
                payload = json.loads(stripped)
            except json.JSONDecodeError:
                return stripped
            if isinstance(payload, dict) and str(payload.get("tool") or "").strip():
                return ""
        return stripped

    def _compact_text(self, text: str, limit: int = 640) -> str:
        compact = " ".join(part.strip() for part in str(text).splitlines() if part.strip())
        if len(compact) > limit:
            return compact[: limit - 3].rstrip() + "..."
        return compact

    def _should_compress_planning_context(
        self,
        project: ProjectState,
        prompt: str,
        attachments: list[dict[str, str]],
    ) -> bool:
        if not self.context_compression_enabled:
            return False
        manifest_text = project.current_manifest_text().strip()
        prompt_text = str(prompt).strip()
        attachment_text_size = sum(len(block) for block in self._attachment_prompt_blocks(attachments))
        combined_size = len(manifest_text) + len(prompt_text) + attachment_text_size
        if len(prompt_text) >= PLAN_SUMMARY_PROMPT_CHAR_THRESHOLD:
            return True
        if len(manifest_text) >= PLAN_SUMMARY_MANIFEST_CHAR_THRESHOLD:
            return True
        return combined_size >= PLAN_SUMMARY_COMBINED_CHAR_THRESHOLD

    def _should_stage_request(self, selection: str, prompt: str) -> bool:
        return False
        if not self.staged_execution_enabled:
            return False
        if self.provider.strip().lower() not in {"api", "codex"}:
            return False
        normalized_selection = str(selection).strip().lower()
        if normalized_selection.startswith("function:"):
            return False
        normalized_prompt = str(prompt)
        lowered = normalized_prompt.lower()
        if "return plain text only." in lowered:
            return False
        if "do not emit algorithm-studio-tool blocks." in lowered or "use interface4agents only." in lowered:
            return False
        if "只返回代码" in normalized_prompt or "直接返回代码" in normalized_prompt:
            return False
        return True

    def _normalize_attachments(self, attachments: list[dict[str, str]] | None) -> list[dict[str, str]]:
        normalized: list[dict[str, str]] = []
        for entry in attachments or []:
            path_text = str(entry.get("path") or "").strip()
            if not path_text:
                raise AssertionError("Attachment path is empty.")
            path = Path(path_text)
            if not path.exists():
                raise FileNotFoundError(f"Attachment not found: {path}")
            mime_type = str(entry.get("mime_type") or mimetypes.guess_type(str(path))[0] or "application/octet-stream")
            kind = str(entry.get("kind") or ("image" if mime_type.startswith("image/") else "file")).strip() or "file"
            normalized.append(
                {
                    "path": str(path),
                    "name": path.name,
                    "kind": kind,
                    "mime_type": mime_type,
                }
            )
        return normalized

    def _read_attachment_bytes(self, path: Path) -> bytes:
        data = path.read_bytes()
        if len(data) > MAX_INLINE_ATTACHMENT_BYTES:
            raise RuntimeError(f"Attachment is too large to inline: {path.name}")
        return data

    def _read_text_attachment(self, attachment: dict[str, str]) -> str:
        path = Path(attachment["path"])
        data = self._read_attachment_bytes(path)
        try:
            return data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise RuntimeError(f"Attachment must be UTF-8 text or an image: {path.name}") from exc

    def _attachment_prompt_blocks(self, attachments: list[dict[str, str]]) -> list[str]:
        blocks: list[str] = []
        for attachment in attachments:
            if attachment["kind"] == "image":
                blocks.append(f"[Attached image] {attachment['name']}")
                continue
            text = self._read_text_attachment(attachment)
            blocks.append(
                "\n".join(
                    [
                        f"[Attached file] {attachment['name']}",
                        text.strip(),
                    ]
                ).strip()
            )
        return blocks

    def _workspace_agent_instructions(self) -> str:
        if not LOCAL_AGENTS_PATH.exists():
            return ""
        text = LOCAL_AGENTS_PATH.read_text(encoding="utf-8").strip()
        if not text:
            return ""
        return "\n".join(
            [
                "Workspace instructions:",
                text,
            ]
        )

    def _document_tool_instructions(self) -> str:
        return "\n".join(
            [
                build_interface4agents_prompt(),
                "",
                "Additional agent rules:",
                "- Use interface4agents only. algorithm-studio-tool and update_document are disabled.",
                "- Apply the smallest possible change that satisfies the request.",
                "- Do not invent extra nodes, stages, functions, reflectors, resource nodes, or scaffolding unless the user explicitly asks for them.",
                "- If the prompt includes a recent operation stack, read it before choosing the next teaching step.",
                "- If Read Stack is off and the task depends on stack-driven teaching, ask the user to enable it instead of guessing progress.",
                "- Expanded v/a nodes may contain internal layout fields. Use field commands when the user needs the container refined into smaller readable parts.",
                "- Layout field rule text is a free-form UI or algorithm contract such as `from v1 to x,y 16,16`; do not rewrite it into fixed float/int semantics unless the user explicitly asks for that interpretation.",
                "- Keep tutorial wording and generated explanations in English unless the user explicitly asks for another language.",
            ]
        ).strip()
        return "\n".join(
            [
                "Tools available:",
                "- ui_add_node",
                "- ui_update_node",
                "- ui_delete_node",
                "- ui_add_rule",
                "- update_document",
                "",
                "Prefer the UI tools first for scene changes. Use them to add, update, or delete nodes and to add rules.",
                "Only use update_document when the user explicitly wants raw document editing or when the requested change cannot be expressed safely with the UI tools.",
                "Apply the smallest possible change that satisfies the request.",
                "Do not invent extra nodes, resource nodes, stages, functions, reflectors, rules, or other scaffolding unless the user explicitly asks for them.",
                "Interpret bare Chinese '容器' as containerElement by default, not as a variable node.",
                "Use kind=container for that case.",
                "Use kind=variable only when the user explicitly asks for a variable/v node such as v1 or 变量节点.",
                "Use kind=array only when the user explicitly asks for an array/a node such as a1 or 数组节点.",
                "Keep meshNode minimal and only represent [mesh].",
                "Reuse singleton tool-like nodes instead of duplicating them when possible: container, decomposer, reflector, interventioner, meshNode, fun.",
                "For script-writing tasks around fun, prefer using the fun node and its linked functiontext node instead of rewriting the whole document.",
                "To call a tool, output a fenced code block labeled algorithm-studio-tool with a JSON object.",
                "Examples:",
                "```algorithm-studio-tool",
                '{"tool":"ui_add_node","kind":"container","name":"container","message":"Added container"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"ui_add_node","kind":"variable","name":"v1","count":1,"stride":4,"message":"Added v1"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"ui_update_node","kind":"variable","name":"v1","count":8,"message":"Updated v1 count"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"ui_update_node","kind":"function","name":"fun","script_language":"pseudocode","script":"draft script or pseudocode","message":"Updated fun script"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"ui_add_node","kind":"functiontext","function_name":"fun","text":"solution draft","message":"Added function text"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"ui_update_node","kind":"functiontext","name":"fun_text","text":"updated solution draft","message":"Updated function text"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"ui_delete_node","kind":"variable","name":"v1","message":"Deleted v1"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"ui_add_rule","name":"v1_to_v2","source":"v1","target":"v2","map_kind":"v2v","message":"Added rule"}',
                "```",
                "```algorithm-studio-tool",
                '{"tool":"update_document","document":{"algorithm_name":"...","package_name":"..."},"message":"short summary"}',
                "```",
                "Rules:",
                "- ui_add_node supported kinds: container, containerelement, container_group, containergroup, variable, array, stage, interventioner, resnode, function, functiontext, reflector.",
                "- Use ui_update_node to change existing node properties instead of rewriting the whole document.",
                "- Use ui_delete_node to remove an existing node by kind and name.",
                "- Use ui_add_rule only when source and target containers already exist.",
                "- Use functiontext for editable detached solution text that belongs to a fun node.",
                "- document must be the full updated package JSON object, not a patch.",
                "- document root must be a JSON object.",
                "- message should be a short user-facing summary of what changed.",
                "- If changing names or structure affects references, update all affected references consistently.",
                "- If no document change is needed, answer normally without a tool call.",
            ]
        )

    def _build_prompt(
        self,
        project: ProjectState,
        selection: str,
        prompt: str,
        attachments: list[dict[str, str]],
        *,
        document_mode: str = "full",
        extra_sections: list[str] | None = None,
    ) -> str:
        summary = [
            f"selection={selection}",
            f"approval_mode={self.approval_mode}",
            f"containers={len(project.containers)}",
            f"container_groups={len(project.container_groups)}",
            f"rules={len(project.decomposer_rules)}",
            f"reflectors={len(project.reflector_items)}",
            f"res={len(project.res_nodes)}",
            f"functions={len(project.function_frames)}",
            f"stages={len(project.intervention_stages)}",
        ]
        manifest_text = project.current_manifest_text().strip()
        project_snapshot = self._project_snapshot(project).strip()
        prompt_sections = [prompt.strip()]
        if extra_sections:
            prompt_sections.extend(section.strip() for section in extra_sections if str(section).strip())
        if document_mode in {"summary", "full"} and project_snapshot:
            prompt_sections.append(
                "\n".join(
                    [
                        "Compressed project snapshot:",
                        project_snapshot,
                    ]
                )
            )
        if document_mode == "full" and manifest_text:
            prompt_sections.append(
                "\n".join(
                    [
                        "Current document:",
                        manifest_text,
                    ]
                )
            )
        prompt_sections.extend(block for block in self._attachment_prompt_blocks(attachments) if block)
        return (
            "You are the Algorithm Studio agent.\n"
            "Use strict failures. Do not silently ignore invalid state.\n\n"
            + "\n".join(summary)
            + "\n\n"
            + self._workspace_agent_instructions()
            + "\n\n"
            + self._document_tool_instructions()
            + "\n\nPrompt:\n"
            + "\n\n".join(section for section in prompt_sections if section)
        )

    def _build_openai_system_message(self, project: ProjectState, selection: str) -> str:
        return (
            "You are the Algorithm Studio agent.\n"
            "Use strict failures. Do not silently ignore invalid state.\n"
            f"Selection: {selection}\n"
            f"approval_mode={self.approval_mode}\n"
            f"containers={len(project.containers)}, "
            f"container_groups={len(project.container_groups)}, "
            f"rules={len(project.decomposer_rules)}, "
            f"reflectors={len(project.reflector_items)}, "
            f"res={len(project.res_nodes)}, "
            f"functions={len(project.function_frames)}, "
            f"stages={len(project.intervention_stages)}\n\n"
            f"{self._workspace_agent_instructions()}\n\n"
            f"{self._document_tool_instructions()}"
        )

    def _build_openai_prompt_blocks(
        self,
        project: ProjectState,
        prompt: str,
        attachments: list[dict[str, str]],
        *,
        document_mode: str = "full",
        extra_sections: list[str] | None = None,
    ) -> list[str]:
        project_snapshot = self._project_snapshot(project).strip()
        prompt_blocks = [prompt.strip()]
        if extra_sections:
            prompt_blocks.extend(section.strip() for section in extra_sections if str(section).strip())
        if document_mode in {"summary", "full"} and project_snapshot:
            prompt_blocks.append(
                "\n".join(
                    [
                        "Compressed project snapshot:",
                        project_snapshot,
                    ]
                )
            )
        manifest_text = project.current_manifest_text().strip()
        if document_mode == "full" and manifest_text:
            prompt_blocks.append(
                "\n".join(
                    [
                        "Current document:",
                        manifest_text,
                    ]
                )
            )
        prompt_blocks.extend(block for block in self._attachment_prompt_blocks(attachments) if block)
        return [block for block in prompt_blocks if block]

    def _build_openai_user_content(self, prompt: str, attachments: list[dict[str, str]]) -> str | list[dict[str, Any]]:
        if not attachments:
            return prompt.strip()
        content: list[dict[str, Any]] = [{"type": "text", "text": prompt.strip()}]
        for attachment in attachments:
            if attachment["kind"] == "image":
                data = self._read_attachment_bytes(Path(attachment["path"]))
                encoded = base64.b64encode(data).decode("ascii")
                content.append(
                    {
                        "type": "image_url",
                        "image_url": {"url": f"data:{attachment['mime_type']};base64,{encoded}"},
                    }
                )
                continue
            text = self._read_text_attachment(attachment)
            content.append(
                {
                    "type": "text",
                    "text": f"[Attached file] {attachment['name']}\n{text.strip()}",
                }
            )
        return content

    def _build_openai_responses_input(
        self,
        prompt: str,
        attachments: list[dict[str, str]],
    ) -> list[dict[str, Any]]:
        content: list[dict[str, Any]] = [{"type": "input_text", "text": prompt.strip()}]
        for attachment in attachments:
            if attachment["kind"] == "image":
                data = self._read_attachment_bytes(Path(attachment["path"]))
                encoded = base64.b64encode(data).decode("ascii")
                content.append(
                    {
                        "type": "input_image",
                        "image_url": f"data:{attachment['mime_type']};base64,{encoded}",
                        "detail": "auto",
                    }
                )
                continue
            text = self._read_text_attachment(attachment)
            content.append(
                {
                    "type": "input_text",
                    "text": f"[Attached file] {attachment['name']}\n{text.strip()}",
                }
            )
        return [{"role": "user", "content": content}]

    def _build_openai_messages(
        self,
        project: ProjectState,
        selection: str,
        prompt: str,
        attachments: list[dict[str, str]],
        *,
        document_mode: str = "full",
        extra_sections: list[str] | None = None,
    ) -> list[dict[str, Any]]:
        prompt_blocks = self._build_openai_prompt_blocks(
            project,
            prompt,
            attachments,
            document_mode=document_mode,
            extra_sections=extra_sections,
        )
        return [
            {"role": "system", "content": self._build_openai_system_message(project, selection)},
            {"role": "user", "content": self._build_openai_user_content("\n\n".join(block for block in prompt_blocks if block), attachments)},
        ]

    def _post_json(
        self,
        url: str,
        body: dict[str, Any],
        *,
        event_callback: AgentEventCallback | None = None,
        waiting_detail: str | None = None,
    ) -> dict[str, Any]:
        payload = json.dumps(body).encode("utf-8")
        headers = {"Content-Type": "application/json"}
        if self.api_key.strip():
            headers["Authorization"] = f"Bearer {self.api_key.strip()}"
        request = Request(url, data=payload, headers=headers, method="POST")
        if waiting_detail:
            _emit_agent_event(event_callback, "activity.update", detail=waiting_detail, tag="reasoning")
        try:
            if self.timeout_seconds is None:
                response = urlopen(request)
            else:
                response = urlopen(request, timeout=self.timeout_seconds)
            with response:
                response_text = response.read().decode("utf-8", errors="replace")
        except HTTPError as exc:
            error_body = exc.read().decode("utf-8", errors="replace") if exc.fp else ""
            error_text = " ".join(
                part.strip()
                for part in [f"{exc.code} {exc.reason}".strip(), error_body.splitlines()[0].strip() if error_body else ""]
                if part
            )
            raise RuntimeError(f"API request failed: {error_text or exc.reason}") from exc
        except URLError as exc:
            raise RuntimeError(f"API request failed: {exc.reason}") from exc
        return json.loads(response_text)

    def _build_openai_responses_url(self) -> str:
        base_url = self.base_url.strip()
        if base_url.endswith("/responses"):
            return base_url
        if base_url.endswith("/chat/completions"):
            return base_url[: -len("/chat/completions")] + "/responses"
        if base_url.endswith("/v1"):
            return base_url + "/responses"
        return base_url.rstrip("/") + "/v1/responses"

    def _build_chat_completions_url(self) -> str:
        base_url = self.base_url.strip()
        parsed = urlparse(base_url)
        if base_url.endswith("/chat/completions"):
            return base_url
        if base_url.endswith("/v1"):
            return base_url + "/chat/completions"
        if parsed.netloc.endswith("deepseek.com") and parsed.path in {"", "/"}:
            return base_url.rstrip("/") + "/chat/completions"
        return base_url.rstrip("/") + "/v1/chat/completions"

    def _extract_chat_completion_content(self, data: dict[str, Any]) -> str:
        choices = data.get("choices", [])
        if not choices:
            raise RuntimeError("API response did not contain choices.")
        message = choices[0].get("message", {})
        content = str(message.get("content") or "").strip()
        if not content:
            raise RuntimeError("API response did not contain assistant content.")
        return content

    def _extract_openai_responses_content(self, data: dict[str, Any]) -> str:
        output_text = str(data.get("output_text") or "").strip()
        if output_text:
            return output_text
        parts: list[str] = []
        for item in data.get("output", []):
            if str(item.get("type") or "").strip() != "message":
                continue
            for content in item.get("content", []):
                content_type = str(content.get("type") or "").strip()
                if content_type == "output_text":
                    text = str(content.get("text") or "").strip()
                    if text:
                        parts.append(text)
                elif content_type == "refusal":
                    refusal = str(content.get("refusal") or "").strip()
                    if refusal:
                        parts.append(refusal)
        joined = "\n".join(part for part in parts if part).strip()
        if not joined:
            raise RuntimeError("Responses API response did not contain assistant content.")
        return joined

    def compact_history(self, history_text: str) -> str:
        if self.provider.strip().lower() != "api" or self._api_provider_kind() != "deepseek":
            raise RuntimeError("History compaction is only supported for the DeepSeek API path.")
        compact_prompt = "\n".join(
            [
                "Compress the conversation into a stable working summary.",
                "Return plain text only.",
                "Preserve the active user goal, hard constraints, current scene/document state, concrete decisions, unresolved risks, and the immediate next action.",
                "Do not invent details. Do not omit constraints that still matter.",
                "",
                "Conversation to compact:",
                history_text.strip(),
            ]
        ).strip()
        body = {
            "model": self.model.strip() or "gpt-5.4-mini",
            "messages": [
                {
                    "role": "system",
                    "content": (
                        "You maintain conversation state for an Algorithm Studio assistant. "
                        "Use strict failures and preserve only relevant facts."
                    ),
                },
                {"role": "user", "content": compact_prompt},
            ],
        }
        data = self._post_json(self._build_chat_completions_url(), body)
        return self._extract_chat_completion_content(data)

    def _build_plan_prompt(self, prompt: str) -> str:
        return "\n".join(
            [
                "Phase: planning only.",
                "Do not edit the scene yet.",
                "Do not emit any interface4agents block yet.",
                "Draft a short execution plan with 3 to 6 steps.",
                "Favor the smallest valid change set and explicitly avoid unnecessary nodes or scaffolding.",
                "End with one short line named `Execution focus:` that states what should be done first.",
                "",
                "Original request:",
                prompt.strip(),
            ]
        ).strip()

    def _build_execution_prompt(self, prompt: str, plan_text: str) -> str:
        compact_plan = self._compact_text(plan_text, limit=1200)
        return "\n".join(
            [
                "Phase: execution.",
                "Follow the plan below, but only make the smallest coherent batch of edits needed for the request.",
                "Use interface4agents only.",
                "Prefer 1 to 4 precise commands over broad rewrites.",
                "If the prompt includes an operation stack, use it as the latest source of truth for progress.",
                "For step-by-step teaching, emit exactly one highlight before the next instruction when supported.",
                "After any command block, add a short plain-text note about what changed and what remains.",
                "",
                "Approved plan:",
                compact_plan or "(none)",
                "",
                "Original request:",
                prompt.strip(),
            ]
        ).strip()

    def _format_command_preview(self, parts: list[str]) -> str:
        cleaned: list[str] = []
        for index, part in enumerate(parts):
            text = str(part).strip()
            if not text:
                continue
            cleaned.append(Path(text).name if index == 0 else text)
        preview = " ".join(cleaned).strip()
        if len(preview) > 120:
            return preview[:117].rstrip() + "..."
        return preview

    def _extract_command_preview(self, value: Any) -> str | None:
        if isinstance(value, str):
            text = value.strip()
            return text or None
        if isinstance(value, list):
            parts = [str(item).strip() for item in value if str(item).strip()]
            if parts:
                return self._format_command_preview(parts)
            return None
        if isinstance(value, dict):
            for key in ("argv", "command", "cmd", "args", "display", "value"):
                preview = self._extract_command_preview(value.get(key))
                if preview:
                    return preview
        return None

    def _extract_event_text(self, value: Any, *, depth: int = 0) -> str | None:
        if depth > 5:
            return None
        if isinstance(value, str):
            text = " ".join(part.strip() for part in value.splitlines() if part.strip())
            return text or None
        if isinstance(value, list):
            for item in value:
                text = self._extract_event_text(item, depth=depth + 1)
                if text:
                    return text
            return None
        if isinstance(value, dict):
            preferred_keys = (
                "summary_text",
                "summary",
                "text",
                "delta",
                "message",
                "detail",
                "title",
                "content",
                "label",
                "name",
                "reasoning",
                "plan",
            )
            for key in preferred_keys:
                if key in value:
                    text = self._extract_event_text(value.get(key), depth=depth + 1)
                    if text:
                        return text
            for key, nested in value.items():
                if key in {
                    "type",
                    "id",
                    "index",
                    "thread_id",
                    "turn_id",
                    "timestamp",
                    "created_at",
                    "updated_at",
                    "status",
                    "exit_code",
                }:
                    continue
                text = self._extract_event_text(nested, depth=depth + 1)
                if text:
                    return text
        return None

    def _looks_like_command_text(self, text: str) -> bool:
        normalized = " ".join(part.strip() for part in str(text).splitlines() if part.strip())
        if not normalized:
            return False
        lowered = normalized.lower()
        command_markers = (
            "powershell.exe",
            "pwsh.exe",
            "cmd.exe",
            "bash -lc",
            "sh -lc",
            "python -c",
            "python3 -c",
            "get-childitem",
            "select-string",
            "rg -n ",
            "rg --files",
            "git status",
            "git diff",
            " c:\\windows\\system32\\",
            " -command ",
            " /c ",
        )
        if any(marker in lowered for marker in command_markers):
            return True
        if lowered.startswith(("powershell ", "powershell.exe ", "cmd.exe ", "bash ", "sh ", "py ", "python ", "python3 ", "rg ", "git ")):
            return True
        return False

    def _clean_activity_text(self, text: str | None) -> str | None:
        if text is None:
            return None
        compact = " ".join(part.strip() for part in str(text).splitlines() if part.strip())
        if not compact:
            return None
        if self._looks_like_command_text(compact):
            return None
        return compact

    def _extract_codex_session_id(self, event: dict[str, Any]) -> str | None:
        def _walk(value: Any, *, scope: str = "", depth: int = 0) -> str | None:
            if depth > 5:
                return None
            if isinstance(value, dict):
                for key, nested in value.items():
                    normalized_key = str(key).strip().lower()
                    text = str(nested).strip() if not isinstance(nested, (dict, list)) else ""
                    if normalized_key in {"session_id", "sessionid", "thread_id", "threadid", "conversation_id", "conversationid"} and text:
                        return text
                    if normalized_key == "id" and scope in {"session", "thread", "conversation"} and text:
                        return text
                for key, nested in value.items():
                    found = _walk(nested, scope=str(key).strip().lower(), depth=depth + 1)
                    if found:
                        return found
                return None
            if isinstance(value, list):
                for item in value:
                    found = _walk(item, scope=scope, depth=depth + 1)
                    if found:
                        return found
            return None

        return _walk(event)

    def _codex_activity_update(self, event: dict[str, Any]) -> tuple[str | None, str | None]:
        event_type = str(event.get("type") or "").strip().lower()
        if not event_type:
            return None, None
        if event_type in {
            "thread.started",
            "turn.started",
            "turn.completed",
            "thread/started",
            "turn/started",
            "turn/completed",
            "thread/compacted",
            "item/started",
            "item/completed",
            "rawresponseitem/completed",
            "codex/event/task_started",
            "codex/event/task_complete",
            "codex/event/item_started",
            "codex/event/item_completed",
            "codex/event/session_configured",
            "sessionconfigured",
        }:
            return None, None
        if event_type in {"error", "codex/event/error", "codex/event/stream_error"}:
            message = self._clean_activity_text(str(event.get("message") or event.get("detail") or "").strip())
            if not message or message.startswith("Reconnecting..."):
                return None, None
            return "error", message
        if event_type in {
            "item/reasoning/summarytextdelta",
            "item/reasoning/summarypartadded",
            "item/reasoning/textdelta",
            "item/plan/delta",
            "turn/plan/updated",
            "codex/event/agent_reasoning",
            "codex/event/agent_reasoning_delta",
            "codex/event/agent_reasoning_raw_content",
            "codex/event/agent_reasoning_raw_content_delta",
            "codex/event/reasoning_content_delta",
            "codex/event/reasoning_raw_content_delta",
            "codex/event/plan_delta",
            "codex/event/plan_update",
        }:
            text = self._clean_activity_text(self._extract_event_text(event))
            if text:
                return "reasoning", text
            return None, None
        if event_type in {
            "codex/event/exec_approval_request",
            "codex/event/apply_patch_approval_request",
        }:
            return None, None
        if event_type == "codex/event/exec_command_begin":
            return None, None
        if event_type == "codex/event/exec_command_end":
            return None, None
        if event_type in {
            "codex/event/mcp_tool_call_begin",
            "codex/event/mcp_tool_call_end",
            "item/mcptoolcall/progress",
            "serverrequest/resolved",
            "codex/event/web_search_begin",
            "codex/event/web_search_end",
            "item/filechange/patchupdated",
            "codex/event/patch_apply_begin",
            "codex/event/patch_apply_end",
        }:
            return None, None
        if event_type in {
            "codex/event/agent_message",
            "codex/event/agent_message_delta",
            "codex/event/agent_message_content_delta",
            "item/agentmessage/delta",
        }:
            text = self._clean_activity_text(self._extract_event_text(event))
            if text:
                return "reasoning", text
            return None, None
        if event_type in {
            "codex/event/exec_command_output_delta",
            "command/exec/outputdelta",
            "item/commandexecution/outputdelta",
            "item/commandexecution/terminalinteraction",
            "process/outputdelta",
            "process/exited",
            "fs/changed",
        }:
            return None, None
        for key in ("command", "cmd", "argv", "args"):
            preview = self._extract_command_preview(event.get(key))
            if preview:
                return None, None
        text = self._clean_activity_text(self._extract_event_text(event))
        if text and not text.startswith("Reconnecting..."):
            return "reasoning", text
        return None, None

    def _call_api(
        self,
        project: ProjectState,
        selection: str,
        prompt: str,
        attachments: list[dict[str, str]],
        event_callback: AgentEventCallback | None,
        *,
        document_mode: str = "full",
        extra_sections: list[str] | None = None,
        emit_start: bool = True,
    ) -> str:
        base_url = self.base_url.strip()
        if not base_url:
            raise ValueError("API base_url is required when provider is api.")
        if self._api_provider_kind() == "openai":
            self._sync_provider_session_state()
            prompt_blocks = self._build_openai_prompt_blocks(
                project,
                prompt,
                attachments,
                document_mode=document_mode,
                extra_sections=extra_sections,
            )
            if emit_start:
                _emit_agent_event(event_callback, "activity.start", title="chat", summary="Start API", detail="POST /responses")
            body: dict[str, Any] = {
                "model": self.model.strip() or "gpt-5.4-mini",
                "instructions": self._build_openai_system_message(project, selection),
                "input": self._build_openai_responses_input("\n\n".join(prompt_blocks), attachments),
                "context_management": [
                    {
                        "type": "compaction",
                        "compact_threshold": OPENAI_RESPONSES_COMPACT_THRESHOLD,
                    }
                ],
            }
            if self._openai_previous_response_id:
                body["previous_response_id"] = self._openai_previous_response_id
            data = self._post_json(
                self._build_openai_responses_url(),
                body,
                event_callback=event_callback,
                waiting_detail="Waiting for OpenAI Responses API",
            )
            response_id = str(data.get("id") or "").strip()
            if not response_id:
                raise RuntimeError("Responses API response did not contain an id.")
            self._openai_previous_response_id = response_id
            return self._extract_openai_responses_content(data)
        if emit_start:
            _emit_agent_event(event_callback, "activity.start", title="chat", summary="开始请求 API", detail="POST /chat/completions")
        body = {
            "model": self.model.strip() or "gpt-5.4-mini",
            "messages": self._build_openai_messages(
                project,
                selection,
                prompt,
                attachments,
                document_mode=document_mode,
                extra_sections=extra_sections,
            ),
        }
        data = self._post_json(
            self._build_chat_completions_url(),
            body,
            event_callback=event_callback,
            waiting_detail="Waiting for API response",
        )
        return self._extract_chat_completion_content(data)
        _emit_agent_event(event_callback, "activity.update", detail="等待 API 返回结果", tag="reasoning")
        try:
            if self.timeout_seconds is None:
                response = urlopen(request)
            else:
                response = urlopen(request, timeout=self.timeout_seconds)
            with response:
                response_text = response.read().decode("utf-8", errors="replace")
        except HTTPError as exc:
            error_body = exc.read().decode("utf-8", errors="replace") if exc.fp else ""
            error_text = " ".join(
                part.strip()
                for part in [f"{exc.code} {exc.reason}".strip(), error_body.splitlines()[0].strip() if error_body else ""]
                if part
            )
            raise RuntimeError(f"API request failed: {error_text or exc.reason}") from exc
        except URLError as exc:
            raise RuntimeError(f"API request failed: {exc.reason}") from exc
        data = json.loads(response_text)
        choices = data.get("choices", [])
        if not choices:
            raise RuntimeError("API response did not contain choices.")
        message = choices[0].get("message", {})
        content = str(message.get("content") or "").strip()
        if not content:
            raise RuntimeError("API response did not contain assistant content.")
        return content

    def _call_codex(
        self,
        project: ProjectState,
        selection: str,
        prompt: str,
        attachments: list[dict[str, str]],
        event_callback: AgentEventCallback | None,
        *,
        document_mode: str = "full",
        extra_sections: list[str] | None = None,
        emit_start: bool = True,
    ) -> str:
        command = _resolve_codex_command(self.codex_command)
        if not Path(command).exists():
            raise RuntimeError(f"Codex command not found: {command}")
        self._sync_provider_session_state()
        fd, output_path = tempfile.mkstemp(prefix="algorithm_studio_codex_", suffix=".txt")
        os.close(fd)
        Path(output_path).unlink(missing_ok=True)
        resume_session_id = str(self._codex_session_id or "").strip()
        if resume_session_id:
            args = [
                command,
                "exec",
                "resume",
                "--json",
                "--output-last-message",
                output_path,
            ]
        else:
            args = [
                command,
                "exec",
                "--cd",
                str(PROJECT_ROOT),
                "--color",
                "never",
                "--json",
                "--output-last-message",
                output_path,
            ]
        model = self.model.strip()
        if model:
            args.extend(["--model", model])
        for attachment in attachments:
            if attachment["kind"] == "image":
                args.extend(["--image", attachment["path"]])
        if resume_session_id:
            args.append(resume_session_id)
        args.append("-")
        stdin_prompt = self._build_prompt(
            project,
            selection,
            prompt,
            attachments,
            document_mode=document_mode,
            extra_sections=extra_sections,
        )
        if emit_start:
            _emit_agent_event(event_callback, "activity.start", title="chat", summary="开始执行 Codex", detail="正在准备执行环境")
        last_error_message = ""
        return_code = -1
        self._cancel_requested = False
        try:
            process = subprocess.Popen(
                args,
                cwd=str(PROJECT_ROOT),
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="strict",
                bufsize=1,
            )
            self._active_process = process
            if process.stdin is None or process.stdout is None:
                raise AssertionError("Codex process pipes were not initialized.")
            process.stdin.write(stdin_prompt)
            process.stdin.close()
            while True:
                if self._cancel_requested and process.poll() is None:
                    process.terminate()
                line = process.stdout.readline()
                if not line:
                    if process.poll() is not None:
                        break
                    time.sleep(0.05)
                    continue
                line_text = line.strip()
                if not line_text:
                    continue
                try:
                    event = json.loads(line_text)
                except json.JSONDecodeError:
                    continue
                session_id = self._extract_codex_session_id(event)
                if session_id:
                    self._codex_session_id = session_id
                tag, summary = self._codex_activity_update(event)
                if summary and tag:
                    _emit_agent_event(event_callback, "activity.update", detail=summary, tag=tag)
                if str(event.get("type") or "").strip().lower() == "turn.failed":
                    last_error_message = str(event.get("error", {}).get("message") or "").strip()
            return_code = process.wait()
            output = Path(output_path).read_text(encoding="utf-8").strip() if Path(output_path).exists() else ""
        finally:
            self._active_process = None
            Path(output_path).unlink(missing_ok=True)
        if self._cancel_requested:
            raise RuntimeError("Request cancelled.")
        if return_code != 0:
            error_message = last_error_message or f"Codex command failed (exit {return_code})."
            lowered_error = error_message.lower()
            if resume_session_id and "session" in lowered_error and ("not found" in lowered_error or "no such" in lowered_error or "unknown" in lowered_error):
                raise RuntimeError("Saved Codex session is unavailable. Clear chat history and retry.") from None
            raise RuntimeError(error_message) from None
        if output:
            return output
        return "Codex finished without output."

    def _call_provider(
        self,
        project: ProjectState,
        selection: str,
        prompt: str,
        attachments: list[dict[str, str]],
        event_callback: AgentEventCallback | None,
        *,
        document_mode: str = "full",
        extra_sections: list[str] | None = None,
        emit_start: bool = True,
    ) -> str:
        mode = self.provider.strip().lower()
        if mode == "api":
            return self._call_api(
                project,
                selection,
                prompt,
                attachments,
                event_callback,
                document_mode=document_mode,
                extra_sections=extra_sections,
                emit_start=emit_start,
            )
        if mode == "codex":
            return self._call_codex(
                project,
                selection,
                prompt,
                attachments,
                event_callback,
                document_mode=document_mode,
                extra_sections=extra_sections,
                emit_start=emit_start,
            )
        summary = [
            f"provider={self.provider}",
            f"model={self.model}",
            f"approval_mode={self.approval_mode}",
            f"selection={selection}",
            f"containers={len(project.containers)}",
            f"container_groups={len(project.container_groups)}",
            f"rules={len(project.decomposer_rules)}",
            f"reflectors={len(project.reflector_items)}",
            f"res={len(project.res_nodes)}",
            f"functions={len(project.function_frames)}",
            f"stages={len(project.intervention_stages)}",
        ]
        return (
            "Mock agent response\n"
            + "\n".join(summary)
            + "\n\nPrompt:\n"
            + prompt.strip()
            + "\n\nNext action:\n"
            + "Generate package JSON patch, C++ skeleton, or shader binding update."
        )

    def generate(
        self,
        project: ProjectState,
        selection: str,
        prompt: str,
        *,
        attachments: list[dict[str, str]] | None = None,
        event_callback: AgentEventCallback | None = None,
    ) -> str:
        normalized_attachments = self._normalize_attachments(attachments)
        self._cancel_requested = False
        if False and self._should_stage_request(selection, prompt):
            compress_context = self._should_compress_planning_context(project, prompt, normalized_attachments)
            detail = "上下文较长，先压缩关键信息再整理方案。" if compress_context else "保留当前上下文，先整理执行方案。"
            _emit_agent_event(event_callback, "activity.start", title="chat", summary="开始整理方案", detail=detail)
            _emit_agent_event(
                event_callback,
                "activity.update",
                detail="先提炼任务边界并整理最小执行步骤。",
                tag="reasoning",
            )
            plan_response = self._call_provider(
                project,
                selection,
                self._build_plan_prompt(prompt),
                normalized_attachments,
                event_callback,
                document_mode="summary" if compress_context else "full",
                emit_start=False,
            )
            plan_text = self._extract_visible_text(plan_response).strip()
            if not plan_text:
                raise RuntimeError("Planning phase returned no visible plan.")
            _emit_agent_event(
                event_callback,
                "activity.update",
                detail="方案: " + self._compact_text(plan_text, limit=220),
                tag="reasoning",
            )
            _emit_agent_event(event_callback, "activity.update", detail="根据方案拆成小步，开始执行当前批次。", tag="reasoning")
            return self._call_provider(
                project,
                selection,
                self._build_execution_prompt(prompt, plan_text),
                normalized_attachments,
                event_callback,
                document_mode="full",
                extra_sections=["Planning summary:\n" + self._compact_text(plan_text, limit=1200)],
                emit_start=False,
            )
        return self._call_provider(
            project,
            selection,
            prompt,
            normalized_attachments,
            event_callback,
            document_mode="full",
            emit_start=True,
        )


def generate_cpp_skeleton(project_name: str) -> str:
    return "\n".join(
        [
            "#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1",
            "",
            "#include \"../algorithm_plugin_api.h\"",
            "",
            "#include <memory>",
            "#include <string>",
            "#include <utility>",
            "",
            "namespace {",
            "",
            f"constexpr const char* kAlgorithmName = \"{project_name}\";",
            "",
            "}  // namespace",
            "",
            "extern \"C\" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(",
            "  const algorithm_support::AlgorithmPluginRequest* request,",
            "  algorithm_support::AlgorithmPluginBundle* out_bundle) {",
            "  if (!request || !out_bundle) {",
            "    return false;",
            "  }",
            "  out_bundle->Clear();",
            "  (void)kAlgorithmName;",
            "  return true;",
            "}",
            "",
            "extern \"C\" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(",
            "  const algorithm_support::AlgorithmPluginRequest* request,",
            "  algorithm::AlgorithmReflector* out_reflector) {",
            "  if (!request || !out_reflector) {",
            "    return false;",
            "  }",
            "  (void)kAlgorithmName;",
            "  return true;",
            "}",
            "",
        ]
    )
