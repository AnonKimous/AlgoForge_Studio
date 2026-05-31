#include "app_frame_sync_glue.h"

namespace app_orchestration {

RenderUiState AppFrameSyncGlue::BuildRenderUiStateFromRenderAgentAndPhysAgent(
  const RenderAgent& render_agent,
  const PhysAgent& phys_agent,
  float animation_time) {
  RenderUiState ui{};
  ui.mode = render_agent.mode();
  ui.phys_run_state = render_agent.phys_run_state();
  ui.phys_solver_kind = phys_agent.solver_kind();
  ui.phys_algorithm_name = phys_agent.algorithm_name();
  ui.phys_current_frame_index = phys_agent.current_frame_index();
  ui.total_velocities = phys_agent.total_velocities();
  ui.linear_velocities = phys_agent.linear_velocities();
  ui.angular_velocities = phys_agent.angular_velocities();
  ui.gpu_dispatch_debug = phys_agent.gpu_dispatch_debug_info();
  ui.animation_time = animation_time;

  return ui;
}

void AppFrameSyncGlue::ApplyRenderFrameResultOnPhysAgentAndMesh(
  const RenderFrameResult& frame,
  PhysAgent* phys_agent,
  Mesh* mesh) {
  if (!phys_agent || !mesh) {
    return;
  }

  if (frame.phys_reset_requested) {
    phys_agent->Reset(*mesh);
  }
  if (frame.phys_step_requested) {
    phys_agent->StepOnce(*mesh);
  }
}

}  // namespace app_orchestration
