from __future__ import annotations

import re
import shlex
from dataclasses import dataclass
from typing import Any


COMMAND_BLOCK_RE = re.compile(r"```interface4agents\s*(.*?)\s*```", re.IGNORECASE | re.DOTALL)


@dataclass(frozen=True)
class AgentCommandSpec:
    name: str
    usage: str
    location: str
    summary: str
    highlight_target: str | None = None
    aliases: tuple[str, ...] = ()


COMMAND_SPECS: tuple[AgentCommandSpec, ...] = (
    AgentCommandSpec(
        name="v",
        usage="v add [name] [count] [stride] | v update <name> [count] [stride] | v select <name> | v delete <name>",
        location="Left palette > Container > Variable",
        summary="Variable node commands.",
        highlight_target="variable",
    ),
    AgentCommandSpec(
        name="a",
        usage="a add [name] [count] [stride] | a update <name> [count] [stride] | a select <name> | a delete <name>",
        location="Left palette > Container > Array",
        summary="Array node commands.",
        highlight_target="array",
    ),
    AgentCommandSpec(
        name="createCosNode",
        usage="createCosNode <name>",
        location="Selection panel > Merge",
        summary="Create a nested containerElement node.",
        highlight_target="createcosnode",
    ),
    AgentCommandSpec(
        name="hang",
        usage="hang <child> <parent>",
        location="Scene canvas",
        summary="Attach a child node into a containerElement.",
        highlight_target="canvas",
    ),
    AgentCommandSpec(
        name="integrateChild",
        usage="integrateChild <name> | intergrateChild <name>",
        location="Selection panel > Arrange",
        summary="Repack a containerElement tree after attaching children.",
        highlight_target="integratechild",
        aliases=("intergratechild",),
    ),
    AgentCommandSpec(
        name="hotReview",
        usage="hotReview",
        location="Canvas viewport",
        summary="Jump the canvas to the area that contains nodes.",
        highlight_target="canvas",
    ),
    AgentCommandSpec(
        name="scene",
        usage="scene main | scene container | scene decomposer | scene reflector | scene interventioner | scene d2c | scene all",
        location="Canvas header > scene tabs",
        summary="Switch the current scene tab.",
    ),
    AgentCommandSpec(
        name="highlight",
        usage="highlight <command...>",
        location="Use this to point the user at a command's location in the UI.",
        summary="Explain where a command lives.",
    ),
)


SCENE_TARGETS: dict[str, tuple[str, str, str]] = {
    "main": ("graph", "main", "algorithmScene"),
    "graph": ("graph", "main", "algorithmScene"),
    "algorithmscene": ("graph", "main", "algorithmScene"),
    "container": ("container_overview", "container", "containerScene"),
    "container_overview": ("container_overview", "container", "containerScene"),
    "containeroverview": ("container_overview", "container", "containerScene"),
    "containerscene": ("container_overview", "container", "containerScene"),
    "decomposer": ("decomposer_overview", "decomposer", "decomposerScene"),
    "decomposer_overview": ("decomposer_overview", "decomposer", "decomposerScene"),
    "decomposeroverview": ("decomposer_overview", "decomposer", "decomposerScene"),
    "decomposerscene": ("decomposer_overview", "decomposer", "decomposerScene"),
    "reflector": ("reflector_overview", "reflector", "reflectorScene"),
    "reflector_overview": ("reflector_overview", "reflector", "reflectorScene"),
    "reflectoroverview": ("reflector_overview", "reflector", "reflectorScene"),
    "reflectorscene": ("reflector_overview", "reflector", "reflectorScene"),
    "interventioner": ("interventioner_overview", "interventioner", "interventionerScene"),
    "interventioner_overview": ("interventioner_overview", "interventioner", "interventionerScene"),
    "interventioneroverview": ("interventioner_overview", "interventioner", "interventionerScene"),
    "interventionerscene": ("interventioner_overview", "interventioner", "interventionerScene"),
    "d2c": ("decomposer2container_overview", "decomposer2container", "d2cScene"),
    "decomposer2container": ("decomposer2container_overview", "decomposer2container", "d2cScene"),
    "decomposer2container_overview": ("decomposer2container_overview", "decomposer2container", "d2cScene"),
    "decomposer2containeroverview": ("decomposer2container_overview", "decomposer2container", "d2cScene"),
    "d2cscene": ("decomposer2container_overview", "decomposer2container", "d2cScene"),
    "all": ("all_in_one", "all_in_one", "allInOne"),
    "all_in_one": ("all_in_one", "all_in_one", "allInOne"),
    "allinone": ("all_in_one", "all_in_one", "allInOne"),
}


