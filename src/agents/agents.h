#pragma once

#include "agents/algorithm_pool.h"
#include "algorithm/algorithm_package.h"
#include "common_data/interaction/interaction_state.h"
#include "common_data/interaction/interaction_signals.h"
#include "common_data/mesh.h"
#include "runtime_systems/render/imgui_vulkan_runtime.h"
#include "runtime_systems/window/window.h"

#include <memory>
#include <utility>

namespace agents {

void DrawVertexArrayOverlay(
  const std::vector<Vec3>& vertex_positions,
  const std::vector<std::array<uint32_t, 2>>& triangle_edges,
  const std::vector<std::array<uint32_t, 3>>& triangles);

class AgentAlgorithmRuntime {
 public:
  void Init(
    const PhysSolverConfig& config,
    const VulkanComputeContextView& compute_context,
    const AlgorithmContainerDescriptor& container_descriptor);
  bool Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) const;
  void SetInterventionPackage(std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle> package);

  void SetAgentToAlgorithmSignal(const AgentToAlgorithmSignal& signal) { agent_to_algorithm_signal_ = signal; }
  void ApplyInterventionRequest(const InteractionInterventionRequest& request);
  void SetAlgorithmToAgentSignal(const AlgorithmToAgentSignal& signal) { algorithm_to_agent_signal_ = signal; }

  const PhysSolverConfig& config() const { return pool_.config(); }
  const VulkanComputeContextView& compute_context() const { return pool_.compute_context(); }
  const AlgorithmContainerDescriptor& container_descriptor() const { return pool_.container_descriptor(); }
  const std::shared_ptr<algorithm::AlgorithmInterventionPackageHandle>& intervention_package() const { return pool_.intervention_package(); }
  const AgentToAlgorithmSignal& agent_to_algorithm_signal() const { return agent_to_algorithm_signal_; }
  const AlgorithmToAgentSignal& algorithm_to_agent_signal() const { return algorithm_to_agent_signal_; }
  const InteractionInterventionRequest& intervention_request() const { return intervention_request_; }

 private:
  AlgorithmPool pool_{};
  InteractionInterventionRequest intervention_request_{};
  AgentToAlgorithmSignal agent_to_algorithm_signal_{};
  AlgorithmToAgentSignal algorithm_to_agent_signal_{};
};

class WindowAgent {
 public:
  using DrawCallback = runtime_systems::ImGuiVulkanRuntime::DrawCallback;

  bool Init(const char* title, int width, int height);
  bool Tick();
  void Destroy();
  void SetDrawCallback(DrawCallback callback);

  WindowHandle native_handle() const;
  int width() const;
  int height() const;
  const InputState& input() const;
  Vec2 MousePosition() const;

 private:
  std::unique_ptr<SdlWindow> window_;
  std::unique_ptr<runtime_systems::ImGuiVulkanRuntime> imgui_runtime_;
};

class RenderAgent {
 public:
  bool Init(const WindowHandle& window_handle);
  InteractionUiAction Tick(const Mesh& mesh, const InteractionUiState& ui_state);
  void Destroy();

  InteractionMode mode() const;
  PhysRunState phys_run_state() const;
  void SetPhysRunState(PhysRunState state);

 private:
  InteractionMode mode_{InteractionMode::Edit};
  PhysRunState phys_run_state_{PhysRunState::Pause};
};

}  // namespace agents

using agents::AgentAlgorithmRuntime;
