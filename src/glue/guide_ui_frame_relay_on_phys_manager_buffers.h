#pragma once

#include "../interaction/interaction_state.h"
#include "../mesh.h"
#include "../physics/phys_manager.h"

namespace app_orchestration {

struct GuideUiPhysBufferCommit {
  std::vector<int> selected_vertices;
  IoBufferPacket packet{};
};

GuideUiPhysBufferCommit BuildGuideUiFrameRelayOnPhysManagerBuffers(
  const Mesh& mesh,
  const GuideUiFrame& guide_ui_frame,
  GuideEditMode guide_edit_mode,
  float guide_velocity_magnitude,
  uint32_t guide_velocity_delay_frames,
  uint32_t guide_velocity_duration_frames,
  float guide_force_magnitude,
  uint32_t guide_force_delay_frames,
  uint32_t guide_force_duration_frames);
}  // namespace app_orchestration
