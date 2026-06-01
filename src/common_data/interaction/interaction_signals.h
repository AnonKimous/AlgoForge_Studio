#pragma once

#include <cstdint>
#include <string>

namespace common_data {

enum class InteractionInterventionMode {
  Displacement = 0,
  Velocity = 1,
  Force = 2,
};

struct InteractionInterventionRequest {
  bool enabled{false};
  InteractionInterventionMode mode{InteractionInterventionMode::Displacement};
  float velocity_magnitude{1.0f};
  uint32_t velocity_delay_frames{0};
  uint32_t velocity_duration_frames{1};
  float force_magnitude{1.0f};
  uint32_t force_delay_frames{0};
  uint32_t force_duration_frames{1};
  std::string source_module_name{"agent"};
  uint32_t source_buffer_id{0u};
  std::string target_module_name{"physics_agent"};
  uint32_t target_buffer_id{0u};
  bool lock_required{false};
};

struct AgentToAlgorithmSignal {
  bool needs_intervention{false};
  bool pause_requested{false};
  bool stop_requested{false};
};

struct AlgorithmToAgentSignal {
  bool intervention_applied{false};
  bool pause_requested{false};
  bool stop_requested{false};
  bool intervention_needed{false};
};

}  // namespace common_data

using common_data::AgentToAlgorithmSignal;
using common_data::AlgorithmToAgentSignal;
using common_data::InteractionInterventionMode;
using common_data::InteractionInterventionRequest;
