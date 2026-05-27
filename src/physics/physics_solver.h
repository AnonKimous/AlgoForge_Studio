#pragma once

#include "physics_types.h"
#include "../mesh.h"

#include <vector>

class PhysicsSolver {
 public:
  void Init(const Mesh& mesh);
  void UpsertDirective(const Mesh& mesh, int vertex, Vec3 requested_target);
  void UpsertDirective(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target);
  void MoveDirective(const Mesh& mesh, int from, int to);
  void SetDirectiveHidden(int index, bool hidden);
  void SetDirectives(const Mesh& mesh, const std::vector<PhysDirective>& directives);
  void ApplyValidDirectives(Mesh& mesh, std::vector<DeltaMatrix>& vertex_deltas);
  PhysicsStepOutput Solve(const PhysicsStepInput& input) const;

  bool initialized() const { return initialized_; }
  const std::vector<PhysDirective>& directives() const { return directives_; }
  PhysicsStepInput BuildStepInput(const Mesh& mesh, const std::vector<DeltaMatrix>& vertex_deltas) const;

 private:
  bool initialized_{false};
  Mesh initial_mesh_{};
  std::vector<PhysicsRestTriangle> rest_matrices_;
  std::vector<PhysDirective> directives_;

  void RevalidateDirectives(const Mesh& mesh);
  int ClampDirectiveInsertionIndex(int index) const;
  bool IsMeshStateAllowed(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles) const;
  bool IsTriangleAllowed(const std::vector<Vec3>& positions, const PhysicsRestTriangle& rest) const;
  Vec3 FindNearestAllowedTarget(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles, int vertex, Vec3 start, Vec3 requested) const;
  void PropagateTriangleTension(const PhysicsStepInput& input, PhysicsStepOutput& output, const std::vector<bool>& guided) const;
};
