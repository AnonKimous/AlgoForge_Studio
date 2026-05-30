#include "app_frame_sync_glue.h"

namespace app_orchestration {

void AppFrameSyncGlue::SyncPhysModeLifecycleFromRenderAgent(
  const RenderAgent& render_agent,
  PhysAgent* phys_agent,
  Mesh* mesh,
  const std::filesystem::path& snapshot_path) {
  if (!phys_agent || !mesh) {
    return;
  }

  phys_agent->TickModeTransition(render_agent.mode(), *mesh, snapshot_path);
}

RenderUiState AppFrameSyncGlue::BuildRenderUiStateFromRenderAgentAndPhysAgentAndInteraction(
  const RenderAgent& render_agent,
  const PhysAgent& phys_agent,
  const Mesh& mesh,
  const std::filesystem::path& mesh_path,
  const InteractionFrame& interaction,
  float animation_time) {
  RenderUiState ui{};
  ui.mode = render_agent.mode();
  ui.phys_run_state = render_agent.phys_run_state();
  ui.phys_guide_enabled = render_agent.phys_guide_enabled();
  ui.phys_solver_kind = phys_agent.solver_kind();
  ui.phys_algorithm_name = phys_agent.algorithm_name();
  ui.guide_edit_mode = phys_agent.guide_edit_mode();
  ui.guide_velocity_magnitude = phys_agent.guide_velocity_magnitude();
  ui.guide_velocity_delay_frames = phys_agent.guide_velocity_delay_frames();
  ui.guide_velocity_duration_frames = phys_agent.guide_velocity_duration_frames();
  ui.guide_force_magnitude = phys_agent.guide_force_magnitude();
  ui.guide_force_delay_frames = phys_agent.guide_force_delay_frames();
  ui.guide_force_duration_frames = phys_agent.guide_force_duration_frames();
  ui.selected_velocity_guidance = phys_agent.selected_velocity_guidance();
  ui.phys_current_frame_index = phys_agent.current_frame_index();
  ui.mesh_file_name = mesh_path.filename().string();
  ui.selection = interaction.selection;
  ui.selected_guide_vertices = phys_agent.selected_guide_vertices();
  ui.total_velocities = phys_agent.total_velocities();
  ui.linear_velocities = phys_agent.linear_velocities();
  ui.angular_velocities = phys_agent.angular_velocities();
  ui.active_velocity_guidances = phys_agent.active_velocity_guidances();
  ui.active_guide_velocities = phys_agent.active_guide_velocities();
  ui.active_guide_forces = phys_agent.active_guide_forces();
  ui.gpu_dispatch_debug = phys_agent.gpu_dispatch_debug_info();
  ui.recorded_frames = phys_agent.recorded_frames();
  ui.guide_keyframes = phys_agent.guide_keyframes();
  ui.animation_time = animation_time;

  if (interaction.selection.kind == SelectionKind::Vertex && interaction.selection.vertex >= 0 &&
      static_cast<size_t>(interaction.selection.vertex) < mesh.positions.size()) {
    ui.selected_vertex_position = mesh.positions[interaction.selection.vertex];
  }
  if (interaction.selection.kind == SelectionKind::Triangle && interaction.selection.triangle >= 0 &&
      static_cast<size_t>(interaction.selection.triangle) < mesh.triangle_material_gpa.size()) {
    ui.selected_triangle_material_gpa = mesh.triangle_material_gpa[interaction.selection.triangle];
  }

  return ui;
}

void AppFrameSyncGlue::ApplyRenderFrameResultOnPhysAgentAndMesh(
  const RenderFrameResult& frame,
  PhysAgent* phys_agent,
  Mesh* mesh) {
  if (!phys_agent || !mesh) {
    return;
  }

  phys_agent->SetGuideEditMode(frame.guide_edit_mode);
  phys_agent->SetGuideVelocitySettings(frame.guide_velocity_magnitude, frame.guide_velocity_delay_frames, frame.guide_velocity_duration_frames);
  phys_agent->SetGuideForceSettings(frame.guide_force_magnitude, frame.guide_force_delay_frames, frame.guide_force_duration_frames);

  if (frame.phys_state_restore_requested && frame.phys_state_restore_index >= 0) {
    phys_agent->RestoreRecordedFrame(*mesh, frame.phys_state_restore_index);
  }
  if (frame.phys_state_cache_requested) {
    phys_agent->CacheCurrentState();
  }
  if (frame.phys_reset_requested) {
    phys_agent->Reset(*mesh);
  }
  if (frame.phys_step_requested) {
    phys_agent->StepOnce(*mesh);
  }
  if (frame.recorded_frame_toggle_requested && frame.recorded_frame_toggle_index >= 0) {
    phys_agent->SetRecordedFrameExpanded(frame.recorded_frame_toggle_index, frame.recorded_frame_toggle_expanded);
  }
  if (frame.guide_keyframe_toggle_requested && frame.guide_keyframe_toggle_index >= 0) {
    const auto& guide_keyframes = phys_agent->guide_keyframes();
    if (frame.guide_keyframe_toggle_index < static_cast<int>(guide_keyframes.size())) {
      phys_agent->SetGuideKeyframeEnabled(guide_keyframes[frame.guide_keyframe_toggle_index].frame_index, frame.guide_keyframe_toggle_enabled);
    }
  }
  if (frame.guide_keyframe_expand_requested && frame.guide_keyframe_expand_index >= 0) {
    phys_agent->SetGuideKeyframeExpandedByIndex(frame.guide_keyframe_expand_index, frame.guide_keyframe_expand_expanded);
  }
  if (frame.triangle_material_change_requested && frame.triangle_material_triangle >= 0 &&
      static_cast<size_t>(frame.triangle_material_triangle) < mesh->triangle_material_gpa.size()) {
    mesh->triangle_material_gpa[frame.triangle_material_triangle] = frame.triangle_material_gpa > 0.0f ? frame.triangle_material_gpa : 0.001f;
  }
}

}  // namespace app_orchestration
