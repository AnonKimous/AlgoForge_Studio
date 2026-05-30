#include "guide_ui_frame_relay_on_phys_agent_buffers.h"

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
  uint32_t guide_force_duration_frames) {
  decomposition::GuideUiPhysDescriptor descriptor{};
  descriptor.mesh = &mesh;
  descriptor.guide_ui_frame = guide_ui_frame;
  descriptor.guide_edit_mode = guide_edit_mode;
  descriptor.guide_velocity_magnitude = guide_velocity_magnitude;
  descriptor.guide_velocity_delay_frames = guide_velocity_delay_frames;
  descriptor.guide_velocity_duration_frames = guide_velocity_duration_frames;
  descriptor.guide_force_magnitude = guide_force_magnitude;
  descriptor.guide_force_delay_frames = guide_force_delay_frames;
  descriptor.guide_force_duration_frames = guide_force_duration_frames;
  descriptor.source_module_name = "guide_ui_agent";
  descriptor.source_buffer_id = 0u;
  descriptor.target_module_name = "phys_agent";
  descriptor.target_buffer_id = 0u;
  descriptor.lock_required = false;
  return descriptor;
}

}  // namespace app_orchestration
