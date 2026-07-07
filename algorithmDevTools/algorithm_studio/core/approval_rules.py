from __future__ import annotations

from dataclasses import dataclass, field
import re
from pathlib import Path


ACCESS_RULES_FILENAME = "accessRules.md"


@dataclass(frozen=True)
class ApprovalRuleSet:
    default_mode: str = "manual"
    allow_rules: list[str] = field(default_factory=list)
    deny_rules: list[str] = field(default_factory=list)
    manual_rules: list[str] = field(default_factory=list)
    source_path: Path | None = None


@dataclass(frozen=True)
class ApprovalDecision:
    approved: bool
    outcome: str
    reason: str
    matched_rule: str = ""
    source_path: Path | None = None

    @property
    def requires_manual(self) -> bool:
        return self.outcome == "manual"


def resolve_access_rules_path() -> Path:
    module_root = Path(__file__).resolve().parents[1]
    candidates = [
        module_root / ACCESS_RULES_FILENAME,
        module_root.parent / ACCESS_RULES_FILENAME,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def load_access_rules(path: Path | None = None) -> ApprovalRuleSet:
    actual_path = path or resolve_access_rules_path()
    if not actual_path.exists():
        raise FileNotFoundError(f"access rules file not found: {actual_path}")
    text = actual_path.read_text(encoding="utf-8")
    return parse_access_rules(text, actual_path)


def parse_access_rules(text: str, source_path: Path | None = None) -> ApprovalRuleSet:
    default_mode = "manual"
    sections: dict[str, list[str]] = {
        "allow": [],
        "deny": [],
        "manual": [],
    }
    current_section: str | None = None

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        stripped = raw_line.strip()
        if not stripped:
            continue
        lower = stripped.lower()
        if lower.startswith("default:"):
            default_mode = _normalize_mode(stripped.split(":", 1)[1], line_number)
            continue
        if stripped.startswith("#"):
            heading = stripped.lstrip("#").strip().lower()
            if heading in sections:
                current_section = heading
            continue
        if lower in sections:
            current_section = lower
            continue
        if current_section is None:
            continue
        entry = stripped
        if entry[:1] in {"-", "*"}:
            entry = entry[1:].strip()
        if entry:
            sections[current_section].append(entry)

    return ApprovalRuleSet(
        default_mode=default_mode,
        allow_rules=sections["allow"],
        deny_rules=sections["deny"],
        manual_rules=sections["manual"],
        source_path=source_path,
    )


def evaluate_access_rules(rule_set: ApprovalRuleSet, text: str) -> ApprovalDecision:
    candidate = text.strip()
    if not candidate:
        raise ValueError("Approval text is required.")

    deny_match = _first_matching_rule(rule_set.deny_rules, candidate)
    if deny_match:
        return ApprovalDecision(
            approved=False,
            outcome="deny",
            reason=f"Matched deny rule: {deny_match}",
            matched_rule=deny_match,
            source_path=rule_set.source_path,
        )

    allow_match = _first_matching_rule(rule_set.allow_rules, candidate)
    if allow_match:
        return ApprovalDecision(
            approved=True,
            outcome="allow",
            reason=f"Matched allow rule: {allow_match}",
            matched_rule=allow_match,
            source_path=rule_set.source_path,
        )

    manual_match = _first_matching_rule(rule_set.manual_rules, candidate)
    if manual_match:
        return ApprovalDecision(
            approved=False,
            outcome="manual",
            reason=f"Matched manual rule: {manual_match}",
            matched_rule=manual_match,
            source_path=rule_set.source_path,
        )

    default_mode = _normalize_mode(rule_set.default_mode, None)
    if default_mode == "allow":
        return ApprovalDecision(
            approved=True,
            outcome="allow",
            reason="No specific rule matched; default mode is allow.",
            source_path=rule_set.source_path,
        )
    if default_mode == "deny":
        return ApprovalDecision(
            approved=False,
            outcome="deny",
            reason="No specific rule matched; default mode is deny.",
            source_path=rule_set.source_path,
        )
    return ApprovalDecision(
        approved=False,
        outcome="manual",
        reason="No specific rule matched; default mode is manual.",
        source_path=rule_set.source_path,
    )


def _normalize_mode(value: str, line_number: int | None) -> str:
    mode = value.strip().lower()
    if mode not in {"allow", "deny", "manual"}:
        location = f" on line {line_number}" if line_number is not None else ""
        raise ValueError(f"Unsupported approval mode{location}: {value.strip()}")
    return mode


def _first_matching_rule(rules: list[str], candidate: str) -> str:
    for rule in rules:
        if _matches_rule(rule, candidate):
            return rule
    return ""


def _matches_rule(rule: str, candidate: str) -> bool:
    normalized_rule = rule.strip()
    if not normalized_rule:
        return False

    lower_rule = normalized_rule.lower()
    if lower_rule.startswith("contains:") or lower_rule.startswith("contains="):
        keyword = normalized_rule.split(":", 1)[1] if ":" in normalized_rule else normalized_rule.split("=", 1)[1]
        return keyword.strip().lower() in candidate.lower()
    if lower_rule.startswith("regex:") or lower_rule.startswith("regex="):
        pattern = normalized_rule.split(":", 1)[1] if ":" in normalized_rule else normalized_rule.split("=", 1)[1]
        if not pattern.strip():
            raise ValueError("Regex rule cannot be empty.")
        return re.search(pattern.strip(), candidate, flags=re.IGNORECASE) is not None
    return normalized_rule.lower() in candidate.lower()
