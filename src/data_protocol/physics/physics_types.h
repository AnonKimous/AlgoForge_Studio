#pragma once

#include "foundation/common/vector_types.h"
#include "foundation/phys_algorithm/velocity_math.h"

#include <cstdint>
#include <vector>

namespace foundation {

enum class PhysRunState {
  Run,
  Pause,
};

struct VelocityGuidance {
  int vertex{-1};
  std::vector<int> vertices;
  Vec3 start{};
  Vec3 requested_target{};
  Vec3 allowed_target{};
  VelocityMatrix total_velocity{};
  bool hidden{false};
  bool valid{};
};

using PhysDirective = VelocityGuidance;

struct VelocityGuideVelocity {
  std::vector<int> vertices;
  VelocityMatrix velocity{};
  Vec3 display_delta{};
  uint32_t start_frame_offset{0};
  uint32_t duration_frames{1};
  bool hidden{false};
  bool valid{true};
};

struct VelocityGuideForce {
  std::vector<int> vertices;
  Vec3 force{};
  Vec3 display_delta{};
  uint32_t start_frame_offset{0};
  uint32_t duration_frames{1};
  bool hidden{false};
  bool valid{true};
};

struct PhysicsRestTriangle {
  uint32_t origin{};
  uint32_t first{};
  uint32_t second{};
  float inv00{}, inv01{};
  float inv10{}, inv11{};
  float rest_area{};
  float material_gpa{1000000.0f};
};

struct PhysicsStepInput {
  std::vector<Vec3> positions;
  std::vector<Vec3> rest_positions;
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  std::vector<PhysicsRestTriangle> rest_triangles;
  std::vector<VelocityGuidance> guidances;
  std::vector<VelocityGuideVelocity> guide_velocities;
  std::vector<VelocityGuideForce> guide_forces;
};

struct PhysicsStepOutput {
  std::vector<Vec3> positions;
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  std::vector<VelocityGuidance> guidances;
  std::vector<VelocityGuideVelocity> guide_velocities;
  std::vector<VelocityGuideForce> guide_forces;
};

}  // namespace foundation

namespace data_protocol = foundation;

using foundation::PhysDirective;
using foundation::PhysRunState;
using foundation::PhysicsRestTriangle;
using foundation::PhysicsStepInput;
using foundation::PhysicsStepOutput;
using foundation::VelocityGuideForce;
using foundation::VelocityGuidance;
using foundation::VelocityGuideVelocity;
