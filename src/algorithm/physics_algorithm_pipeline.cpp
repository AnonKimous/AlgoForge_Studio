#include "physics_algorithm_pipeline.h"

#include "cpu_physics_algorithm.h"
#include "gpu_physics_algorithm.h"

bool PhysicsAlgorithmPipeline_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  if (!result) return false;
  *result = PhysicsAlgorithmResult{};

  switch (request.config.solver_kind) {
    case PhysSolverKind::Cpu:
      return CpuPhysicsAlgorithm_Run(request, result);
    case PhysSolverKind::Gpu:
      return GpuPhysicsAlgorithm_Run(request, result);
  }

  return false;
}
