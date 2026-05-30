#pragma once

#include "data_protocol/interaction/interaction_state.h"
#include "data_protocol/mesh.h"
#include "decomposition/decomposition_manager.h"

namespace app_orchestration {

decomposition::GuideUiPhysDescriptor BuildGuideUiFrameRelayOnPhysAgentDescriptor(
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
