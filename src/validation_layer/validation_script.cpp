#include "validation_script.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {

std::string Trim(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return std::string(text.substr(begin, end - begin));
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

std::vector<std::string> SplitCommands(std::string_view script) {
  std::vector<std::string> commands;
  std::string current;
  for (char ch : script) {
    if (ch == ';' || ch == '\n' || ch == '\r') {
      std::string trimmed = Trim(current);
      if (!trimmed.empty()) {
        commands.push_back(std::move(trimmed));
      }
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  std::string trimmed = Trim(current);
  if (!trimmed.empty()) {
    commands.push_back(std::move(trimmed));
  }
  return commands;
}

bool ParseGuideEnabled(std::istringstream& stream, bool& enabled) {
  std::string token;
  if (!(stream >> token)) return false;
  token = ToLower(token);
  if (token == "on" || token == "enable" || token == "enabled" || token == "true" || token == "1") {
    enabled = true;
    return true;
  }
  if (token == "off" || token == "disable" || token == "disabled" || token == "false" || token == "0") {
    enabled = false;
    return true;
  }
  return false;
}

}  // namespace

ValidationScriptParseResult ParseValidationScript(std::string_view script) {
  ValidationScriptParseResult result{};
  for (const std::string& command_text : SplitCommands(script)) {
    std::istringstream stream(command_text);
    std::string head;
    if (!(stream >> head)) {
      continue;
    }
    head = ToLower(head);

    ValidationAction action{};
    if (head == "physstep" || head == "step") {
      uint32_t count = 1;
      if (stream >> count) {
        if (count == 0) {
          result.error = "physstep count must be greater than zero";
          return result;
        }
      } else {
        stream.clear();
        count = 1;
      }
      action.kind = ValidationActionKind::PhysStep;
      action.step_count = count;
    } else if (head == "reset") {
      action.kind = ValidationActionKind::Reset;
    } else if (head == "run") {
      action.kind = ValidationActionKind::SetRunState;
      action.run_state = ValidationPhysRunState::Run;
    } else if (head == "pause") {
      action.kind = ValidationActionKind::SetRunState;
      action.run_state = ValidationPhysRunState::Pause;
    } else if (head == "guide" || head == "guides") {
      bool enabled = true;
      if (!ParseGuideEnabled(stream, enabled)) {
        result.error = "guide command expects on/off, enable/disable, true/false, or 1/0";
        return result;
      }
      action.kind = ValidationActionKind::SetGuideEnabled;
      action.enabled = enabled;
    } else {
      result.error = "unknown validation script command: " + command_text;
      return result;
    }

    result.actions.push_back(action);
  }

  result.ok = true;
  return result;
}

std::string DescribeValidationAction(const ValidationAction& action) {
  switch (action.kind) {
    case ValidationActionKind::PhysStep:
      return "physstep " + std::to_string(action.step_count);
    case ValidationActionKind::Reset:
      return "reset";
    case ValidationActionKind::SetRunState:
      return action.run_state == ValidationPhysRunState::Run ? "run" : "pause";
    case ValidationActionKind::SetGuideEnabled:
      return action.enabled ? "guide on" : "guide off";
  }
  return "unknown";
}
