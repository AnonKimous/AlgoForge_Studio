#pragma once

#include "common_data/vector_types.h"
#include "common_data/phys_algorithm/velocity_math.h"

#include <cstdint>
#include <vector>

namespace common_data {

enum class PhysRunState {
  Run,
  Pause,
};

struct InterventionDisplacement {
  int vertex{-1};
  std::vector<int> vertices;
  Vec3 start{};
  Vec3 requested_target{};
  Vec3 allowed_target{};
  VelocityMatrix total_velocity{};
  bool hidden{false};
  bool valid{};
};

using PhysDirective = InterventionDisplacement;

struct InterventionVelocity {
  std::vector<int> vertices;
  VelocityMatrix velocity{};
  Vec3 display_delta{};
  uint32_t start_frame_offset{0};
  uint32_t duration_frames{1};
  bool hidden{false};
  bool valid{true};
};

struct InterventionForce {
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
  std::vector<InterventionDisplacement> displacement_interventions;
  std::vector<InterventionVelocity> velocity_interventions;
  std::vector<InterventionForce> force_interventions;
};

struct PhysicsStepOutput {
  std::vector<Vec3> positions;
  std::vector<VelocityMatrix> total_velocities;
  std::vector<VelocityMatrix> linear_velocities;
  std::vector<VelocityMatrix> angular_velocities;
  std::vector<InterventionDisplacement> displacement_interventions;
  std::vector<InterventionVelocity> velocity_interventions;
  std::vector<InterventionForce> force_interventions;
};

}  // namespace common_data


using common_data::PhysDirective;
using common_data::PhysRunState;
using common_data::PhysicsRestTriangle;
using common_data::PhysicsStepInput;
using common_data::PhysicsStepOutput;
using common_data::InterventionDisplacement;
using common_data::InterventionForce;
using common_data::InterventionVelocity;