def _command_spec_by_name(name: str) -> AgentCommandSpec:
    normalized = str(name).strip().lower().removesuffix("()")
    for spec in COMMAND_SPECS:
        if spec.name.lower() == normalized or normalized in spec.aliases:
            return spec
    raise RuntimeError(f"Unsupported interface4agents command: {name or '(empty)'}")


def _normalize_highlight_token(token: str) -> str:
    return str(token).strip().lower().removesuffix("()")


def _normalize_highlight_name(token: str) -> str:
    return str(token).strip().removesuffix("()")


def build_interface4agents_prompt() -> str:
    lines = [
        "Tools available through interface4agents:",
        "- Commands are written as command scripts, one command per line.",
        "- Use a fenced block labeled interface4agents when you want the UI to execute commands.",
        "- Syntax: cmd arg arg arg",
        "- Do not emit algorithm-studio-tool blocks.",
        "- Do not edit scripts directly when a command exists.",
        "",
    ]
    for spec in COMMAND_SPECS:
        lines.append(f"- {spec.usage}")
        lines.append(f"  - location: {spec.location}")
    lines.extend(
        [
            "",
            "highlight accepts a command name, a node kind plus node name, or a raw node name, flashes the target in the UI, and returns where that target lives.",
            "Use highlight before explaining a command to the user.",
            "",
            "Example:",
            "```interface4agents",
            "highlight scene container",
            "highlight createCosNode",
            "highlight hang",
            "highlight integrateChild",
            "highlight hotReview",
            "highlight node v1",
            "highlight containerelement cosNode",
            "scene container",
            "createCosNode cosNode",
            "v add v1 1 4",
            "hang v1 cosNode",
            "integrateChild cosNode",
            "a add a1 1 12",
            "```",
        ]
    )
    return "\n".join(lines)


def extract_interface4agents_script(response: str) -> tuple[list[list[str]], str]:
    text = str(response or "")
    commands: list[list[str]] = []

    def _collect(match: re.Match[str]) -> str:
        block = match.group(1)
        for raw_line in block.splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            commands.append(shlex.split(line))
        return ""

    visible_text = COMMAND_BLOCK_RE.sub(_collect, text).strip()
    return commands, visible_text


def _call(app: Any, method_name: str, *args: Any) -> Any:
    method = getattr(app, method_name)
    return method(*args)


def _execute_container_command(app: Any, kind: str, args: list[str]) -> str:
    if not args:
        raise RuntimeError(f"{kind[0]} requires a subcommand.")
    subcommand = args[0].strip().lower()
    tail = args[1:]
    if subcommand == "add":
        payload: dict[str, Any] = {
            "tool": "ui_add_node",
            "kind": kind,
        }
        if tail:
            payload["name"] = tail[0]
        if len(tail) > 1:
            payload["count"] = tail[1]
        if len(tail) > 2:
            payload["stride"] = tail[2]
        return _call(app, "_apply_agent_ui_add_node", payload)
    if subcommand == "update":
        if not tail:
            raise RuntimeError(f"{kind[0]} update requires a name.")
        payload = {
            "tool": "ui_update_node",
            "kind": kind,
            "name": tail[0],
        }
        if len(tail) > 1:
            payload["count"] = tail[1]
        if len(tail) > 2:
            payload["stride"] = tail[2]
        return _call(app, "_apply_agent_ui_update_node", payload)
    if subcommand == "select":
        if not tail:
            raise RuntimeError(f"{kind[0]} select requires a name.")
        _call(app, "_select_item_on_canvas", "container", tail[0])
        return f"Selected {kind} {tail[0]}."
    if subcommand == "delete":
        if not tail:
            raise RuntimeError(f"{kind[0]} delete requires a name.")
        payload = {
            "tool": "ui_delete_node",
            "kind": kind,
            "name": tail[0],
        }
        return _call(app, "_apply_agent_ui_delete_node", payload)
    raise RuntimeError(f"Unsupported {kind[0]} command: {subcommand}")


def _execute_create_cos_node_command(app: Any, args: list[str]) -> str:
    if not args:
        raise RuntimeError("createCosNode requires a name.")
    payload = {
        "tool": "ui_add_node",
        "kind": "containerelement",
        "name": args[0],
    }
    return _call(app, "_apply_agent_ui_add_node", payload)


