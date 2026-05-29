#include "guide_ui_frame_relay_on_phys_manager_buffers.h"

#include "../communication/io_protocol.h"
#include "../math/phys_algorithm/velocity_math.h"
#include "../physics/phys_manager_buffer_reflectors.h"

#include <cmath>

namespace app_orchestration {

GuideUiPhysBufferCommit BuildGuideUiFrameRelayOnPhysManagerBuffers(
  const Mesh& mesh,
  const GuideUiFrame& guide_ui_frame,
  GuideEditMode guide_edit_mode,
  float guide_velocity_magnitude,
  uint32_t guide_velocity_delay_frames,
  uint32_t guide_velocity_duration_frames,
  float guide_force_magnitude,
  uint32_t guide_force_delay_frames,
  uint32_t guide_force_duration_frames) {
  GuideUiPhysBufferCommit commit{};
  commit.packet.protocol.name = kGuideUiPhysIoProtocolName;
  commit.selected_vertices = guide_ui_frame.selected_vertices;
  if (!guide_ui_frame.dragging || guide_ui_frame.selected_vertices.empty()) {
    return commit;
  }

  const int primary_vertex = guide_ui_frame.selected_vertices.front();
  if (primary_vertex < 0 || static_cast<size_t>(primary_vertex) >= mesh.positions.size()) {
    return commit;
  }

  const Vec3& primary_start = mesh.positions[primary_vertex];
  Vec3 drag_delta{
    guide_ui_frame.drag_target.x - primary_start.x,
    guide_ui_frame.drag_target.y - primary_start.y,
    guide_ui_frame.drag_target.z - primary_start.z,
  };
  float drag_length = std::sqrt(
    drag_delta.x * drag_delta.x +
    drag_delta.y * drag_delta.y +
    drag_delta.z * drag_delta.z);
  Vec3 drag_direction{};
  if (drag_length > 1e-6f) {
    drag_direction = Vec3{
      drag_delta.x / drag_length,
      drag_delta.y / drag_length,
      drag_delta.z / drag_length,
    };
  }

  commit.packet.signal_buffer.push_back(IoSignalBufferEntry{
    "guide_ui_phys_commit",
    0u,
    1u,
    1u,
    1u,
  });

  switch (guide_edit_mode) {
    case GuideEditMode::Displacement: {
      VelocityGuidance guidance{};
      guidance.vertex = primary_vertex;
      guidance.vertices = guide_ui_frame.selected_vertices;
      guidance.start = primary_start;
      guidance.requested_target = Vec3{
        guide_ui_frame.drag_target.x,
        guide_ui_frame.drag_target.y,
        primary_start.z,
      };
      guidance.allowed_target = guidance.requested_target;
      guidance.total_velocity = MakeLinearVelocityMatrix(Vec3{
        guidance.requested_target.x - guidance.start.x,
        guidance.requested_target.y - guidance.start.y,
        guidance.requested_target.z - guidance.start.z,
      });
      guidance.valid = true;
      commit.packet.data_buffer.push_back(core_services::CreateVelocityGuidanceDataBufferEntry({guidance}));
      commit.packet.data_buffer.push_back(core_services::CreateVelocityGuidanceReflectorBufferEntry());
      break;
    }
    case GuideEditMode::Velocity: {
      VelocityGuideVelocity guide{};
      guide.vertices = guide_ui_frame.selected_vertices;
      guide.velocity = MakeLinearVelocityMatrix(Vec3{
        drag_direction.x * guide_velocity_magnitude,
        drag_direction.y * guide_velocity_magnitude,
        drag_direction.z * guide_velocity_magnitude,
      });
      guide.display_delta = drag_delta;
      guide.start_frame_offset = guide_velocity_delay_frames;
      guide.duration_frames = guide_velocity_duration_frames;
      guide.valid = true;
      commit.packet.data_buffer.push_back(core_services::CreateGuideVelocityDataBufferEntry({guide}));
      commit.packet.data_buffer.push_back(core_services::CreateGuideVelocityReflectorBufferEntry());
      break;
    }
    case GuideEditMode::Force: {
      VelocityGuideForce guide{};
      guide.vertices = guide_ui_frame.selected_vertices;
      guide.force = Vec3{
        drag_direction.x * guide_force_magnitude,
        drag_direction.y * guide_force_magnitude,
        drag_direction.z * guide_force_magnitude,
      };
      guide.display_delta = drag_delta;
      guide.start_frame_offset = guide_force_delay_frames;
      guide.duration_frames = guide_force_duration_frames;
      guide.valid = true;
      commit.packet.data_buffer.push_back(core_services::CreateGuideForceDataBufferEntry({guide}));
      commit.packet.data_buffer.push_back(core_services::CreateGuideForceReflectorBufferEntry());
      break;
    }
  }

  return commit;
}

}  // namespace app_orchestration
