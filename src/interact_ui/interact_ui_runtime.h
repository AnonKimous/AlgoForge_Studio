#pragma once

#include "agent_management/agent_manager.h"
#include "capabilities/agent/agent.h"
#include "common_data/common_data.h"
#include "runtime_systems/runtime_environment.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace interact_ui {

struct AgentInterventionUiContext {
  const agent::Agent* agent{nullptr};
  const Mesh* mesh{nullptr};
  const InputState* input{nullptr};
  Vec2 mouse_pixel{};
  float dt_seconds{0.0f};
  AgentToAlgorithmSignal* agent_to_algorithm_signal{nullptr};
  const AlgorithmToAgentSignal* algorithm_to_agent_signal{nullptr};
};

class IAlgorithmInterventionPackageUiState {
 public:
  virtual ~IAlgorithmInterventionPackageUiState() = default;
};

class IAlgorithmInterventionPackageUi {
 public:
  virtual ~IAlgorithmInterventionPackageUi() = default;

  virtual const char* title() const = 0;
  virtual std::unique_ptr<IAlgorithmInterventionPackageUiState> CreateUiState() const = 0;
  virtual void DrawUi(
    const AgentInterventionUiContext& context,
    IAlgorithmInterventionPackageUiState* ui_state) const = 0;
};

struct AgentInterventionUiBinding {
  std::string algorithm_name;
  std::shared_ptr<IAlgorithmInterventionPackageUi> hook{};
};

class InteractUiRuntime {
 public:
  ~InteractUiRuntime();

  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
  bool CreateAgent(
    AgentCreateSpec spec,
    std::vector<AgentInterventionUiBinding> ui_bindings = {});
  bool Tick();
  void Destroy();

 private:
  struct ActiveCustomInterventionUiSlot {
    size_t agent_index{0u};
    size_t algorithm_index{0u};
    std::string algorithm_name;
    std::shared_ptr<IAlgorithmInterventionPackageUi> hook{};
    std::unique_ptr<IAlgorithmInterventionPackageUiState> state{};
  };

  struct AgentComposerUiState {
    std::array<char, 128> agent_name{};
    bool attach_temporary_test_line_motion{true};
    std::array<char, 128> algorithm_name{};
    std::array<char, 128> manifest_name{};
    std::array<char, 260> mesh_resource_path{};
    Mesh loaded_mesh_resource{};
    bool has_loaded_mesh_resource{false};
    std::array<float, 3> descriptor_scalar_values{};
    bool use_intervention{false};
    bool use_reflector{false};
  };

  void DrawInteractUi();
  void DrawAgentComposerUi();
  void DrawAgentBindingUi();
  void DrawMeshPreviewUi();
  void InitializeAgentComposerDefaults();
  void DrawCustomInterventionUi(
    size_t agent_index,
    size_t algorithm_index,
    const agent::AgentAlgorithmCodecGroup& group,
    ActiveCustomInterventionUiSlot* slot);
  void SyncCustomInterventionUiState();

  Mesh mesh_{};
  AgentManager agent_manager_{};
  runtime_systems::RuntimeEnvironment runtime_environment_{};
  std::string ui_status_message_{};
  struct ManagedAgentInterventionUiBindings {
    size_t agent_index{0u};
    std::vector<AgentInterventionUiBinding> bindings{};
  };
  std::vector<ManagedAgentInterventionUiBindings> managed_agent_ui_bindings_{};
  std::vector<ActiveCustomInterventionUiSlot> active_custom_ui_slots_{};
  AgentComposerUiState agent_composer_ui_state_{};
  bool agent_composer_defaults_initialized_{false};
  std::chrono::steady_clock::time_point last_frame_time_{};
  float frame_dt_{0.0f};
};

}  // namespace interact_ui

using interact_ui::AgentInterventionUiBinding;
using interact_ui::AgentInterventionUiContext;
using interact_ui::InteractUiRuntime;
using interact_ui::IAlgorithmInterventionPackageUi;
using interact_ui::IAlgorithmInterventionPackageUiState;
