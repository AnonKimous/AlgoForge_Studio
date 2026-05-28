#pragma once

#include "physics_types.h"
#include "../mesh.h"

#include <vector>

class PhysicsSolver {
 public:
  void Init(const Mesh& mesh);
  void UpsertVelocityGuidance(const Mesh& mesh, int vertex, Vec3 requested_target);
  void UpsertVelocityGuidance(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target);
  void MoveVelocityGuidance(const Mesh& mesh, int from, int to);
  void SetVelocityGuidanceHidden(int index, bool hidden);
  void SetVelocityGuidances(const Mesh& mesh, const std::vector<VelocityGuidance>& guidances);
  void SetGuideVelocities(const std::vector<VelocityGuideVelocity>& guide_velocities);
  void SetGuideForces(const std::vector<VelocityGuideForce>& guide_forces);
  void ApplyValidVelocityGuidances(Mesh& mesh, std::vector<VelocityMatrix>& total_velocities, std::vector<VelocityMatrix>& linear_velocities, std::vector<VelocityMatrix>& angular_velocities);
  PhysicsStepOutput Solve(const PhysicsStepInput& input) const;

  void UpsertDirective(const Mesh& mesh, int vertex, Vec3 requested_target) { UpsertVelocityGuidance(mesh, vertex, requested_target); }
  void UpsertDirective(const Mesh& mesh, int vertex, Vec3 start, Vec3 requested_target) { UpsertVelocityGuidance(mesh, vertex, start, requested_target); }
  void MoveDirective(const Mesh& mesh, int from, int to) { MoveVelocityGuidance(mesh, from, to); }
  void SetDirectiveHidden(int index, bool hidden) { SetVelocityGuidanceHidden(index, hidden); }
  void SetDirectives(const Mesh& mesh, const std::vector<VelocityGuidance>& guidances) { SetVelocityGuidances(mesh, guidances); }
  void ApplyValidDirectives(Mesh& mesh, std::vector<VelocityMatrix>& total_velocities, std::vector<VelocityMatrix>& linear_velocities, std::vector<VelocityMatrix>& angular_velocities) { ApplyValidVelocityGuidances(mesh, total_velocities, linear_velocities, angular_velocities); }

  bool initialized() const { return initialized_; }
  const std::vector<VelocityGuidance>& guidances() const { return guidances_; }
  const std::vector<VelocityGuideVelocity>& guide_velocities() const { return guide_velocities_; }
  const std::vector<VelocityGuideForce>& guide_forces() const { return guide_forces_; }
  PhysicsStepInput BuildStepInput(const Mesh& mesh, const std::vector<VelocityMatrix>& total_velocities, const std::vector<VelocityMatrix>& linear_velocities, const std::vector<VelocityMatrix>& angular_velocities) const;

 private:
  bool initialized_{false};
  Mesh initial_mesh_{};
  std::vector<PhysicsRestTriangle> rest_matrices_;
  std::vector<VelocityGuidance> guidances_;
  std::vector<VelocityGuideVelocity> guide_velocities_;
  std::vector<VelocityGuideForce> guide_forces_;

  void RevalidateDirectives(const Mesh& mesh);
  int ClampDirectiveInsertionIndex(int index) const;
  bool IsMeshStateAllowed(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles) const;
  bool IsTriangleAllowed(const std::vector<Vec3>& positions, const PhysicsRestTriangle& rest) const;
  Vec3 FindNearestAllowedTarget(const std::vector<Vec3>& positions, const std::vector<PhysicsRestTriangle>& rest_triangles, int vertex, Vec3 start, Vec3 requested) const;
  void ApplyCorotatedResponse(const PhysicsStepInput& input, PhysicsStepOutput& output, const std::vector<bool>& guided) const;
};
