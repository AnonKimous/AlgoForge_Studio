#include "agent_execute_runtime.h"

#include "algorithm_library/camera_algorithm_package.h"
#include "algorithm_library/corotated_cpu_algorithm_contract.h"
#include "algorithm_library/physics_convolution_gpu_algorithm_contract.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace agent_execute {

namespace {

agent::AgentInitConfig BuildCameraAgentInfoFromMesh(const Mesh& mesh, const std::string& agent_name) {
  agent::AgentInitConfig info{};
  info.agent_name = agent_name.empty() ? "camera_agent" : agent_name;
  info.algorithm_name = kCameraAlgorithmName;

  auto codec = std::make_shared<CameraAlgorithmPackageCodec>();
  codec::VolumeDescriptor volume{};
  volume.point_position = mesh.positions;
  volume.point_velocity.assign(mesh.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
  volume.mass = 1.0f;
  volume.driving_dir = Vec3{0.0f, 0.0f, 1.0f};

  AlgorithmComplianceDescriptor compliance_descriptor{};
  codec->BuildContainerDescriptor(volume, &compliance_descriptor);
  info.compliance_packages.push_back(agent::AgentAlgorithmPackageHandle{
    kCameraAlgorithmName,
    codec});
  info.compliance_descriptor = compliance_descriptor;
  return info;
}

agent::AgentInitConfig BuildPhysicsAgentInfoFromMesh(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& gpu_shader_path,
  const std::string& agent_name) {
  agent::AgentInitConfig info{};
  info.agent_name = agent_name.empty() ? (algorithm_name.empty() ? "agent" : algorithm_name + "_agent") : agent_name;
  info.algorithm_name = algorithm_name.empty()
    ? (solver_kind == PhysSolverKind::Gpu ? kPhysicsConvolutionGpuAlgorithmName : kCorotatedCpuAlgorithmName)
    : algorithm_name;

  PhysSolverConfig solver_config{};
  solver_config.solver_kind = solver_kind;
  solver_config.algorithm_name = info.algorithm_name;
  solver_config.run_algorithm_on_init = true;
  if (solver_kind == PhysSolverKind::Gpu) {
    solver_config.gpu_shader.shader_name = gpu_shader_path;
    solver_config.gpu_shader.shader_mask.assign(9, 1u);
    solver_config.gpu_shader.shader_data.assign(9, 1.0f);
  }
  info.solver_config = solver_config;

  if (solver_kind == PhysSolverKind::Gpu) {
    info.compliance_descriptor = CreatePhysicsConvolutionGpuAlgorithmContainerDescriptor(
      static_cast<uint32_t>(mesh.positions.size()));
  } else {
    info.compliance_descriptor = CreateCorotatedCpuAlgorithmContainerDescriptor(
      static_cast<uint32_t>(mesh.positions.size()),
      static_cast<uint32_t>(mesh.triangles.size()));
  }
  info.compliance_packages.push_back(agent::AgentAlgorithmPackageHandle{
    info.algorithm_name,
    nullptr});
  info.intervention_package = std::make_shared<agent::AgentInterventionPackageHandle>();
  info.intervention_package->package_name = "physics_intervention";
  return info;
}

std::shared_ptr<agent::Agent> FindLastActiveAgent(
  const std::vector<std::shared_ptr<agent::Agent>>& slots) {
  for (std::size_t index = slots.size(); index-- > 0;) {
    const auto& slot = slots[index];
    if (!slot) {
      continue;
    }
    return slot;
  }
  return {};
}

}  // namespace

agent::AgentInitConfig CreateCameraAgentInfo(const Mesh& mesh) {
  return BuildCameraAgentInfoFromMesh(mesh, "camera_agent");
}

agent::AgentInitConfig CreatePhysicsAgentInfo(
  const Mesh& mesh,
  PhysSolverKind solver_kind,
  const std::string& algorithm_name,
  const std::string& gpu_shader_path) {
  return BuildPhysicsAgentInfoFromMesh(mesh, solver_kind, algorithm_name, gpu_shader_path, "");
}

bool AgentExecuteRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  mesh_ = mesh;
  initialized_ = window_agent_.Init(window_title ? window_title : "Agent Execute", width, height);
  if (!initialized_) {
    return false;
  }
  mesh_source_path_.clear();
  ui_status_message_ = "Mesh is prefilled. Create an agent to start work.";
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool AgentExecuteRuntime::LoadMeshFromFile(const std::string& path, std::string* error_message) {
  try {
    mesh_ = LoadMeshFile(path);
    mesh_source_path_ = path;
    agent_slots_.clear();
    active_agent_binding_.reset();
    active_agent_binding_ready_ = false;
    agent_slots_dirty_ = false;
    agent_ticker_.Destroy();
    ui_status_message_ = "Loaded mesh: " + path;
    return true;
  } catch (const std::exception& error) {
    if (error_message) {
      *error_message = error.what();
    }
    ui_status_message_ = std::string("Mesh load failed: ") + error.what();
    return false;
  }
}

void AgentExecuteRuntime::SetDrawCallback(std::function<void()> draw_callback) {
  draw_callback_ = std::move(draw_callback);
  window_agent_.SetDrawCallback(draw_callback_);
}

bool AgentExecuteRuntime::LoadMeshAndResetBindings(const std::string& path, std::string* status_message) {
  std::string error_message;
  if (!LoadMeshFromFile(path, &error_message)) {
    if (status_message) {
      *status_message = error_message;
    }
    return false;
  }
  if (status_message) {
    *status_message = "Loaded mesh and cleared existing agents.";
  }
  return true;
}

bool AgentExecuteRuntime::MountActiveAgent(const std::shared_ptr<agent::Agent>& agent) {
  if (active_agent_binding_ == agent && active_agent_binding_ready_) {
    return true;
  }
  if (!agent) {
    if (active_agent_binding_ready_) {
      agent_ticker_.Destroy();
    }
    active_agent_binding_.reset();
    active_agent_binding_ready_ = false;
    return true;
  }

  if (active_agent_binding_ready_) {
    agent_ticker_.Destroy();
    active_agent_binding_ready_ = false;
  }

  active_agent_binding_ = agent;
  agent_ticker_.Init(agent, VulkanComputeContextView{});
  agent_ticker_.SetRunState(PhysRunState::Run);
  active_agent_binding_ready_ = true;
  return true;
}

const std::vector<Vec3>& AgentExecuteRuntime::active_vertex_positions() const {
  return mesh_.positions;
}

void AgentExecuteRuntime::SetUiStatusMessage(std::string message) {
  ui_status_message_ = std::move(message);
}

AgentExecuteRuntime::AgentHandle AgentExecuteRuntime::LoadAgent(agent::AgentInitConfig info) {
  auto agent_instance = std::make_shared<agent::Agent>();
  if (!agent::agent_init(agent_instance.get(), std::move(info))) {
    return kInvalidAgentHandle;
  }

  const AgentHandle handle = agent_slots_.size();
  agent_slots_.push_back(agent_instance);
  agent_slots_dirty_ = true;
  return handle;
}

bool AgentExecuteRuntime::UnloadAgent(AgentHandle handle) {
  if (handle >= agent_slots_.size() || !agent_slots_[handle]) {
    return false;
  }
  agent_slots_[handle].reset();
  agent_slots_dirty_ = true;
  return true;
}

void AgentExecuteRuntime::RefreshBindingsFromAgentSlots() {
  MountActiveAgent(FindLastActiveAgent(agent_slots_));
}

bool AgentExecuteRuntime::Tick() {
  if (!initialized_) {
    return false;
  }
  if (!window_agent_.Tick()) {
    return false;
  }

  if (agent_slots_dirty_) {
    RefreshBindingsFromAgentSlots();
    agent_slots_dirty_ = false;
  }

  const auto now = std::chrono::steady_clock::now();
  float frame_dt = std::chrono::duration<float>(now - last_frame_time_).count();
  frame_dt = std::clamp(frame_dt, 0.0f, 0.05f);
  last_frame_time_ = now;

  if (active_agent_binding_ready_) {
    agent_ticker_.Tick(
      mesh_,
      window_agent_.input(),
      window_agent_.MousePosition(),
      frame_dt);
  }

  return true;
}

void AgentExecuteRuntime::Destroy() {
  agent_ticker_.Destroy();
  active_agent_binding_.reset();
  agent_slots_.clear();
  window_agent_.Destroy();
  mesh_ = Mesh{};
  mesh_source_path_.clear();
  initialized_ = false;
  active_agent_binding_ready_ = false;
  agent_slots_dirty_ = false;
  ui_status_message_.clear();
  draw_callback_ = {};
  last_frame_time_ = {};
}

}  // namespace agent_execute
