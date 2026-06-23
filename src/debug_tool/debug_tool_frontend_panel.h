#pragma once

#include "common_data/common_data.h"
#include "debug_tool/debug_tool_host.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace debug_tool_frontend {

using debug_tool::IDebugToolHost;

struct AgentInterventionUiContext {
  size_t agent_index{0u};
  size_t algorithm_index{0u};
  const debug_tool::AgentRuntimeSummary* agent_summary{nullptr};
  const debug_tool::AlgorithmRuntimeSummary* algorithm_summary{nullptr};
  const InputState* input{nullptr};
  Vec2 mouse_pixel{};
  float dt_seconds{0.0f};
  const AgentToAlgorithmSignal* agent_to_algorithm_signal{nullptr};
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

class DebugToolFrontendPanel {
 public:
  void Draw(IDebugToolHost& host);
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
    debug_tool::AlgorithmExecutionPreference execution_preference{debug_tool::AlgorithmExecutionPreference::Gpu};
    std::vector<debug_tool::AlgorithmCatalogEntry> algorithm_catalog_entries;
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
    bool preview_request_dirty{true};
    bool pipeline_run_from_stage0_to_end{true};
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

  void DrawDebugToolFrontend(IDebugToolHost& host);
  void DrawWindowMenu();
  void DrawAgentComposerUi(IDebugToolHost& host);
  void DrawAgentBindingUi(IDebugToolHost& host);
  void DrawAgentManagerUi(IDebugToolHost& host);
  void DrawAgentDetailUi(IDebugToolHost& host);
  void DrawFileBrowserUi(IDebugToolHost& host);
  void DrawAlgorithmPreviewUi(IDebugToolHost& host);
  void InitializeAgentComposerDefaults();
  void InitializeFileBrowserDefaults();
  bool RefreshAlgorithmComposerBindings(IDebugToolHost& host, const std::string& algorithm_name);
  void DrawCustomInterventionUi(
    IDebugToolHost& host,
    size_t agent_index,
    size_t algorithm_index,
    const debug_tool::AgentRuntimeSummary& agent_summary,
    const debug_tool::AlgorithmRuntimeSummary& algorithm_summary,
    ActiveCustomInterventionUiSlot* slot);
  void SyncCustomInterventionUiState(IDebugToolHost& host);
  void InitializeAlgorithmCatalog(IDebugToolHost& host);

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

}  // namespace debug_tool_frontend

using debug_tool_frontend::AgentInterventionUiBinding;
using debug_tool_frontend::AgentInterventionUiContext;
using debug_tool_frontend::IAlgorithmInterventionPackageUi;
using debug_tool_frontend::IAlgorithmInterventionPackageUiState;
using debug_tool_frontend::DebugToolFrontendPanel;
