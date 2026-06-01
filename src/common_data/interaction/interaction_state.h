#pragma once

#include "algorithm_library/algorithm_types.h"
#include "common_data/interaction/interaction_signals.h"
#include "common_data/vector_types.h"
#include "common_data/physics/physics_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace common_data {

enum class InteractionMode {
  Edit,
  Phys,
};

struct InteractionUiState {
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Pause};
  PhysSolverKind phys_solver_kind{PhysSolverKind::Cpu};
  std::string phys_algorithm_name;
  int phys_current_frame_index{0};
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  GpuPhysicsDispatchDebugInfo gpu_dispatch_debug;
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
  InteractionInterventionRequest intervention_request{};
  float animation_time{};
};

struct InteractionUiAction {
  uint32_t draw_calls{};
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Pause};
  bool phys_reset_requested{false};
  bool phys_step_requested{false};
  AgentToAlgorithmSignal agent_to_algorithm_signal{};
  InteractionInterventionRequest intervention_request{};
};

}  // namespace common_data


using common_data::InteractionMode;
using common_data::InteractionUiAction;
using common_data::InteractionUiState;
