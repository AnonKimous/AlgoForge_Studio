#pragma once

#include "capabilities/agent/agent.h"
#include "common_data/common_data.h"
#include "interact_ui/interact_ui_host.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace interact_ui {

struct AgentInterventionUiContext {
  const agent::Agent* agent{nullptr};
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

struct AlgorithmCatalogEntry {
  std::string algorithm_name;
  std::string display_name;
  std::string folder_name;
  std::string container_manifest_name;
  std::string decomposer_name;
  std::string reflector_name;
  std::string intervention_name;
};

class InteractUiPanel {
 public:
  void Draw(IInteractUiHost& host);
  void Destroy();
  void RegisterAgentUiBindings(size_t agent_index, std::vector<AgentInterventionUiBinding> ui_bindings);

 private:
  enum class UiPage {
    Agents,
    Files,
  };

  struct ActiveCustomInterventionUiSlot {
    size_t agent_index{0u};
    size_t algorithm_index{0u};
    std::string algorithm_name;
    std::shared_ptr<IAlgorithmInterventionPackageUi> hook{};
    std::unique_ptr<IAlgorithmInterventionPackageUiState> state{};
  };

  struct AgentComposerUiState {
    std::array<char, 128> agent_name{};
    std::array<char, 128> algorithm_name{};
    struct ResourceInput {
      std::string resource_name;
      std::string resource_kind;
      bool required{true};
      std::array<char, 260> resource_path{};
    };
    struct DescriptorInput {
      std::string descriptor_name;
      std::string container_name;
      uint32_t array_index{0u};
      float scalar_value{0.0f};
    };
    agent::AlgorithmExecutionPreference execution_preference{agent::AlgorithmExecutionPreference::Gpu};
    std::vector<interact_ui::AlgorithmCatalogEntry> algorithm_catalog_entries;
    int selected_algorithm_catalog_index{-1};
    int selected_agent_index{-1};
    int selected_algorithm_index{-1};
    bool algorithm_catalog_loaded{false};
    std::string algorithm_catalog_error;
    std::array<char, 512> algorithm_library_root_path{};
    std::array<char, 512> algorithm_library_current_path{};
    std::vector<std::string> algorithm_library_entry_names;
    std::vector<std::string> algorithm_library_entry_paths;
    std::vector<bool> algorithm_library_entry_is_directory;
    int selected_algorithm_library_entry_index{-1};
    std::string algorithm_library_browser_error;
    bool algorithm_library_browser_loaded{false};
    std::vector<ResourceInput> resource_inputs;
    std::vector<DescriptorInput> descriptor_inputs;
    std::string reflected_algorithm_name;
    std::string reflection_error;
    bool reflection_valid{false};
  };

  struct FileBrowserUiState {
    std::array<char, 512> root_path{};
    std::array<char, 512> current_path{};
    std::vector<std::string> entry_names;
    std::vector<std::string> entry_paths;
    std::vector<bool> entry_is_directory;
    int selected_entry_index{-1};
    bool loaded{false};
    std::string error_message;
  };

  struct ManagedAgentInterventionUiBindings {
    size_t agent_index{0u};
    std::vector<AgentInterventionUiBinding> bindings{};
  };

  void DrawInteractUi(IInteractUiHost& host);
  void DrawWindowMenu();
  void DrawAgentComposerUi(IInteractUiHost& host);
  void DrawAgentBindingUi(IInteractUiHost& host);
  void DrawAgentManagerUi(IInteractUiHost& host);
  void DrawAgentDetailUi(IInteractUiHost& host);
  void DrawFileBrowserUi(IInteractUiHost& host);
  void DrawAlgorithmPreviewUi(IInteractUiHost& host);
  void InitializeAgentComposerDefaults();
  void InitializeFileBrowserDefaults();
  void DrawCustomInterventionUi(
    IInteractUiHost& host,
    size_t agent_index,
    size_t algorithm_index,
    const agent::AgentAlgorithmCodecGroup& group,
    ActiveCustomInterventionUiSlot* slot);
  void SyncCustomInterventionUiState(IInteractUiHost& host);

  AgentComposerUiState agent_composer_ui_state_{};
  FileBrowserUiState file_browser_ui_state_{};
  bool agent_composer_defaults_initialized_{false};
  bool file_browser_defaults_initialized_{false};
  UiPage selected_page_{UiPage::Agents};
  bool show_agent_manager_window_{true};
  bool show_agent_detail_window_{true};
  bool show_file_browser_window_{true};
  bool show_render_preview_window_{true};
  bool show_runtime_status_window_{true};
  std::vector<ManagedAgentInterventionUiBindings> managed_agent_ui_bindings_{};
  std::vector<ActiveCustomInterventionUiSlot> active_custom_ui_slots_{};
};

}  // namespace interact_ui

using interact_ui::AgentInterventionUiBinding;
using interact_ui::AgentInterventionUiContext;
using interact_ui::IAlgorithmInterventionPackageUi;
using interact_ui::IAlgorithmInterventionPackageUiState;
using interact_ui::InteractUiPanel;
