#include "agent_execute_runtime.h"

#include "algorithm_library/camera_algorithm_package.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"

#include <memory>
#include <utility>

namespace agent_execute {

namespace {

AgentLaunchSpec BuildCameraAgentInfoFromMesh(const Mesh& mesh, const std::string& agent_name) {
  AgentLaunchSpec spec{};
  spec.agent_config.agent_name = agent_name.empty() ? "camera_agent" : agent_name;
  spec.agent_config.algorithm_name = kCameraAlgorithmName;

  auto codec = std::make_shared<CameraAlgorithmPackageCodec>();
  codec::VolumeDescriptor volume{};
  volume.point_position = mesh.positions;
  volume.point_velocity.assign(mesh.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
  volume.mass = 1.0f;
  volume.driving_dir = Vec3{0.0f, 0.0f, 1.0f};

  codec->BuildContainerDescriptor(volume, &spec.compliance_descriptor);
  return spec;
}

AgentLaunchSpec BuildPhysicsAgentInfoFromMesh(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& gpu_shader_path,
  const std::string& agent_name) {
  AgentLaunchSpec spec{};
  spec.agent_config.agent_name = agent_name.empty() ? (algorithm_name.empty() ? "agent" : algorithm_name + "_agent") : agent_name;
  spec.agent_config.algorithm_name = algorithm_name.empty()
    ? (solver_kind == PhysSolverKind::Gpu ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName)
    : algorithm_name;

  spec.solver_config.solver_kind = solver_kind;
  spec.solver_config.algorithm_name = spec.agent_config.algorithm_name;
  spec.solver_config.run_algorithm_on_init = true;
  if (solver_kind == PhysSolverKind::Gpu) {
    spec.solver_config.gpu_shader.shader_name = gpu_shader_path;
    spec.solver_config.gpu_shader.shader_mask.assign(9, 1u);
    spec.solver_config.gpu_shader.shader_data.assign(9, 1.0f);
  }

  if (solver_kind == PhysSolverKind::Gpu) {
    spec.compliance_descriptor = CreatePhysicsConvolutionGpuAlgorithmContainerDescriptor(
      static_cast<uint32_t>(mesh.positions.size()));
  } else {
    spec.compliance_descriptor = CreateCorotatedCpuAlgorithmContainerDescriptor(
      static_cast<uint32_t>(mesh.positions.size()),
      static_cast<uint32_t>(mesh.triangles.size()));
  }
  return spec;
}

}  // namespace

AgentLaunchSpec CreateCameraAgentInfo(const Mesh& mesh) {
  return BuildCameraAgentInfoFromMesh(mesh, "camera_agent");
}

AgentLaunchSpec CreatePhysicsAgentInfo(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& gpu_shader_path) {
  return BuildPhysicsAgentInfoFromMesh(mesh, solver_kind, algorithm_name, gpu_shader_path, "");
}

bool AgentExecuteRuntime::LoadAgent(AgentLaunchSpec spec) {
  auto agent_instance = std::make_shared<agent::Agent>();
  if (!agent::agent_init(agent_instance.get(), std::move(spec.agent_config))) {
    return false;
  }

  current_agent_ = std::move(agent_instance);
  current_launch_spec_ = std::move(spec);
  current_launch_spec_.agent_config = {};
  agent_ticker_.Destroy();
  agent_ticker_.Init(
    current_agent_,
    VulkanComputeContextView{},
    current_launch_spec_.solver_config,
    current_launch_spec_.compliance_descriptor);
  agent_ticker_.SetRunState(PhysRunState::Run);
  return true;
}

bool AgentExecuteRuntime::UnloadAgent() {
  if (!current_agent_) {
    return false;
  }
  agent_ticker_.Destroy();
  current_agent_.reset();
  current_launch_spec_ = {};
  return true;
}

bool AgentExecuteRuntime::Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  if (!current_agent_) {
    return true;
  }
  agent_ticker_.Tick(mesh, input, mouse_pixel, dt_seconds);
  return true;
}

void AgentExecuteRuntime::Destroy() {
  agent_ticker_.Destroy();
  current_agent_.reset();
  current_launch_spec_ = {};
}

}  // namespace agent_execute
