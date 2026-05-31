#pragma once

#include "algorithm/algorithm_types.h"
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

struct RenderUiState {
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Pause};
  PhysSolverKind phys_solver_kind{PhysSolverKind::Cpu};
  std::string phys_algorithm_name;
  int phys_current_frame_index{0};
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  GpuPhysicsDispatchDebugInfo gpu_dispatch_debug;
  float animation_time{};
};

struct RenderFrameResult {
  uint32_t draw_calls{};
  InteractionMode mode{InteractionMode::Edit};
  PhysRunState phys_run_state{PhysRunState::Pause};
  bool phys_reset_requested{false};
  bool phys_step_requested{false};
};

}  // namespace common_data


using common_data::InteractionMode;
using common_data::RenderFrameResult;
using common_data::RenderUiState;
