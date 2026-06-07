#pragma once

#include "agent_management/agent_manager.h"
#include "common_data/common_data.h"

#include <string>

namespace interact_ui {

class IInteractUiHost {
 public:
  virtual ~IInteractUiHost() = default;

  virtual AgentManager& agent_manager() = 0;
  virtual const AgentManager& agent_manager() const = 0;

  virtual const InputState& input() const = 0;
  virtual Vec2 mouse_position() const = 0;
  virtual float frame_dt_seconds() const = 0;

  virtual std::string& ui_status_message() = 0;
  virtual const std::string& ui_status_message() const = 0;
};

}  // namespace interact_ui

