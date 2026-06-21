from __future__ import annotations

try:
    from .agent_client import MockAgentClient as _LegacyMockAgentClient
    from .agent_client import generate_cpp_skeleton
    from .interface4agents import build_interface4agents_prompt, execute_interface4agents_command, extract_interface4agents_script
except ImportError:
    from agent_client import MockAgentClient as _LegacyMockAgentClient
    from agent_client import generate_cpp_skeleton
    from interface4agents import build_interface4agents_prompt, execute_interface4agents_command, extract_interface4agents_script


class MockAgentClient(_LegacyMockAgentClient):
    def _document_tool_instructions(self) -> str:
        return build_interface4agents_prompt()


__all__ = [
    "MockAgentClient",
    "build_interface4agents_prompt",
    "execute_interface4agents_command",
    "extract_interface4agents_script",
    "generate_cpp_skeleton",
]

