#pragma once

#include "validation_actions.h"

#include <string>
#include <string_view>
#include <vector>

struct ValidationScriptParseResult {
  bool ok{false};
  std::string error;
  std::vector<ValidationAction> actions;
};

ValidationScriptParseResult ParseValidationScript(std::string_view script);
std::string DescribeValidationAction(const ValidationAction& action);
