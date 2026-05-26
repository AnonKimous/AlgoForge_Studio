#pragma once

#include "../math_types.h"

#include <cstdint>
#include <vector>

enum class PhysSubMode {
  Run,
  Guide,
};

struct PhysDirective {
  int vertex{-1};
  Vec3 start{};
  Vec3 requested_target{};
  Vec3 allowed_target{};
  bool valid{};
};

struct PhysicsRestTriangle {
  uint32_t origin{};
  uint32_t first{};
  uint32_t second{};
  float inv00{}, inv01{};
  float inv10{}, inv11{};
};

struct PhysicsStepInput {
  std::vector<Vec3> positions;
  std::vector<PhysicsRestTriangle> rest_triangles;
  std::vector<PhysDirective> directives;
};

struct PhysicsStepOutput {
  std::vector<Vec3> positions;
  std::vector<PhysDirective> directives;
};