def _execute_hang_command(app: Any, args: list[str]) -> str:
    if len(args) < 2:
        raise RuntimeError("hang requires child and parent names.")
    child_name = args[0].strip()
    parent_name = args[1].strip()
    if _call(app, "_find_container", child_name) is not None:
        _call(app, "_add_container_to_group", child_name, parent_name)
    elif _call(app, "_find_container_group", child_name) is not None:
        _call(app, "_add_group_to_parent_group", child_name, parent_name)
    else:
        raise RuntimeError(f"hang child not found: {child_name}")
    _call(app, "_interface4agents_integrate_child", parent_name)
    return f"Attached {child_name} into {parent_name}."


def _execute_integrate_child_command(app: Any, args: list[str]) -> str:
    if not args:
        raise RuntimeError("integrateChild requires a containerElement name.")
    return _call(app, "_interface4agents_integrate_child", args[0].strip())


def _execute_scene_command(app: Any, args: list[str]) -> str:
    if not args:
        raise RuntimeError("scene requires a target.")
    normalized = args[0].strip().lower().replace(" ", "_").replace("-", "_")
    if normalized not in SCENE_TARGETS:
        raise RuntimeError(f"Unsupported scene target: {args[0]}")
    view_mode, _, scene_label = SCENE_TARGETS[normalized]
    _call(app, "_set_canvas_view_mode", view_mode, log_message=f"Switched to {scene_label}.")
    return f"Switched to {scene_label}."


def _execute_hot_review_command(app: Any, _args: list[str]) -> str:
    return _call(app, "_interface4agents_hot_review")


def _execute_highlight_named_node_command(app: Any, args: list[str]) -> str:
    if not args:
        raise RuntimeError("highlight requires a node name.")
    if len(args) == 1:
        return _call(app, "_interface4agents_highlight_named_node", _normalize_highlight_name(args[0]))
    kind = _normalize_highlight_token(args[0])
    name = _normalize_highlight_name(args[1])
    return _call(app, "_interface4agents_highlight_node", kind, name)


def _execute_highlight_command(app: Any, args: list[str]) -> str:
    if not args:
        raise RuntimeError("highlight requires a command to point at.")
    token = _normalize_highlight_token(args[0])
    try:
        spec = _command_spec_by_name(token)
    except RuntimeError:
        spec = None
    if spec is None:
        if token in {"node", "name"}:
            return _execute_highlight_named_node_command(app, args[1:])
        if token in {"container", "containerelement", "decomposer", "reflector", "resnode", "function", "functiontext", "interventioner", "stage"}:
            return _execute_highlight_named_node_command(app, args)
        return _execute_highlight_named_node_command(app, args)
    if spec.name in {"v", "a"}:
        target_key = "variable" if spec.name == "v" else "array"
        _call(app, "_interface4agents_highlight_target", target_key)
        return f"Highlight: {spec.usage} | location: {spec.location}"
    if spec.name == "scene":
        if len(args) < 2:
            raise RuntimeError("highlight scene requires a target.")
        normalized = args[1].strip().lower().replace(" ", "_").replace("-", "_")
        if normalized not in SCENE_TARGETS:
            raise RuntimeError(f"Unsupported scene target: {args[1]}")
        _, tab_key, scene_label = SCENE_TARGETS[normalized]
        _call(app, "_interface4agents_highlight_target", f"scene:{tab_key}")
        return f"Highlight: {spec.usage} | location: {spec.location} | target: {scene_label}"
    if spec.highlight_target:
        _call(app, "_interface4agents_highlight_target", spec.highlight_target)
        return f"Highlight: {spec.usage} | location: {spec.location}"
    raise RuntimeError(f"Unsupported highlight command: {spec.name}")


def execute_interface4agents_command(app: Any, tokens: list[str]) -> str:
    if not tokens:
        raise RuntimeError("Empty interface4agents command.")
    command = tokens[0].strip().lower()
    args = tokens[1:]
    if command == "v":
        return _execute_container_command(app, "variable", args)
    if command == "a":
        return _execute_container_command(app, "array", args)
    if command == "createcosnode":
        return _execute_create_cos_node_command(app, args)
    if command == "hang":
        return _execute_hang_command(app, args)
    if command in {"integratechild", "intergratechild"}:
        return _execute_integrate_child_command(app, args)
    if command == "scene":
        return _execute_scene_command(app, args)
    if command == "hotreview":
        return _execute_hot_review_command(app, args)
    if command == "highlight":
        return _execute_highlight_command(app, args)
    raise RuntimeError(f"Unsupported interface4agents command: {command}")
