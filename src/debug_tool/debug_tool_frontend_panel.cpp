#include "debug_tool_frontend_panel.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <sstream>
#include <unordered_map>

namespace debug_tool_frontend {

namespace {

std::string _ProjectDataRootPath() {
  const std::filesystem::path base_path(SDL_GetBasePath() ? SDL_GetBasePath() : "");
  if (base_path.empty()) {
    return "data";
  }
  return (base_path.parent_path() / "data").string();
}

template <size_t N>
void _CopyTextToBuffer(const char* text, std::array<char, N>* out_buffer) {
  if (!out_buffer || N == 0u) {
    return;
  }

  out_buffer->fill('\0');
  if (!text) {
    return;
  }

  std::strncpy(out_buffer->data(), text, N - 1u);
  (*out_buffer)[N - 1u] = '\0';
}

template <size_t N>
void _SetTextBuffer(std::array<char, N>* out_buffer, const std::string& value) {
  _CopyTextToBuffer(value.c_str(), out_buffer);
}

int _FindAlgorithmCatalogIndex(
  const std::vector<debug_tool::AlgorithmCatalogEntry>& catalog_entries,
  const std::string& algorithm_name) {
  for (size_t i = 0; i < catalog_entries.size(); ++i) {
    if (catalog_entries[i].algorithm_name == algorithm_name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::string _TrimCopy(const char* text) {
  if (!text) {
    return {};
  }

  std::string value = text;
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(
    value.begin(),
    std::find_if(
      value.begin(),
      value.end(),
      [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
  value.erase(
    std::find_if(
      value.rbegin(),
      value.rend(),
      [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }).base(),
    value.end());
  return value;
}


void _PopWindowToViewportOnAppear() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

struct AlgorithmLibraryBrowserEntry {
  std::string name;
  std::string path;
  bool is_directory{false};
};

std::string _LeafName(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const std::filesystem::path fs_path(path);
  const std::string leaf = fs_path.filename().string();
  if (!leaf.empty()) {
    return leaf;
  }
  return fs_path.string();
}

std::vector<AlgorithmLibraryBrowserEntry> _LoadAlgorithmLibraryDirectoryEntries(
  const std::string& directory_path,
  std::string* out_error_message) {
  std::vector<AlgorithmLibraryBrowserEntry> entries;
  std::error_code ec;
  const std::filesystem::path root(directory_path);
  if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
    if (out_error_message) {
      *out_error_message = "Algorithm library folder does not exist: " + directory_path;
    }
    return entries;
  }

  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    std::error_code status_ec;

    AlgorithmLibraryBrowserEntry browser_entry{};
    browser_entry.name = entry.path().filename().string();
    browser_entry.path = entry.path().string();
    browser_entry.is_directory = entry.is_directory(status_ec);
    entries.push_back(std::move(browser_entry));
  }

  auto lower_copy = [](const std::string& text) {
    std::string value = text;
    std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
  };

  std::sort(
    entries.begin(),
    entries.end(),
    [&](const AlgorithmLibraryBrowserEntry& lhs, const AlgorithmLibraryBrowserEntry& rhs) {
      if (lhs.is_directory != rhs.is_directory) {
        return lhs.is_directory > rhs.is_directory;
      }
      const std::string lhs_name = lower_copy(lhs.name);
      const std::string rhs_name = lower_copy(rhs.name);
      if (lhs_name == rhs_name) {
        return lhs.path < rhs.path;
      }
      return lhs_name < rhs_name;
    });

  return entries;
}

void _SetBrowserEntries(
  const std::vector<AlgorithmLibraryBrowserEntry>& browser_entries,
  std::vector<std::string>* out_names,
  std::vector<std::string>* out_paths,
  std::vector<bool>* out_is_directory) {
  if (!out_names || !out_paths || !out_is_directory) {
    return;
  }
  out_names->clear();
  out_paths->clear();
  out_is_directory->clear();
  for (const AlgorithmLibraryBrowserEntry& entry : browser_entries) {
    out_names->push_back(entry.name);
    out_paths->push_back(entry.path);
    out_is_directory->push_back(entry.is_directory);
  }
}

bool _LoadBrowserState(
  const std::string& directory_path,
  std::vector<std::string>* out_names,
  std::vector<std::string>* out_paths,
  std::vector<bool>* out_is_directory,
  std::string* out_error_message) {
  const std::vector<AlgorithmLibraryBrowserEntry> browser_entries =
    _LoadAlgorithmLibraryDirectoryEntries(directory_path, out_error_message);
  if (browser_entries.empty()) {
    return false;
  }
  _SetBrowserEntries(browser_entries, out_names, out_paths, out_is_directory);
  return true;
}

const AgentInterventionUiBinding* _FindUiBinding(
  const std::vector<AgentInterventionUiBinding>& bindings,
  const std::string& algorithm_name) {
  for (const AgentInterventionUiBinding& binding : bindings) {
    if (binding.algorithm_name == algorithm_name) {
      return &binding;
    }
  }
  return nullptr;
}

template <typename T>
void _ResizePreservingValues(std::vector<T>* values, size_t new_size) {
  if (!values) {
    return;
  }
  values->resize(new_size);
}

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) assert((condition) && (message))
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

bool _ValidateRequestedResourceEntries(
  const std::vector<debug_tool::RequestedResourceEntry>& requested_resources,
  const std::string& algorithm_name,
  std::string* out_error_message) {
  for (size_t i = 0; i < requested_resources.size(); ++i) {
    const debug_tool::RequestedResourceEntry& resource = requested_resources[i];
    if (resource.resource_name.empty()) {
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (resource.resource_kind.empty()) {
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource kind for '" +
          algorithm_name + "'.";
      }
      return false;
    }
  }
  return true;
}

bool _ValidateRequestedDescriptorEntries(
  const std::vector<debug_tool::RequestedDescriptorEntry>& requested_descriptors,
  const std::string& algorithm_name,
  std::string* out_error_message) {
  for (size_t i = 0; i < requested_descriptors.size(); ++i) {
    const debug_tool::RequestedDescriptorEntry& descriptor = requested_descriptors[i];
    if (descriptor.descriptor_name.empty()) {
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a descriptor name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (descriptor.container_name.empty()) {
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a container name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
  }
  return true;
}

}  // namespace

void DebugToolFrontendPanel::InitializeAgentComposerDefaults() {
  if (agent_composer_defaults_initialized_) {
    return;
  }

  _CopyTextToBuffer("", &agent_composer_ui_state_.agent_name);
  _CopyTextToBuffer("", &agent_composer_ui_state_.algorithm_name);
  agent_composer_ui_state_.resource_inputs.clear();
  agent_composer_ui_state_.descriptor_inputs.clear();
  agent_composer_ui_state_.algorithm_catalog_entries.clear();
  agent_composer_ui_state_.selected_algorithm_catalog_index = -1;
  agent_composer_ui_state_.selected_agent_index = -1;
  agent_composer_ui_state_.selected_algorithm_index = -1;
  agent_composer_ui_state_.execution_preference = debug_tool::AlgorithmExecutionPreference::Gpu;
  agent_composer_ui_state_.algorithm_catalog_loaded = false;
  agent_composer_ui_state_.algorithm_catalog_error.clear();
  agent_composer_ui_state_.reflected_algorithm_name.clear();
  agent_composer_ui_state_.reflection_error.clear();
  agent_composer_ui_state_.reflection_valid = false;
  agent_composer_defaults_initialized_ = true;
}

void DebugToolFrontendPanel::InitializeAlgorithmCatalog(IDebugToolHost& host) {
  if (agent_composer_ui_state_.algorithm_catalog_loaded) {
    return;
  }

  std::string catalog_error;
  if (host.LoadAlgorithmCatalog(&agent_composer_ui_state_.algorithm_catalog_entries, &catalog_error)) {
    agent_composer_ui_state_.algorithm_catalog_loaded = true;
    agent_composer_ui_state_.algorithm_catalog_error.clear();
  } else {
    agent_composer_ui_state_.algorithm_catalog_error = std::move(catalog_error);
  }
}

bool DebugToolFrontendPanel::RefreshAlgorithmComposerBindings(
  IDebugToolHost& host,
  const std::string& algorithm_name) {
  if (algorithm_name.empty()) {
    agent_composer_ui_state_.reflected_algorithm_name.clear();
    agent_composer_ui_state_.reflection_error.clear();
    agent_composer_ui_state_.reflection_valid = false;
    agent_composer_ui_state_.resource_inputs.clear();
    agent_composer_ui_state_.descriptor_inputs.clear();
    return true;
  }

  std::vector<debug_tool::RequestedResourceEntry> requested_resources;
  std::vector<debug_tool::RequestedDescriptorEntry> requested_descriptors;
  std::string error_message;
  if (!host.QueryAlgorithmRequestedBindings(
        algorithm_name,
        &requested_resources,
        &requested_descriptors,
        &error_message) ||
      !_ValidateRequestedResourceEntries(requested_resources, algorithm_name, &error_message) ||
      !_ValidateRequestedDescriptorEntries(requested_descriptors, algorithm_name, &error_message)) {
    agent_composer_ui_state_.reflected_algorithm_name = algorithm_name;
    agent_composer_ui_state_.reflection_error = std::move(error_message);
    agent_composer_ui_state_.reflection_valid = false;
    agent_composer_ui_state_.resource_inputs.clear();
    agent_composer_ui_state_.descriptor_inputs.clear();
    return false;
  }

  agent_composer_ui_state_.reflected_algorithm_name = algorithm_name;
  agent_composer_ui_state_.reflection_error.clear();
  agent_composer_ui_state_.reflection_valid = true;
  agent_composer_ui_state_.resource_inputs.clear();
  agent_composer_ui_state_.descriptor_inputs.clear();
  _ResizePreservingValues(&agent_composer_ui_state_.resource_inputs, requested_resources.size());
  _ResizePreservingValues(&agent_composer_ui_state_.descriptor_inputs, requested_descriptors.size());
  for (size_t i = 0; i < requested_resources.size(); ++i) {
    agent_composer_ui_state_.resource_inputs[i].resource_name = requested_resources[i].resource_name;
    agent_composer_ui_state_.resource_inputs[i].resource_kind = requested_resources[i].resource_kind;
    agent_composer_ui_state_.resource_inputs[i].required = requested_resources[i].required;
    agent_composer_ui_state_.resource_inputs[i].resource_path.fill('\0');
  }
  for (size_t i = 0; i < requested_descriptors.size(); ++i) {
    agent_composer_ui_state_.descriptor_inputs[i].descriptor_name = requested_descriptors[i].descriptor_name;
    agent_composer_ui_state_.descriptor_inputs[i].container_name = requested_descriptors[i].container_name;
    agent_composer_ui_state_.descriptor_inputs[i].array_index = requested_descriptors[i].array_index;
    agent_composer_ui_state_.descriptor_inputs[i].scalar_value = 0.0f;
  }

  std::vector<debug_tool::AlgorithmResourceBinding> default_resource_bindings;
  std::vector<debug_tool::AlgorithmDescriptorValue> default_descriptor_values;
  bool has_default_file = false;
  error_message.clear();
  if (!host.LoadAlgorithmPackageDefaultBindings(
        algorithm_name,
        &default_resource_bindings,
        &default_descriptor_values,
        &has_default_file,
        &error_message)) {
    agent_composer_ui_state_.reflected_algorithm_name = algorithm_name;
    agent_composer_ui_state_.reflection_error = std::move(error_message);
    agent_composer_ui_state_.reflection_valid = false;
    agent_composer_ui_state_.resource_inputs.clear();
    agent_composer_ui_state_.descriptor_inputs.clear();
    return false;
  }
  if (!has_default_file) {
    return true;
  }

  std::unordered_map<std::string, size_t> resource_index_by_name;
  for (size_t i = 0; i < agent_composer_ui_state_.resource_inputs.size(); ++i) {
    resource_index_by_name.emplace(agent_composer_ui_state_.resource_inputs[i].resource_name, i);
  }
  for (const debug_tool::AlgorithmResourceBinding& default_binding : default_resource_bindings) {
    const auto found = resource_index_by_name.find(default_binding.resource_name);
    if (found == resource_index_by_name.end()) {
      agent_composer_ui_state_.reflected_algorithm_name = algorithm_name;
      agent_composer_ui_state_.reflection_error =
        "Default JSON contains an unknown resource binding: " + default_binding.resource_name;
      agent_composer_ui_state_.reflection_valid = false;
      agent_composer_ui_state_.resource_inputs.clear();
      agent_composer_ui_state_.descriptor_inputs.clear();
      return false;
    }
    agent_composer_ui_state_.resource_inputs[found->second].resource_path.fill('\0');
    _SetTextBuffer(
      &agent_composer_ui_state_.resource_inputs[found->second].resource_path,
      default_binding.source_path);
  }

  std::unordered_map<std::string, size_t> descriptor_index_by_name;
  for (size_t i = 0; i < agent_composer_ui_state_.descriptor_inputs.size(); ++i) {
    descriptor_index_by_name.emplace(agent_composer_ui_state_.descriptor_inputs[i].descriptor_name, i);
  }
  for (const debug_tool::AlgorithmDescriptorValue& default_value : default_descriptor_values) {
    const auto found = descriptor_index_by_name.find(default_value.descriptor_name);
    if (found == descriptor_index_by_name.end()) {
      agent_composer_ui_state_.reflected_algorithm_name = algorithm_name;
      agent_composer_ui_state_.reflection_error =
        "Default JSON contains an unknown descriptor value: " + default_value.descriptor_name;
      agent_composer_ui_state_.reflection_valid = false;
      agent_composer_ui_state_.resource_inputs.clear();
      agent_composer_ui_state_.descriptor_inputs.clear();
      return false;
    }
    agent_composer_ui_state_.descriptor_inputs[found->second].scalar_value = default_value.scalar_value;
  }

  return true;
}

void DebugToolFrontendPanel::InitializeFileBrowserDefaults() {
  if (file_browser_defaults_initialized_) {
    return;
  }

  const std::string data_root_path = _ProjectDataRootPath();
  _CopyTextToBuffer(data_root_path.c_str(), &file_browser_ui_state_.root_path);
  _CopyTextToBuffer(file_browser_ui_state_.root_path.data(), &file_browser_ui_state_.current_path);
  file_browser_ui_state_.entry_names.clear();
  file_browser_ui_state_.entry_paths.clear();
  file_browser_ui_state_.entry_is_directory.clear();
  file_browser_ui_state_.selected_entry_index = -1;
  file_browser_ui_state_.loaded = false;
  file_browser_ui_state_.error_message.clear();

  std::string error_message;
  if (_LoadBrowserState(
        file_browser_ui_state_.current_path.data(),
        &file_browser_ui_state_.entry_names,
        &file_browser_ui_state_.entry_paths,
        &file_browser_ui_state_.entry_is_directory,
        &error_message)) {
    file_browser_ui_state_.loaded = true;
  }
  if (!error_message.empty()) {
    file_browser_ui_state_.error_message = std::move(error_message);
  }

  file_browser_defaults_initialized_ = true;
}

void DebugToolFrontendPanel::RegisterAgentUiBindings(
  size_t agent_index,
  std::vector<AgentInterventionUiBinding> ui_bindings) {
  managed_agent_ui_bindings_.erase(
    std::remove_if(
      managed_agent_ui_bindings_.begin(),
      managed_agent_ui_bindings_.end(),
      [&](const ManagedAgentInterventionUiBindings& bindings) {
        return bindings.agent_index == agent_index;
      }),
    managed_agent_ui_bindings_.end());
  managed_agent_ui_bindings_.push_back(ManagedAgentInterventionUiBindings{
    .agent_index = agent_index,
    .bindings = std::move(ui_bindings),
  });
  active_custom_ui_slots_.clear();
}

void DebugToolFrontendPanel::SyncCustomInterventionUiState(IDebugToolHost& host) {
  if (!host.has_agents()) {
    active_custom_ui_slots_.clear();
    return;
  }

  std::vector<ActiveCustomInterventionUiSlot> updated_slots;
  for (size_t agent_index = 0; agent_index < host.agent_count(); ++agent_index) {
    debug_tool::AgentRuntimeSummary agent_summary{};
    if (!host.GetAgentSummary(agent_index, &agent_summary)) {
      continue;
    }

    const std::vector<AgentInterventionUiBinding>* ui_bindings = nullptr;
    for (const ManagedAgentInterventionUiBindings& agent_bindings : managed_agent_ui_bindings_) {
      if (agent_bindings.agent_index == agent_index) {
        ui_bindings = &agent_bindings.bindings;
        break;
      }
    }
    if (!ui_bindings) {
      continue;
    }

    updated_slots.reserve(updated_slots.size() + agent_summary.algorithms.size());
    for (size_t algorithm_index = 0; algorithm_index < agent_summary.algorithms.size(); ++algorithm_index) {
      const debug_tool::AlgorithmRuntimeSummary& algorithm_summary = agent_summary.algorithms[algorithm_index];
      if (algorithm_summary.algorithm_name.empty()) {
        continue;
      }

      const AgentInterventionUiBinding* binding =
        _FindUiBinding(*ui_bindings, algorithm_summary.algorithm_name);
      if (!binding || !binding->hook) {
        continue;
      }

      ActiveCustomInterventionUiSlot slot{};
      slot.agent_index = agent_index;
      slot.algorithm_index = algorithm_index;
      slot.algorithm_name = algorithm_summary.algorithm_name;
      slot.hook = binding->hook;

      for (ActiveCustomInterventionUiSlot& previous_slot : active_custom_ui_slots_) {
        if (previous_slot.agent_index == slot.agent_index &&
            previous_slot.algorithm_index == slot.algorithm_index &&
            previous_slot.hook == slot.hook &&
            previous_slot.algorithm_name == slot.algorithm_name) {
          slot.state = std::move(previous_slot.state);
          break;
        }
      }

      if (!slot.state) {
        slot.state = slot.hook->CreateUiState();
      }
      updated_slots.push_back(std::move(slot));
    }
  }

  active_custom_ui_slots_ = std::move(updated_slots);
}

void DebugToolFrontendPanel::DrawCustomInterventionUi(
  IDebugToolHost& host,
  size_t agent_index,
  size_t algorithm_index,
  const debug_tool::AgentRuntimeSummary& agent_summary,
  const debug_tool::AlgorithmRuntimeSummary& algorithm_summary,
  ActiveCustomInterventionUiSlot* slot) {
  if (!slot || !slot->hook) {
    ImGui::TextUnformatted("No custom intervention UI.");
    return;
  }
  if (!slot->state) {
    ImGui::TextUnformatted("Custom intervention UI state is unavailable.");
    return;
  }

  const AgentInterventionUiContext context{
    .agent_index = agent_index,
    .algorithm_index = algorithm_index,
    .agent_summary = &agent_summary,
    .algorithm_summary = &algorithm_summary,
    .input = &host.input(),
    .mouse_pixel = host.mouse_position(),
    .dt_seconds = host.frame_dt_seconds(),
    .agent_to_algorithm_signal = &algorithm_summary.agent_to_algorithm_signal,
    .algorithm_to_agent_signal = &algorithm_summary.algorithm_to_agent_signal,
  };
  const std::string header_label = algorithm_summary.algorithm_name.empty()
    ? ("Algorithm " + std::to_string(algorithm_index))
    : algorithm_summary.algorithm_name;
  ImGui::PushID(static_cast<int>(algorithm_index));
  ImGui::SeparatorText(header_label.c_str());
  slot->hook->DrawUi(context, slot->state.get());
  ImGui::PopID();
}

void DebugToolFrontendPanel::DrawAgentComposerUi(IDebugToolHost& host) {
  InitializeAgentComposerDefaults();
  InitializeAlgorithmCatalog(host);

  ImGui::SeparatorText("Debug Agent");
  ImGui::Text("Built-in agent count: %zu", host.agent_count());
  if (!host.has_agents()) {
    ImGui::TextUnformatted("Debug agent is unavailable.");
    return;
  }

  agent_composer_ui_state_.selected_agent_index = 0;
  debug_tool::AgentRuntimeSummary selected_agent_summary{};
  const bool selected_agent_available = host.GetAgentSummary(0u, &selected_agent_summary);
  ImGui::Text(
    "Target Agent: %s",
    selected_agent_available && !selected_agent_summary.agent_name.empty()
      ? selected_agent_summary.agent_name.c_str()
      : "Debug Agent");

  if (!agent_composer_ui_state_.algorithm_catalog_entries.empty()) {
    const std::string current_display = (agent_composer_ui_state_.selected_algorithm_catalog_index >= 0 &&
      agent_composer_ui_state_.selected_algorithm_catalog_index <
        static_cast<int>(agent_composer_ui_state_.algorithm_catalog_entries.size()))
      ? agent_composer_ui_state_.algorithm_catalog_entries[static_cast<size_t>(agent_composer_ui_state_.selected_algorithm_catalog_index)].display_name
      : "Select algorithm...";
    if (ImGui::BeginCombo("Algorithm Catalog", current_display.c_str())) {
      for (size_t i = 0; i < agent_composer_ui_state_.algorithm_catalog_entries.size(); ++i) {
        const auto& entry = agent_composer_ui_state_.algorithm_catalog_entries[i];
        const bool is_selected = static_cast<int>(i) == agent_composer_ui_state_.selected_algorithm_catalog_index;
        if (ImGui::Selectable(entry.display_name.c_str(), is_selected)) {
          agent_composer_ui_state_.selected_algorithm_catalog_index = static_cast<int>(i);
          _SetTextBuffer(&agent_composer_ui_state_.algorithm_name, entry.algorithm_name);
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  } else if (!agent_composer_ui_state_.algorithm_catalog_error.empty()) {
    ImGui::TextWrapped("%s", agent_composer_ui_state_.algorithm_catalog_error.c_str());
  }

  ImGui::SeparatorText("Mount Algorithm");
  ImGui::TextUnformatted("Use the Files page to browse folders and drag paths into resource slots.");
  const bool algorithm_running = selected_agent_available && !selected_agent_summary.algorithms.empty();
  ImGui::BeginDisabled(algorithm_running);

  const std::string algorithm_name = _TrimCopy(agent_composer_ui_state_.algorithm_name.data());
  if (algorithm_name.empty()) {
    agent_composer_ui_state_.selected_algorithm_catalog_index = -1;
    RefreshAlgorithmComposerBindings(host, algorithm_name);
  } else {
    const int catalog_index = _FindAlgorithmCatalogIndex(agent_composer_ui_state_.algorithm_catalog_entries, algorithm_name);
    agent_composer_ui_state_.selected_algorithm_catalog_index = catalog_index;
    if (algorithm_name != agent_composer_ui_state_.reflected_algorithm_name) {
      RefreshAlgorithmComposerBindings(host, algorithm_name);
    }
  }

  ImGui::InputText(
    "Algorithm Name",
    agent_composer_ui_state_.algorithm_name.data(),
    agent_composer_ui_state_.algorithm_name.size());
  if (!algorithm_name.empty() && agent_composer_ui_state_.selected_algorithm_catalog_index < 0) {
    ImGui::TextUnformatted("Algorithm is not in catalog; manual entry only.");
  }

  if (!agent_composer_ui_state_.reflection_error.empty()) {
    ImGui::TextWrapped("%s", agent_composer_ui_state_.reflection_error.c_str());
  }

  ImGui::SeparatorText("Execution Mode");
  const char* execution_mode_text = "GPU";
  switch (agent_composer_ui_state_.execution_preference) {
    case debug_tool::AlgorithmExecutionPreference::Cpu: execution_mode_text = "CPU"; break;
    case debug_tool::AlgorithmExecutionPreference::Gpu: execution_mode_text = "GPU"; break;
  }
  if (ImGui::BeginCombo("Backend", execution_mode_text)) {
    const struct Option {
      const char* label;
      debug_tool::AlgorithmExecutionPreference value;
    } options[] = {
      {"CPU", debug_tool::AlgorithmExecutionPreference::Cpu},
      {"GPU", debug_tool::AlgorithmExecutionPreference::Gpu},
    };
    for (const Option& option : options) {
      const bool is_selected = agent_composer_ui_state_.execution_preference == option.value;
      if (ImGui::Selectable(option.label, is_selected)) {
        agent_composer_ui_state_.execution_preference = option.value;
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SeparatorText("Requested Resources");
  if (agent_composer_ui_state_.resource_inputs.empty()) {
    ImGui::TextUnformatted("No requested resources exposed by the algorithm package.");
  } else {
    const auto& dropped_files = host.input().dropped_files;
    std::vector<bool> drop_consumed(dropped_files.size(), false);
    for (size_t i = 0; i < agent_composer_ui_state_.resource_inputs.size(); ++i) {
      auto& resource = agent_composer_ui_state_.resource_inputs[i];
      ImGui::PushID(static_cast<int>(i));
      const std::string label = resource.resource_name.empty()
        ? ("Resource " + std::to_string(i))
        : resource.resource_name;
      ImGui::Text("%s%s%s",
        label.c_str(),
        resource.resource_kind.empty() ? "" : " [",
        resource.resource_kind.empty() ? "" : resource.resource_kind.c_str());
      if (!resource.resource_kind.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("]");
      }
      ImGui::SetNextItemWidth(-1.0f);
      const std::string path_label = "##resource_path_" + std::to_string(i);
      ImGui::InputText(path_label.c_str(), resource.resource_path.data(), resource.resource_path.size());
      const ImVec2 path_min = ImGui::GetItemRectMin();
      const ImVec2 path_max = ImGui::GetItemRectMax();
      for (size_t drop_index = 0; drop_index < dropped_files.size(); ++drop_index) {
        if (drop_consumed[drop_index]) {
          continue;
        }
        const auto& dropped_file = dropped_files[drop_index];
        if (dropped_file.x < static_cast<int>(path_min.x) || dropped_file.x > static_cast<int>(path_max.x) ||
            dropped_file.y < static_cast<int>(path_min.y) || dropped_file.y > static_cast<int>(path_max.y)) {
          continue;
        }
        _SetTextBuffer(&resource.resource_path, dropped_file.path);
        drop_consumed[drop_index] = true;
        break;
      }
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
          if (payload->Data && payload->DataSize > 0) {
            _SetTextBuffer(&resource.resource_path, static_cast<const char*>(payload->Data));
          }
        } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FOLDER_PATH")) {
          if (payload->Data && payload->DataSize > 0) {
            _SetTextBuffer(&resource.resource_path, static_cast<const char*>(payload->Data));
          }
        }
        ImGui::EndDragDropTarget();
      }
      if (ImGui::BeginDragDropSource()) {
        const std::string current_path = _TrimCopy(resource.resource_path.data());
        if (!current_path.empty()) {
          const size_t source_index = i;
          ImGui::SetDragDropPayload("RESOURCE_SLOT_INDEX", &source_index, sizeof(source_index));
          ImGui::TextUnformatted(current_path.c_str());
        }
        ImGui::EndDragDropSource();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) {
        resource.resource_path.fill('\0');
      }
      if (!resource.required) {
        ImGui::TextUnformatted("Optional resource.");
      }
      ImGui::PopID();
    }
    ImGui::SeparatorText("Resource Trash");
    ImGui::Button("Drop dragged resource here to remove");
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RESOURCE_SLOT_INDEX")) {
        if (payload->DataSize == sizeof(size_t)) {
          const size_t source_index = *static_cast<const size_t*>(payload->Data);
          if (source_index < agent_composer_ui_state_.resource_inputs.size()) {
            agent_composer_ui_state_.resource_inputs[source_index].resource_path.fill('\0');
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
  }

  ImGui::SeparatorText("Requested Descriptors");
  if (agent_composer_ui_state_.descriptor_inputs.empty()) {
    ImGui::TextUnformatted("No requested descriptors exposed by the algorithm package.");
  } else {
    for (size_t i = 0; i < agent_composer_ui_state_.descriptor_inputs.size(); ++i) {
      auto& descriptor = agent_composer_ui_state_.descriptor_inputs[i];
      ImGui::PushID(static_cast<int>(1000 + i));
      const std::string label = descriptor.descriptor_name.empty()
        ? ("Descriptor " + std::to_string(i))
        : descriptor.descriptor_name;
      ImGui::Text("%s -> %s[%u]",
        label.c_str(),
        descriptor.container_name.empty() ? "<container>" : descriptor.container_name.c_str(),
        descriptor.array_index);
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputFloat("Value", &descriptor.scalar_value, 0.1f, 1.0f, "%.3f");
      ImGui::PopID();
    }
  }

  ImGui::EndDisabled();
  if (!ImGui::Button("Start Algorithm")) {
    return;
  }

  if (algorithm_name.empty()) {
    host.ui_status_message() = "Algorithm name must not be empty.";
    return;
  }

  std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings;
  for (const auto& resource : agent_composer_ui_state_.resource_inputs) {
    resource_bindings.push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .source_path = _TrimCopy(resource.resource_path.data()),
    });
  }
  std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values;
  for (const auto& descriptor : agent_composer_ui_state_.descriptor_inputs) {
    descriptor_values.push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = descriptor.descriptor_name,
      .scalar_value = descriptor.scalar_value,
    });
  }

  size_t attached_algorithm_index = 0u;
  std::string attach_error_message;
  if (!host.AttachAlgorithmToAgent(
        0u,
        algorithm_name,
        resource_bindings,
        descriptor_values,
        &attached_algorithm_index,
        &attach_error_message,
        debug_tool::AlgorithmMountMode::Direct,
        agent_composer_ui_state_.execution_preference)) {
    host.ui_status_message() = attach_error_message.empty()
      ? "Failed to attach algorithm to built-in agent."
      : std::move(attach_error_message);
    return;
  }

  (void)attached_algorithm_index;
  host.StartTicking();
  host.ui_status_message() = "Algorithm started on built-in agent.";
}

void DebugToolFrontendPanel::DrawAgentBindingUi(IDebugToolHost& host) {
  DrawAgentComposerUi(host);
  ImGui::Separator();

  ImGui::Text("Managed agents: %zu", host.agent_count());

  if (!host.has_agents()) {
    ImGui::TextUnformatted("Built-in debug agent is unavailable.");
    return;
  }

  for (size_t agent_index = 0; agent_index < host.agent_count(); ++agent_index) {
    debug_tool::AgentRuntimeSummary agent_summary{};
    if (!host.GetAgentSummary(agent_index, &agent_summary)) {
      continue;
    }

    const std::string agent_header = agent_summary.agent_name.empty()
      ? ("Agent #" + std::to_string(agent_index))
      : ("Agent #" + std::to_string(agent_index) + "  " + agent_summary.agent_name);
    ImGui::SeparatorText(agent_header.c_str());
    ImGui::Text("Algorithms: %zu", agent_summary.algorithms.size());
    for (size_t algorithm_index = 0; algorithm_index < agent_summary.algorithms.size(); ++algorithm_index) {
      const debug_tool::AlgorithmRuntimeSummary& algorithm_summary = agent_summary.algorithms[algorithm_index];
      if (algorithm_summary.algorithm_name.empty()) {
        continue;
      }
      const std::string algorithm_name = algorithm_summary.algorithm_name.empty()
        ? ("<algorithm " + std::to_string(algorithm_index) + ">")
        : algorithm_summary.algorithm_name;
      ImGui::BulletText("#%zu %s", algorithm_index, algorithm_name.c_str());
    }
  }

  SyncCustomInterventionUiState(host);
  for (ActiveCustomInterventionUiSlot& slot : active_custom_ui_slots_) {
    debug_tool::AgentRuntimeSummary agent_summary{};
    if (!host.GetAgentSummary(slot.agent_index, &agent_summary) ||
        slot.algorithm_index >= agent_summary.algorithms.size() ||
        !slot.hook) {
      continue;
    }
    const debug_tool::AlgorithmRuntimeSummary& algorithm_summary = agent_summary.algorithms[slot.algorithm_index];
    ImGui::Spacing();
    const char* custom_title = slot.hook->title();
    ImGui::SeparatorText(custom_title ? custom_title : "Custom Intervention UI");
    DrawCustomInterventionUi(host, slot.agent_index, slot.algorithm_index, agent_summary, algorithm_summary, &slot);
  }
}

void DebugToolFrontendPanel::DrawFileBrowserUi(IDebugToolHost&) {
  InitializeFileBrowserDefaults();
  if (!ImGui::Begin("Files")) {
    ImGui::End();
    return;
  }

  auto refresh_browser = [&](const std::string& directory_path) {
    std::string browser_error;
    if (_LoadBrowserState(
          directory_path,
          &file_browser_ui_state_.entry_names,
          &file_browser_ui_state_.entry_paths,
          &file_browser_ui_state_.entry_is_directory,
          &browser_error)) {
      file_browser_ui_state_.selected_entry_index = -1;
      file_browser_ui_state_.loaded = true;
      file_browser_ui_state_.error_message.clear();
      return true;
    }
    file_browser_ui_state_.selected_entry_index = -1;
    file_browser_ui_state_.loaded = false;
    file_browser_ui_state_.error_message = std::move(browser_error);
    return false;
  };

  ImGui::SeparatorText("File Browser");
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText(
    "Current Path",
    file_browser_ui_state_.current_path.data(),
    file_browser_ui_state_.current_path.size());

  if (ImGui::Button("Open Path")) {
    refresh_browser(file_browser_ui_state_.current_path.data());
  }
  ImGui::SameLine();
  if (ImGui::Button("Open Root")) {
    _SetTextBuffer(&file_browser_ui_state_.current_path, file_browser_ui_state_.root_path.data());
    refresh_browser(file_browser_ui_state_.current_path.data());
  }
  ImGui::SameLine();
  if (ImGui::Button("Up")) {
    const std::filesystem::path current_path(file_browser_ui_state_.current_path.data());
    const std::filesystem::path parent_path = current_path.parent_path();
    if (!parent_path.empty()) {
      _SetTextBuffer(&file_browser_ui_state_.current_path, parent_path.string());
      refresh_browser(file_browser_ui_state_.current_path.data());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    refresh_browser(file_browser_ui_state_.current_path.data());
  }

  if (!file_browser_ui_state_.error_message.empty()) {
    ImGui::TextWrapped("%s", file_browser_ui_state_.error_message.c_str());
  }

  ImGui::Text("Root: %s", file_browser_ui_state_.root_path.data());
  ImGui::Text("Current: %s", file_browser_ui_state_.current_path.data());
  ImGui::TextUnformatted("Drag files from this page into resource inputs.");

  if (file_browser_ui_state_.entry_names.empty()) {
    ImGui::TextUnformatted("No files or folders in this directory.");
    ImGui::End();
    return;
  }

  ImGui::BeginChild("FileBrowserEntries", ImVec2(0.0f, 320.0f), true);
  for (size_t i = 0; i < file_browser_ui_state_.entry_names.size(); ++i) {
    const bool is_selected = static_cast<int>(i) == file_browser_ui_state_.selected_entry_index;
    const bool is_directory =
      i < file_browser_ui_state_.entry_is_directory.size() && file_browser_ui_state_.entry_is_directory[i];
    const std::string& entry_name = file_browser_ui_state_.entry_names[i];
    const std::string& entry_path = file_browser_ui_state_.entry_paths[i];
    ImGui::PushID(static_cast<int>(i));
    const std::string label = is_directory ? ("[DIR] " + entry_name) : ("[FILE] " + entry_name);
    if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
      file_browser_ui_state_.selected_entry_index = static_cast<int>(i);
      if (is_directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        _SetTextBuffer(&file_browser_ui_state_.current_path, entry_path);
        refresh_browser(file_browser_ui_state_.current_path.data());
      }
    }
    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload(
        is_directory ? "FOLDER_PATH" : "FILE_PATH",
        entry_path.c_str(),
        entry_path.size() + 1u);
      ImGui::TextUnformatted(entry_path.c_str());
      ImGui::EndDragDropSource();
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  if (file_browser_ui_state_.selected_entry_index >= 0 &&
      file_browser_ui_state_.selected_entry_index < static_cast<int>(file_browser_ui_state_.entry_paths.size())) {
    const size_t selected_index = static_cast<size_t>(file_browser_ui_state_.selected_entry_index);
    const bool selected_is_directory =
      selected_index < file_browser_ui_state_.entry_is_directory.size() &&
      file_browser_ui_state_.entry_is_directory[selected_index];
    const std::string& selected_path = file_browser_ui_state_.entry_paths[selected_index];
    ImGui::Text("Selected: %s", selected_path.c_str());
    if (selected_is_directory) {
      if (ImGui::Button("Enter Selected Folder")) {
        _SetTextBuffer(&file_browser_ui_state_.current_path, selected_path);
        refresh_browser(file_browser_ui_state_.current_path.data());
      }
    }
  }

  ImGui::End();
}

void DebugToolFrontendPanel::DrawAgentManagerUi(IDebugToolHost& host) {
  InitializeAgentComposerDefaults();

  if (!ImGui::Begin("Agent Manager")) {
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("Debug Agent");
  ImGui::Text("Built-in agent count: %zu", host.agent_count());
  ImGui::Text("Tick State: %s", host.tick_enabled() ? "running" : "paused");
  if (ImGui::Button("Start Tick")) {
    host.StartTicking();
    host.ui_status_message() = "Agent manager tick started.";
  }
  ImGui::SameLine();
  if (ImGui::Button("Pause Tick")) {
    host.PauseTicking();
    host.ui_status_message() = "Agent manager tick paused.";
  }
  if (!host.has_agents()) {
    ImGui::TextUnformatted("Debug agent is unavailable.");
    ImGui::End();
    return;
  }

  if (agent_composer_ui_state_.selected_agent_index < 0 ||
      agent_composer_ui_state_.selected_agent_index >= static_cast<int>(host.agent_count())) {
    agent_composer_ui_state_.selected_agent_index = 0;
  }
  agent_composer_ui_state_.selected_agent_index = 0;

  for (size_t agent_index = 0; agent_index < host.agent_count(); ++agent_index) {
    debug_tool::AgentRuntimeSummary agent_summary{};
    if (!host.GetAgentSummary(agent_index, &agent_summary)) {
      continue;
    }

    const std::string agent_header = agent_summary.agent_name.empty()
      ? ("Agent #" + std::to_string(agent_index))
      : ("Agent #" + std::to_string(agent_index) + "  " + agent_summary.agent_name);
    ImGui::PushID(static_cast<int>(agent_index));
    if (agent_summary.algorithms.empty()) {
      const ImGuiTreeNodeFlags leaf_flags =
        ImGuiTreeNodeFlags_Leaf |
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ((agent_index == 0u)
          ? ImGuiTreeNodeFlags_Selected
          : 0);
      ImGui::TreeNodeEx(agent_header.c_str(), leaf_flags);
      ImGui::TextUnformatted("No algorithms.");
    } else {
      ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;
      if (agent_index == 0u) {
        flags |= ImGuiTreeNodeFlags_Selected;
      }

      const bool opened = ImGui::TreeNodeEx(agent_header.c_str(), flags);

      if (opened) {
        for (size_t algorithm_index = 0; algorithm_index < agent_summary.algorithms.size(); ++algorithm_index) {
          const debug_tool::AlgorithmRuntimeSummary& algorithm_summary = agent_summary.algorithms[algorithm_index];
          if (algorithm_summary.algorithm_name.empty()) {
            continue;
          }
          const std::string algorithm_name = algorithm_summary.algorithm_name.empty()
            ? ("<algorithm " + std::to_string(algorithm_index) + ">")
            : algorithm_summary.algorithm_name;
          const bool algorithm_selected =
            agent_index == 0u &&
            static_cast<int>(algorithm_index) == agent_composer_ui_state_.selected_algorithm_index;
          if (ImGui::Selectable(algorithm_name.c_str(), algorithm_selected)) {
            agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(algorithm_index);
          }
        }
        ImGui::TreePop();
      }
    }
    ImGui::PopID();
  }

  ImGui::End();
}

void DebugToolFrontendPanel::DrawAgentDetailUi(IDebugToolHost& host) {
  InitializeAgentComposerDefaults();
  InitializeAlgorithmCatalog(host);

  if (!ImGui::Begin("Algorithm Detail")) {
    ImGui::End();
    return;
  }
  if (!host.has_agents()) {
    ImGui::TextUnformatted("Built-in debug agent is unavailable.");
    ImGui::End();
    return;
  }

  agent_composer_ui_state_.selected_agent_index = 0;
  debug_tool::AgentRuntimeSummary selected_agent_summary{};
  if (!host.GetAgentSummary(0u, &selected_agent_summary)) {
    ImGui::TextUnformatted("Built-in debug agent is unavailable.");
    ImGui::End();
    return;
  }

  const std::string selected_agent_header = selected_agent_summary.agent_name.empty()
    ? "Debug Agent"
    : ("Debug Agent  " + selected_agent_summary.agent_name);
  ImGui::SeparatorText("Selected Algorithm");
  ImGui::Text("Target Agent: %s", selected_agent_header.c_str());

  int selected_algorithm_index = agent_composer_ui_state_.selected_algorithm_index;
  if (selected_algorithm_index < 0 ||
      selected_algorithm_index >= static_cast<int>(selected_agent_summary.algorithms.size())) {
    selected_algorithm_index = selected_agent_summary.algorithms.empty() ? -1 : 0;
    agent_composer_ui_state_.selected_algorithm_index = selected_algorithm_index;
  }

  if (!selected_agent_summary.algorithms.empty()) {
    const std::string selected_algorithm_display = selected_algorithm_index >= 0
      ? (selected_agent_summary.algorithms[static_cast<size_t>(selected_algorithm_index)].algorithm_name.empty()
        ? ("<algorithm " + std::to_string(selected_algorithm_index) + ">")
        : selected_agent_summary.algorithms[static_cast<size_t>(selected_algorithm_index)].algorithm_name)
      : "Select algorithm...";
    if (ImGui::BeginCombo("Selected Algorithm", selected_algorithm_display.c_str())) {
      for (size_t i = 0; i < selected_agent_summary.algorithms.size(); ++i) {
        const debug_tool::AlgorithmRuntimeSummary& candidate = selected_agent_summary.algorithms[i];
        const std::string candidate_name = candidate.algorithm_name.empty()
          ? ("<algorithm " + std::to_string(i) + ">")
          : candidate.algorithm_name;
        const bool is_selected = static_cast<int>(i) == selected_algorithm_index;
        if (ImGui::Selectable(candidate_name.c_str(), is_selected)) {
          selected_algorithm_index = static_cast<int>(i);
          agent_composer_ui_state_.selected_algorithm_index = selected_algorithm_index;
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  } else {
    ImGui::TextUnformatted("No algorithms available.");
  }

  const debug_tool::AlgorithmRuntimeSummary* selected_algorithm_summary =
    selected_algorithm_index >= 0
      ? &selected_agent_summary.algorithms[static_cast<size_t>(selected_algorithm_index)]
      : nullptr;
  const std::string selected_algorithm_name = selected_algorithm_summary
    ? (selected_algorithm_summary->algorithm_name.empty()
      ? ("<algorithm " + std::to_string(selected_algorithm_index) + ">")
      : selected_algorithm_summary->algorithm_name)
    : "No algorithm selected";
  ImGui::Text("Current Algorithm: %s", selected_algorithm_name.c_str());
  if (selected_algorithm_summary) {
    const char* assembly_state_text = "failed";
    switch (selected_algorithm_summary->assembly_state) {
      case debug_tool::AlgorithmAssemblyState::Pending: assembly_state_text = "pending"; break;
      case debug_tool::AlgorithmAssemblyState::Assembling: assembly_state_text = "assembling"; break;
      case debug_tool::AlgorithmAssemblyState::Ready: assembly_state_text = "ready"; break;
      case debug_tool::AlgorithmAssemblyState::Failed: assembly_state_text = "failed"; break;
    }
    ImGui::Text("Assembly State: %s", assembly_state_text);
    ImGui::Text("CPU Symbol: %s", selected_algorithm_summary->cpu_symbol ? "true" : "false");
    ImGui::Text("GPU Symbol: %s", selected_algorithm_summary->gpu_symbol ? "true" : "false");

    if (!selected_algorithm_summary->resource_bindings.empty()) {
      ImGui::SeparatorText("Bound Resources");
      for (const debug_tool::AlgorithmResourceBinding& binding : selected_algorithm_summary->resource_bindings) {
        ImGui::BulletText("%s [%s] -> %s",
          binding.resource_name.c_str(),
          binding.resource_kind.c_str(),
          binding.source_path.c_str());
      }
    }
    if (!selected_algorithm_summary->descriptor_values.empty()) {
      ImGui::SeparatorText("Descriptor Values");
      for (const debug_tool::AlgorithmDescriptorValue& value : selected_algorithm_summary->descriptor_values) {
        ImGui::BulletText("%s = %.3f", value.descriptor_name.c_str(), value.scalar_value);
      }
    }

    if (selected_algorithm_summary->reflection_snapshot.valid) {
      ImGui::SeparatorText("Reflection Snapshot");
      ImGui::Text("Variables: %zu", selected_algorithm_summary->reflection_snapshot.variables.size());
      for (const debug_tool::AlgorithmReflectionValue& value : selected_algorithm_summary->reflection_snapshot.variables) {
        if (value.bytes.size() >= sizeof(float) * 3u) {
          float xyz[3]{};
          std::memcpy(xyz, value.bytes.data(), sizeof(xyz));
          ImGui::BulletText(
            "%s -> %s: (%.3f, %.3f, %.3f)",
            value.reflection_object_name.c_str(),
            value.container_name.c_str(),
            xyz[0],
            xyz[1],
            xyz[2]);
        } else {
          ImGui::BulletText(
            "%s -> %s: %zu bytes",
            value.reflection_object_name.c_str(),
            value.container_name.c_str(),
            value.bytes.size());
        }
      }
      ImGui::Text("Variable Arrays: %zu", selected_algorithm_summary->reflection_snapshot.variable_arrays.size());
      for (const debug_tool::AlgorithmReflectionValue& value : selected_algorithm_summary->reflection_snapshot.variable_arrays) {
        if (value.bytes.size() >= sizeof(float) * 3u) {
          float xyz[3]{};
          std::memcpy(xyz, value.bytes.data(), sizeof(xyz));
          ImGui::BulletText(
            "%s -> %s: (%.3f, %.3f, %.3f)",
            value.reflection_object_name.c_str(),
            value.container_name.c_str(),
            xyz[0],
            xyz[1],
            xyz[2]);
        } else {
          ImGui::BulletText(
            "%s -> %s: %zu bytes",
            value.reflection_object_name.c_str(),
            value.container_name.c_str(),
            value.bytes.size());
        }
      }
    }
  } else {
    ImGui::TextUnformatted("Select an algorithm in Agent Manager.");
  }

  SyncCustomInterventionUiState(host);
  for (ActiveCustomInterventionUiSlot& slot : active_custom_ui_slots_) {
    if (slot.agent_index != static_cast<size_t>(agent_composer_ui_state_.selected_agent_index) ||
        slot.algorithm_index != static_cast<size_t>(selected_algorithm_index)) {
      continue;
    }
    if (!selected_algorithm_summary || !slot.hook) {
      continue;
    }
    ImGui::Spacing();
    const char* custom_title = slot.hook->title();
    ImGui::SeparatorText(custom_title ? custom_title : "Custom Intervention UI");
    DrawCustomInterventionUi(host, slot.agent_index, slot.algorithm_index, selected_agent_summary, *selected_algorithm_summary, &slot);
  }

  ImGui::SeparatorText("Mount New Algorithm");
  ImGui::BeginDisabled(selected_algorithm_summary && selected_algorithm_index >= 0);
  if (!agent_composer_ui_state_.algorithm_catalog_entries.empty()) {
    const std::string current_display = (agent_composer_ui_state_.selected_algorithm_catalog_index >= 0 &&
      agent_composer_ui_state_.selected_algorithm_catalog_index <
        static_cast<int>(agent_composer_ui_state_.algorithm_catalog_entries.size()))
      ? agent_composer_ui_state_.algorithm_catalog_entries[static_cast<size_t>(agent_composer_ui_state_.selected_algorithm_catalog_index)].display_name
      : "Select algorithm...";
    if (ImGui::BeginCombo("Algorithm Catalog", current_display.c_str())) {
      for (size_t i = 0; i < agent_composer_ui_state_.algorithm_catalog_entries.size(); ++i) {
        const auto& entry = agent_composer_ui_state_.algorithm_catalog_entries[i];
        const bool is_selected = static_cast<int>(i) == agent_composer_ui_state_.selected_algorithm_catalog_index;
        if (ImGui::Selectable(entry.display_name.c_str(), is_selected)) {
          agent_composer_ui_state_.selected_algorithm_catalog_index = static_cast<int>(i);
          _SetTextBuffer(&agent_composer_ui_state_.algorithm_name, entry.algorithm_name);
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  } else if (!agent_composer_ui_state_.algorithm_catalog_error.empty()) {
    ImGui::TextWrapped("%s", agent_composer_ui_state_.algorithm_catalog_error.c_str());
  }

  ImGui::InputText(
    "Algorithm Name",
    agent_composer_ui_state_.algorithm_name.data(),
    agent_composer_ui_state_.algorithm_name.size());
  if (!agent_composer_ui_state_.reflection_error.empty()) {
    ImGui::TextWrapped("%s", agent_composer_ui_state_.reflection_error.c_str());
  }

  ImGui::SeparatorText("Execution Mode");
  const char* execution_mode_text = "GPU";
  switch (agent_composer_ui_state_.execution_preference) {
    case debug_tool::AlgorithmExecutionPreference::Cpu: execution_mode_text = "CPU"; break;
    case debug_tool::AlgorithmExecutionPreference::Gpu: execution_mode_text = "GPU"; break;
  }
  if (ImGui::BeginCombo("Backend", execution_mode_text)) {
    const struct Option {
      const char* label;
      debug_tool::AlgorithmExecutionPreference value;
    } options[] = {
      {"CPU", debug_tool::AlgorithmExecutionPreference::Cpu},
      {"GPU", debug_tool::AlgorithmExecutionPreference::Gpu},
    };
    for (const Option& option : options) {
      const bool is_selected = agent_composer_ui_state_.execution_preference == option.value;
      if (ImGui::Selectable(option.label, is_selected)) {
        agent_composer_ui_state_.execution_preference = option.value;
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  const std::string algorithm_name = _TrimCopy(agent_composer_ui_state_.algorithm_name.data());
  if (algorithm_name.empty()) {
    agent_composer_ui_state_.selected_algorithm_catalog_index = -1;
    RefreshAlgorithmComposerBindings(host, algorithm_name);
  } else {
    const int catalog_index = _FindAlgorithmCatalogIndex(agent_composer_ui_state_.algorithm_catalog_entries, algorithm_name);
    agent_composer_ui_state_.selected_algorithm_catalog_index = catalog_index;
    if (algorithm_name != agent_composer_ui_state_.reflected_algorithm_name) {
      RefreshAlgorithmComposerBindings(host, algorithm_name);
    }
  }

  ImGui::SeparatorText("Requested Resources");
  if (agent_composer_ui_state_.resource_inputs.empty()) {
    ImGui::TextUnformatted("No requested resources exposed by the algorithm package.");
  } else {
    const auto& dropped_files = host.input().dropped_files;
    std::vector<bool> drop_consumed(dropped_files.size(), false);
    for (size_t i = 0; i < agent_composer_ui_state_.resource_inputs.size(); ++i) {
      auto& resource = agent_composer_ui_state_.resource_inputs[i];
      ImGui::PushID(static_cast<int>(i));
      const std::string label = resource.resource_name.empty()
        ? ("Resource " + std::to_string(i))
        : resource.resource_name;
      ImGui::Text("%s%s%s",
        label.c_str(),
        resource.resource_kind.empty() ? "" : " [",
        resource.resource_kind.empty() ? "" : resource.resource_kind.c_str());
      if (!resource.resource_kind.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("]");
      }
      ImGui::SetNextItemWidth(-1.0f);
      const std::string path_label = "##resource_path_" + std::to_string(i);
      ImGui::InputText(path_label.c_str(), resource.resource_path.data(), resource.resource_path.size());
      const ImVec2 path_min = ImGui::GetItemRectMin();
      const ImVec2 path_max = ImGui::GetItemRectMax();
      for (size_t drop_index = 0; drop_index < dropped_files.size(); ++drop_index) {
        if (drop_consumed[drop_index]) {
          continue;
        }
        const auto& dropped_file = dropped_files[drop_index];
        if (dropped_file.x < static_cast<int>(path_min.x) || dropped_file.x > static_cast<int>(path_max.x) ||
            dropped_file.y < static_cast<int>(path_min.y) || dropped_file.y > static_cast<int>(path_max.y)) {
          continue;
        }
        _SetTextBuffer(&resource.resource_path, dropped_file.path);
        drop_consumed[drop_index] = true;
        break;
      }
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
          if (payload->Data && payload->DataSize > 0) {
            _SetTextBuffer(&resource.resource_path, static_cast<const char*>(payload->Data));
          }
        } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FOLDER_PATH")) {
          if (payload->Data && payload->DataSize > 0) {
            _SetTextBuffer(&resource.resource_path, static_cast<const char*>(payload->Data));
          }
        }
        ImGui::EndDragDropTarget();
      }
      if (ImGui::BeginDragDropSource()) {
        const std::string current_path = _TrimCopy(resource.resource_path.data());
        if (!current_path.empty()) {
          const size_t source_index = i;
          ImGui::SetDragDropPayload("RESOURCE_SLOT_INDEX", &source_index, sizeof(source_index));
          ImGui::TextUnformatted(current_path.c_str());
        }
        ImGui::EndDragDropSource();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) {
        resource.resource_path.fill('\0');
      }
      if (!resource.required) {
        ImGui::TextUnformatted("Optional resource.");
      }
      ImGui::PopID();
    }
  }

  ImGui::SeparatorText("Requested Descriptors");
  if (agent_composer_ui_state_.descriptor_inputs.empty()) {
    ImGui::TextUnformatted("No requested descriptors exposed by the algorithm package.");
  } else {
    for (size_t i = 0; i < agent_composer_ui_state_.descriptor_inputs.size(); ++i) {
      auto& descriptor = agent_composer_ui_state_.descriptor_inputs[i];
      ImGui::PushID(static_cast<int>(1000 + i));
      const std::string label = descriptor.descriptor_name.empty()
        ? ("Descriptor " + std::to_string(i))
        : descriptor.descriptor_name;
      ImGui::Text("%s -> %s[%u]",
        label.c_str(),
        descriptor.container_name.empty() ? "<container>" : descriptor.container_name.c_str(),
        descriptor.array_index);
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputFloat("Value", &descriptor.scalar_value, 0.1f, 1.0f, "%.3f");
      ImGui::PopID();
    }
  }
  ImGui::EndDisabled();

  runtime_systems::RenderPreviewRequest preview_request{};
  if (selected_algorithm_summary) {
    const size_t preview_algorithm_index = selected_algorithm_index >= 0
      ? static_cast<size_t>(selected_algorithm_index)
      : 0u;
    std::string preview_error_message;
    if (host.BuildRenderPreviewRequest(
          0u,
          preview_algorithm_index,
          &preview_request,
          &preview_error_message) &&
        preview_request.valid) {
      host.SetRenderPreviewRequest(std::move(preview_request));
    } else {
      host.ui_status_message() = preview_error_message.empty()
        ? "Selected algorithm failed preview validation."
        : std::move(preview_error_message);
#ifndef NDEBUG
      DEBUG_TOOL_ASSERT(false, "Selected algorithm failed preview validation.");
#endif
    }
  } else {
    host.SetRenderPreviewRequest({});
  }

  ImGui::SeparatorText("Start And Reset");
  const bool algorithm_running = selected_algorithm_summary && selected_algorithm_index >= 0;
  const char* action_label = algorithm_running ? "Reset Algorithm" : "Start Algorithm";
  if (ImGui::Button(action_label, ImVec2(180.0f, 0.0f))) {
    auto build_and_apply_preview_for_algorithm = [&](size_t algorithm_index, std::string* out_error_message) -> bool {
      runtime_systems::RenderPreviewRequest mounted_preview_request{};
      std::string mounted_preview_error_message;
      if (!host.BuildRenderPreviewRequest(
            0u,
            algorithm_index,
            &mounted_preview_request,
            &mounted_preview_error_message)) {
        DEBUG_TOOL_ASSERT(false, "Submitted algorithm did not produce a drawable preview.");
        if (out_error_message) {
          *out_error_message = mounted_preview_error_message.empty()
            ? "Submitted algorithm did not produce a drawable preview."
            : std::move(mounted_preview_error_message);
        }
        return false;
      }
      if (!mounted_preview_request.valid) {
        DEBUG_TOOL_ASSERT(false, "Submitted algorithm did not produce a drawable preview.");
        if (out_error_message) {
          *out_error_message = mounted_preview_error_message.empty()
            ? "Submitted algorithm did not produce a drawable preview."
            : std::move(mounted_preview_error_message);
        }
        return false;
      }
      if (out_error_message) {
        out_error_message->clear();
      }
      host.SetRenderPreviewRequest(std::move(mounted_preview_request));
      return true;
    };

    if (!algorithm_running) {
      if (algorithm_name.empty()) {
        host.ui_status_message() = "Algorithm name must not be empty.";
      } else {
        std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings;
        for (const auto& resource : agent_composer_ui_state_.resource_inputs) {
          resource_bindings.push_back(debug_tool::AlgorithmResourceBinding{
            .resource_name = resource.resource_name,
            .resource_kind = resource.resource_kind,
            .source_path = _TrimCopy(resource.resource_path.data()),
          });
        }
        std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values;
        for (const auto& descriptor : agent_composer_ui_state_.descriptor_inputs) {
          descriptor_values.push_back(debug_tool::AlgorithmDescriptorValue{
            .descriptor_name = descriptor.descriptor_name,
            .scalar_value = descriptor.scalar_value,
          });
        }

        size_t attached_algorithm_index = 0u;
        std::string attach_error_message;
        if (!host.AttachAlgorithmToAgent(
              0u,
              algorithm_name,
              resource_bindings,
              descriptor_values,
              &attached_algorithm_index,
              &attach_error_message,
              debug_tool::AlgorithmMountMode::Direct,
              agent_composer_ui_state_.execution_preference)) {
          DEBUG_TOOL_ASSERT(false, "Failed to attach algorithm to the built-in agent.");
          host.ui_status_message() = attach_error_message.empty()
            ? "Failed to start algorithm on built-in agent."
            : ("Failed to start algorithm on built-in agent: " + attach_error_message);
        } else {
          agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(attached_algorithm_index);
          std::string preview_error_message;
          if (!build_and_apply_preview_for_algorithm(attached_algorithm_index, &preview_error_message)) {
            DEBUG_TOOL_ASSERT(false, "Submitted algorithm failed preview validation.");
            host.ui_status_message() = preview_error_message.empty()
              ? "Algorithm started, but no drawable result was produced."
              : std::move(preview_error_message);
          } else {
            host.StartTicking();
            host.ui_status_message() = "Algorithm started on built-in agent.";
          }
        }
      }
    } else {
      const std::string reset_algorithm_name = selected_algorithm_summary->algorithm_name;
      const std::vector<debug_tool::AlgorithmResourceBinding> reset_resource_bindings = selected_algorithm_summary->resource_bindings;
      const std::vector<debug_tool::AlgorithmDescriptorValue> reset_descriptor_values = selected_algorithm_summary->descriptor_values;
      const debug_tool::AlgorithmMountMode reset_mount_mode = selected_algorithm_summary->mount_mode;
      const debug_tool::AlgorithmExecutionPreference reset_execution_preference = selected_algorithm_summary->execution_preference;
      std::string reset_error_message;
      if (!host.DetachAlgorithmFromAgent(
            0u,
            static_cast<size_t>(selected_algorithm_index),
            &reset_error_message)) {
        host.ui_status_message() = reset_error_message.empty()
          ? "Failed to unload the selected algorithm."
          : std::move(reset_error_message);
      } else {
        host.ClearGpuExecutors();
        size_t reset_attached_algorithm_index = 0u;
        if (!host.AttachAlgorithmToAgent(
              0u,
              reset_algorithm_name,
              reset_resource_bindings,
              reset_descriptor_values,
              &reset_attached_algorithm_index,
              &reset_error_message,
              reset_mount_mode,
              reset_execution_preference)) {
          DEBUG_TOOL_ASSERT(false, "Failed to reattach the selected algorithm.");
          host.ui_status_message() = reset_error_message.empty()
            ? "Failed to reattach the selected algorithm."
            : ("Failed to reattach the selected algorithm: " + reset_error_message);
        } else {
          agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(reset_attached_algorithm_index);
          std::string preview_error_message;
          if (!build_and_apply_preview_for_algorithm(reset_attached_algorithm_index, &preview_error_message)) {
            DEBUG_TOOL_ASSERT(false, "Reset algorithm failed preview validation.");
            host.ui_status_message() = preview_error_message.empty()
              ? "Selected algorithm was reset, but no drawable result was produced."
              : std::move(preview_error_message);
          } else {
            host.StartTicking();
            host.ui_status_message() = "Selected algorithm was reset.";
          }
        }
      }
    }
  }

  ImGui::End();
}

void DebugToolFrontendPanel::DrawWindowMenu() {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }

  if (ImGui::BeginMenu("Window")) {
    if (ImGui::MenuItem("Show All")) {
      show_agent_manager_window_ = true;
      show_agent_detail_window_ = true;
      show_file_browser_window_ = true;
      show_render_preview_window_ = true;
      show_runtime_status_window_ = true;
    }
    if (ImGui::MenuItem("Hide All")) {
      show_agent_manager_window_ = false;
      show_agent_detail_window_ = false;
      show_file_browser_window_ = false;
      show_render_preview_window_ = false;
      show_runtime_status_window_ = false;
    }
    ImGui::Separator();
    ImGui::MenuItem("Agent Manager", nullptr, &show_agent_manager_window_);
    ImGui::MenuItem("Algorithm Detail", nullptr, &show_agent_detail_window_);
    ImGui::MenuItem("Files", nullptr, &show_file_browser_window_);
    ImGui::MenuItem("Render Preview", nullptr, &show_render_preview_window_);
    ImGui::MenuItem("Runtime Status", nullptr, &show_runtime_status_window_);
    ImGui::EndMenu();
  }

  ImGui::EndMainMenuBar();
}

void DebugToolFrontendPanel::DrawAlgorithmPreviewUi(IDebugToolHost& host) {
  _PopWindowToViewportOnAppear();
  ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Render Preview", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return;
  }

  const ImVec2 preview_size = ImGui::GetContentRegionAvail();
  if (preview_size.x > 0.0f && preview_size.y > 0.0f) {
    host.SetRenderPreviewExtent(preview_size);
  }
  if (host.has_render_preview_texture()) {
    ImGui::Image(host.render_preview_texture_id(), preview_size);
  } else {
    ImGui::TextUnformatted("Render preview is not ready.");
    ImGui::TextUnformatted("Load an algorithm with a drawable intervention package.");
  }
  ImGui::End();
}

void DebugToolFrontendPanel::DrawDebugToolFrontend(IDebugToolHost& host) {
  DrawWindowMenu();

  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  }

  if (show_agent_manager_window_) {
    DrawAgentManagerUi(host);
  }
  if (show_agent_detail_window_) {
    DrawAgentDetailUi(host);
  }
  if (show_file_browser_window_) {
    DrawFileBrowserUi(host);
  }
  if (show_render_preview_window_) {
    DrawAlgorithmPreviewUi(host);
  }

  if (show_runtime_status_window_) {
    if (!ImGui::Begin("Runtime Status")) {
      ImGui::End();
      return;
    }
    ImGui::Text("Status: %s", host.ui_status_message().empty() ? "ready" : host.ui_status_message().c_str());
    ImGui::Text("Managed agents: %zu", host.agent_count());
    ImGui::TextWrapped("%s", host.render_preview_debug_summary().c_str());
    ImGui::Text(
      "Algorithm signal: %s",
      host.combined_algorithm_to_agent_signal().pause_requested ? "pause requested" : "idle");
    ImGui::End();
  }
}

void DebugToolFrontendPanel::Draw(IDebugToolHost& host) {
  DrawDebugToolFrontend(host);
}

void DebugToolFrontendPanel::Destroy() {
  agent_composer_ui_state_ = {};
  agent_composer_defaults_initialized_ = false;
  show_agent_manager_window_ = true;
  show_agent_detail_window_ = true;
  show_file_browser_window_ = true;
  show_render_preview_window_ = true;
  show_runtime_status_window_ = true;
  managed_agent_ui_bindings_.clear();
  active_custom_ui_slots_.clear();
}

}  // namespace debug_tool_frontend
