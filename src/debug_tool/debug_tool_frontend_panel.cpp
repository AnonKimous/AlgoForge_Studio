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
#include <unordered_set>

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

std::string _FormatAlgorithmRuntimeLabel(
  const debug_tool::AlgorithmRuntimeSummary& algorithm_summary,
  size_t fallback_index,
  bool show_pipeline_stage_detail) {
  const std::string algorithm_name = algorithm_summary.algorithm_name.empty()
    ? ("<algorithm " + std::to_string(fallback_index) + ">")
    : algorithm_summary.algorithm_name;
  if (!show_pipeline_stage_detail &&
      algorithm_summary.pipeline_stage &&
      !algorithm_summary.pipeline_name.empty()) {
    return algorithm_summary.pipeline_name +
      " (" + std::string(debug_tool::AlgorithmPipelineTopologyDisplayName(
        algorithm_summary.pipeline_topology)) + ", " +
      debug_tool::AlgorithmPipelineSyncModeDisplayName(algorithm_summary.pipeline_sync_mode) + ")";
  }
  if (!algorithm_summary.pipeline_stage || algorithm_summary.pipeline_name.empty()) {
    return algorithm_name;
  }
  return algorithm_name +
    " [" + algorithm_summary.pipeline_name +
    " " + std::string(debug_tool::AlgorithmPipelineTopologyDisplayName(
      algorithm_summary.pipeline_topology)) +
    " " + debug_tool::AlgorithmPipelineSyncModeDisplayName(algorithm_summary.pipeline_sync_mode) +
    " stage " + std::to_string(algorithm_summary.pipeline_stage_index) + "]";
}

const char* _AlgorithmInterventionStageKindDisplayName(
  algorithm_management::AlgorithmInterventionStageKind stage_kind) {
  switch (stage_kind) {
    case algorithm_management::AlgorithmInterventionStageKind::Pretick: return "pretick";
    case algorithm_management::AlgorithmInterventionStageKind::Exec: return "exec";
    case algorithm_management::AlgorithmInterventionStageKind::AfterTick: return "aftertick";
    case algorithm_management::AlgorithmInterventionStageKind::RenderResult: return "renderresult";
    case algorithm_management::AlgorithmInterventionStageKind::Reflect: return "reflect";
    case algorithm_management::AlgorithmInterventionStageKind::Custom: return "custom";
  }
  return "custom";
}

const char* _AlgorithmExecutionPreferenceDisplayName(
  algorithm_management::AlgorithmExecutionPreference execution_preference) {
  switch (execution_preference) {
    case algorithm_management::AlgorithmExecutionPreference::Cpu: return "cpu";
    case algorithm_management::AlgorithmExecutionPreference::Gpu: return "gpu";
  }
  return "cpu";
}

void _DrawReflectionSnapshot(
  const char* title,
  const debug_tool::AlgorithmReflectionSnapshot& snapshot) {
  if (!title) {
    title = "Reflection Snapshot";
  }
  ImGui::SeparatorText(title);
  ImGui::BeginChild(title, ImVec2(0.0f, 120.0f), true);
  if (!snapshot.valid) {
    ImGui::TextUnformatted("No reflection snapshot available.");
    ImGui::EndChild();
    return;
  }
  ImGui::Text("Variables: %zu", snapshot.variables.size());
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variables) {
    if (value.bytes.size() >= sizeof(float) * 3u) {
      float xyz[3]{};
      std::memcpy(xyz, value.bytes.data(), sizeof(xyz));
      ImGui::BulletText(
        "%s -> %s [%s]: (%.3f, %.3f, %.3f)",
        value.reflection_object_name.c_str(),
        value.container_name.c_str(),
        value.storage_kind.c_str(),
        xyz[0],
        xyz[1],
        xyz[2]);
    } else {
      ImGui::BulletText(
        "%s -> %s [%s]: %zu bytes",
        value.reflection_object_name.c_str(),
        value.container_name.c_str(),
        value.storage_kind.c_str(),
        value.bytes.size());
    }
  }

  ImGui::Text("Variable Arrays: %zu", snapshot.variable_arrays.size());
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    if (value.bytes.size() >= sizeof(float) * 3u) {
      float xyz[3]{};
      std::memcpy(xyz, value.bytes.data(), sizeof(xyz));
      ImGui::BulletText(
        "%s -> %s [%s]: (%.3f, %.3f, %.3f)",
        value.reflection_object_name.c_str(),
        value.container_name.c_str(),
        value.storage_kind.c_str(),
        xyz[0],
        xyz[1],
        xyz[2]);
    } else {
      ImGui::BulletText(
        "%s -> %s [%s]: %zu bytes",
        value.reflection_object_name.c_str(),
        value.container_name.c_str(),
        value.storage_kind.c_str(),
        value.bytes.size());
    }
  }
  ImGui::EndChild();
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
  agent_composer_ui_state_.preview_request_dirty = true;
  agent_composer_ui_state_.pipeline_run_from_stage0_to_end = true;
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
    agent_composer_ui_state_.preview_request_dirty = true;
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
    agent_composer_ui_state_.preview_request_dirty = true;
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
    agent_composer_ui_state_.descriptor_inputs[i].scalar_value = 0.0;
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
    agent_composer_ui_state_.preview_request_dirty = true;
    return false;
  }
  if (!has_default_file) {
    agent_composer_ui_state_.preview_request_dirty = true;
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
      agent_composer_ui_state_.preview_request_dirty = true;
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
      agent_composer_ui_state_.preview_request_dirty = true;
      return false;
    }
    agent_composer_ui_state_.descriptor_inputs[found->second].scalar_value = default_value.scalar_value;
  }

  agent_composer_ui_state_.preview_request_dirty = true;
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

  ImGui::SeparatorText("Mount New Algorithm");
  bool algorithm_name_dirty = false;
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
          algorithm_name_dirty = true;
          agent_composer_ui_state_.preview_request_dirty = true;
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

  ImGui::TextUnformatted("Use the Files page to browse folders and drag paths into resource slots.");

  if (ImGui::InputText(
    "Algorithm Name",
    agent_composer_ui_state_.algorithm_name.data(),
    agent_composer_ui_state_.algorithm_name.size())) {
    algorithm_name_dirty = true;
  }
  const std::string algorithm_name = _TrimCopy(agent_composer_ui_state_.algorithm_name.data());
  if (algorithm_name.empty()) {
    agent_composer_ui_state_.selected_algorithm_catalog_index = -1;
  } else {
    const int catalog_index = _FindAlgorithmCatalogIndex(agent_composer_ui_state_.algorithm_catalog_entries, algorithm_name);
    agent_composer_ui_state_.selected_algorithm_catalog_index = catalog_index;
  }
  if (algorithm_name_dirty) {
    RefreshAlgorithmComposerBindings(host, algorithm_name);
  }
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
      if (ImGui::InputText(path_label.c_str(), resource.resource_path.data(), resource.resource_path.size())) {
        agent_composer_ui_state_.preview_request_dirty = true;
      }
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
        agent_composer_ui_state_.preview_request_dirty = true;
        drop_consumed[drop_index] = true;
        break;
      }
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_PATH")) {
          if (payload->Data && payload->DataSize > 0) {
            _SetTextBuffer(&resource.resource_path, static_cast<const char*>(payload->Data));
            agent_composer_ui_state_.preview_request_dirty = true;
          }
        } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FOLDER_PATH")) {
          if (payload->Data && payload->DataSize > 0) {
            _SetTextBuffer(&resource.resource_path, static_cast<const char*>(payload->Data));
            agent_composer_ui_state_.preview_request_dirty = true;
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
        agent_composer_ui_state_.preview_request_dirty = true;
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
      ImGui::InputDouble("Value", &descriptor.scalar_value, 0.1, 1.0, "%.6f");
      ImGui::PopID();
    }
  }

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
    std::unordered_set<std::string> shown_pipeline_names{};
    for (size_t algorithm_index = 0; algorithm_index < agent_summary.algorithms.size(); ++algorithm_index) {
      const debug_tool::AlgorithmRuntimeSummary& algorithm_summary = agent_summary.algorithms[algorithm_index];
      if (algorithm_summary.algorithm_name.empty()) {
        continue;
      }
      if (algorithm_summary.pipeline_stage && !algorithm_summary.pipeline_name.empty()) {
        if (!shown_pipeline_names.insert(algorithm_summary.pipeline_name).second) {
          continue;
        }
      }
      const bool show_pipeline_stage_detail = !algorithm_summary.pipeline_stage;
      const std::string algorithm_name = _FormatAlgorithmRuntimeLabel(
        algorithm_summary,
        algorithm_index,
        show_pipeline_stage_detail);
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

  DrawAgentComposerUi(host);
  ImGui::Separator();

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
        std::unordered_set<std::string> shown_pipeline_names{};
        for (size_t algorithm_index = 0; algorithm_index < agent_summary.algorithms.size(); ++algorithm_index) {
          const debug_tool::AlgorithmRuntimeSummary& algorithm_summary = agent_summary.algorithms[algorithm_index];
          if (algorithm_summary.algorithm_name.empty()) {
            continue;
          }
          if (algorithm_summary.pipeline_stage && !algorithm_summary.pipeline_name.empty()) {
            if (!shown_pipeline_names.insert(algorithm_summary.pipeline_name).second) {
              continue;
            }
          }
          const bool show_pipeline_stage_detail = !algorithm_summary.pipeline_stage;
          const std::string algorithm_name =
            _FormatAlgorithmRuntimeLabel(
              algorithm_summary,
              algorithm_index,
              show_pipeline_stage_detail);
          const bool algorithm_selected =
            agent_index == 0u &&
            static_cast<int>(algorithm_index) == agent_composer_ui_state_.selected_algorithm_index;
          if (ImGui::Selectable(algorithm_name.c_str(), algorithm_selected)) {
            agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(algorithm_index);
            agent_composer_ui_state_.preview_request_dirty = true;
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

  debug_tool::AgentRuntimeSummary selected_agent_summary{};
  if (!host.GetAgentSummary(0u, &selected_agent_summary)) {
    ImGui::TextUnformatted("Built-in debug agent is unavailable.");
    ImGui::End();
    return;
  }

  const std::string selected_agent_header = selected_agent_summary.agent_name.empty()
    ? "Debug Agent"
    : ("Debug Agent  " + selected_agent_summary.agent_name);
  ImGui::Text("Target Agent: %s", selected_agent_header.c_str());

  int selected_algorithm_index = agent_composer_ui_state_.selected_algorithm_index;
  if (selected_algorithm_index < 0 ||
      selected_algorithm_index >= static_cast<int>(selected_agent_summary.algorithms.size())) {
    selected_algorithm_index = selected_agent_summary.algorithms.empty() ? -1 : 0;
    agent_composer_ui_state_.selected_algorithm_index = selected_algorithm_index;
    agent_composer_ui_state_.preview_request_dirty = true;
  }

  const debug_tool::AlgorithmRuntimeSummary* selected_algorithm_summary =
    selected_algorithm_index >= 0
      ? &selected_agent_summary.algorithms[static_cast<size_t>(selected_algorithm_index)]
      : nullptr;
  if (selected_algorithm_summary &&
      selected_algorithm_summary->pipeline_stage &&
      !selected_algorithm_summary->pipeline_name.empty()) {
    ImGui::SeparatorText("Pipeline Stage Tools");
    ImGui::TextUnformatted(
      "Single-stage debug should mount the selected stage as a normal algorithm, not rewrite the pipeline internals.");
    if (ImGui::Button("Mount This Stage As Algorithm", ImVec2(260.0f, 0.0f))) {
      std::string attach_error_message;
      size_t attached_algorithm_index = 0u;
      host.SetRenderPreviewRequest({});
      host.PauseTicking();
      if (!host.AttachAlgorithmToAgent(
            0u,
            selected_algorithm_summary->algorithm_name,
            selected_algorithm_summary->resource_bindings,
            selected_algorithm_summary->descriptor_values,
            &attached_algorithm_index,
            &attach_error_message,
            debug_tool::AlgorithmMountMode::Direct,
            selected_algorithm_summary->execution_preference)) {
        DEBUG_TOOL_ASSERT(false, "Failed to mount the selected pipeline stage as a normal algorithm.");
        host.ui_status_message() = attach_error_message.empty()
          ? "Failed to mount the selected pipeline stage as a normal algorithm."
          : attach_error_message;
      } else {
        agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(attached_algorithm_index);
        agent_composer_ui_state_.preview_request_dirty = true;
        host.ClearGpuRuntimeCaches();
        host.ui_status_message() =
          "Mounted the selected stage as a normal algorithm. Use this mount for single-stage debug.";
      }
    }
  }

  const std::string selected_algorithm_name = selected_algorithm_summary
    ? (selected_algorithm_summary->pipeline_name.empty()
        ? selected_algorithm_summary->algorithm_name
        : selected_algorithm_summary->pipeline_name)
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
    const char* active_bundle_preference_text = "gpu";
    switch (selected_algorithm_summary->pipeline_active_bundle_preference) {
      case debug_tool::AlgorithmExecutionPreference::Cpu: active_bundle_preference_text = "cpu"; break;
      case debug_tool::AlgorithmExecutionPreference::Gpu: active_bundle_preference_text = "gpu"; break;
    }
    if (selected_algorithm_summary->pipeline_active_bundle_valid) {
      ImGui::Text(
        "Active Bundle: begin=%u count=%u pref=%s",
        selected_algorithm_summary->pipeline_active_bundle_begin_stage_index,
        selected_algorithm_summary->pipeline_active_bundle_stage_count,
        active_bundle_preference_text);
    } else {
      ImGui::TextUnformatted("Active Bundle: invalid");
    }
    ImGui::BeginChild("AlgorithmDetailStats", ImVec2(0.0f, 220.0f), true);

    if (!selected_algorithm_summary->pipeline_stage &&
        (selected_algorithm_summary->pipeline_total_elapsed_seconds > 0.0f ||
         !selected_algorithm_summary->pipeline_stage_runtime_stats.empty())) {
      ImGui::SeparatorText("流水线耗时");
      ImGui::Text(
        "拓扑: %s",
        debug_tool::AlgorithmPipelineTopologyDisplayName(
          selected_algorithm_summary->pipeline_topology));
      ImGui::Text(
        "同步模式: %s",
        debug_tool::AlgorithmPipelineSyncModeDisplayName(
          selected_algorithm_summary->pipeline_sync_mode));
      ImGui::Text(
        "总耗时: %.3f s",
        selected_algorithm_summary->pipeline_total_elapsed_seconds);
      if (!selected_algorithm_summary->pipeline_stage_runtime_stats.empty()) {
        for (const algorithm_management::AlgorithmPipelineStageRuntimeStat& stage_stat :
             selected_algorithm_summary->pipeline_stage_runtime_stats) {
          if (!stage_stat.reason.empty()) {
            ImGui::BulletText(
              "%s: %.3f s (%s)",
              stage_stat.stage_name.c_str(),
              stage_stat.elapsed_seconds,
              stage_stat.reason.c_str());
          } else {
            ImGui::BulletText(
              "%s: %.3f s",
              stage_stat.stage_name.c_str(),
              stage_stat.elapsed_seconds);
          }
        }
      }
    }
    ImGui::EndChild();

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

    ImGui::SeparatorText("Reflector");
    ImGui::Text("Reflector: %s", selected_algorithm_summary->has_reflector ? "present" : "absent");
    _DrawReflectionSnapshot("Reflection Snapshot", selected_algorithm_summary->reflection_snapshot);

    ImGui::SeparatorText("Intervention");
    ImGui::Text("Intervention: %s", selected_algorithm_summary->has_intervention ? "present" : "absent");
    if (selected_algorithm_summary->intervention_stage_summaries.empty()) {
      ImGui::TextUnformatted("No intervention stages available.");
    } else {
      for (const debug_tool::AlgorithmInterventionStageSummary& stage_summary :
           selected_algorithm_summary->intervention_stage_summaries) {
        ImGui::BulletText(
          "%s [%s, %s]",
          stage_summary.stage_name.empty() ? "<stage>" : stage_summary.stage_name.c_str(),
          _AlgorithmInterventionStageKindDisplayName(stage_summary.stage_kind),
          _AlgorithmExecutionPreferenceDisplayName(stage_summary.execution_preference));
        if (!stage_summary.functions.empty()) {
          ImGui::Indent();
          ImGui::TextUnformatted("Functions:");
          for (const std::string& function_name : stage_summary.functions) {
            ImGui::BulletText("%s", function_name.c_str());
          }
          ImGui::Unindent();
        }
        if (!stage_summary.used_algorithm_containers.empty()) {
          ImGui::Indent();
          ImGui::TextUnformatted("Containers:");
          for (const algorithm_management::AlgorithmInterventionContainerBinding& binding :
               stage_summary.used_algorithm_containers) {
            ImGui::BulletText(
              "%s [%s]%s",
              binding.container_name.c_str(),
              binding.container_kind.c_str(),
              binding.required ? "" : " (optional)");
          }
          ImGui::Unindent();
        }
        if (!stage_summary.vertex_shader_path.empty() || !stage_summary.fragment_shader_path.empty()) {
          ImGui::Indent();
          ImGui::Text(
            "Shaders: %s | %s",
            stage_summary.vertex_shader_path.empty() ? "<none>" : stage_summary.vertex_shader_path.c_str(),
            stage_summary.fragment_shader_path.empty() ? "<none>" : stage_summary.fragment_shader_path.c_str());
          if (!stage_summary.pipeline_kind.empty()) {
            ImGui::Text("Pipeline: %s", stage_summary.pipeline_kind.c_str());
          }
          ImGui::Unindent();
        }
      }
    }

    if (!selected_algorithm_summary->pipeline_stage &&
        selected_algorithm_summary->bridge_debug_set.valid) {
      ImGui::SeparatorText("Logical Stage Debug");
      ImGui::Text(
        "Bridge: %s <- %s -> %s",
        selected_algorithm_summary->bridge_debug_set.stage_name.empty()
          ? "<stage>"
          : selected_algorithm_summary->bridge_debug_set.stage_name.c_str(),
        selected_algorithm_summary->bridge_debug_set.previous_stage_name.empty()
          ? "<head>"
          : selected_algorithm_summary->bridge_debug_set.previous_stage_name.c_str(),
        selected_algorithm_summary->bridge_debug_set.next_stage_name.empty()
          ? "<tail>"
          : selected_algorithm_summary->bridge_debug_set.next_stage_name.c_str());
      ImGui::TextUnformatted(
        "Single-step debug folds bridge ingress into a logical decomposer view and folds bridge egress into a logical reflector view.");

      if (selected_algorithm_summary->bridge_debug_set.has_logical_decomposer_snapshot) {
        _DrawReflectionSnapshot(
          "Logical Decomposer Output",
          selected_algorithm_summary->bridge_debug_set.logical_decomposer_snapshot);
      }
      if (selected_algorithm_summary->bridge_debug_set.has_stage_runtime_snapshot) {
        _DrawReflectionSnapshot(
          "Stage Runtime Output",
          selected_algorithm_summary->bridge_debug_set.stage_runtime_snapshot);
      }
      if (selected_algorithm_summary->bridge_debug_set.has_logical_reflector_snapshot) {
        _DrawReflectionSnapshot(
          "Logical Reflector Output",
          selected_algorithm_summary->bridge_debug_set.logical_reflector_snapshot);
      }
      if (selected_algorithm_summary->bridge_debug_set.has_logical_replay_reflector_snapshot) {
        _DrawReflectionSnapshot(
          "Replay Logical Reflector Output",
          selected_algorithm_summary->bridge_debug_set.logical_replay_reflector_snapshot);
      }
      _DrawReflectionSnapshot(
        "Replay Runtime Reflector Snapshot",
        selected_algorithm_summary->bridge_debug_set.replay_reflection_snapshot);

      ImGui::SeparatorText("Bridge Transport Detail");

      if (!selected_algorithm_summary->bridge_debug_set.ingress_bindings.empty()) {
        ImGui::TextUnformatted("Ingress Bindings");
        for (const debug_tool::PipelineStageBridgeDebugBinding& binding :
             selected_algorithm_summary->bridge_debug_set.ingress_bindings) {
          ImGui::BulletText(
            "%s.%s -> %s.%s%s",
            binding.source_stage_name.c_str(),
            binding.source_container_name.c_str(),
            binding.target_stage_name.c_str(),
            binding.target_container_name.c_str(),
            binding.required ? "" : " (optional)");
        }
      }
      if (!selected_algorithm_summary->bridge_debug_set.egress_bindings.empty()) {
        ImGui::TextUnformatted("Egress Bindings");
        for (const debug_tool::PipelineStageBridgeDebugBinding& binding :
             selected_algorithm_summary->bridge_debug_set.egress_bindings) {
          ImGui::BulletText(
            "%s.%s -> %s.%s%s",
            binding.source_stage_name.c_str(),
            binding.source_container_name.c_str(),
            binding.target_stage_name.c_str(),
            binding.target_container_name.c_str(),
            binding.required ? "" : " (optional)");
        }
      }

      if (ImGui::Button("Replay Logical Stage Once", ImVec2(220.0f, 0.0f))) {
        std::string replay_error_message;
        if (!host.ReplayPipelineStageBridgeDebug(
              0u,
              static_cast<size_t>(selected_algorithm_index),
              &replay_error_message)) {
          host.ui_status_message() = replay_error_message.empty()
            ? "Logical stage replay failed."
            : std::move(replay_error_message);
        } else {
          host.ui_status_message() = "Logical stage replay executed once.";
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

  auto build_and_apply_preview_for_algorithm = [&](size_t algorithm_index, std::string* out_error_message) -> bool {
    runtime_systems::RenderPreviewRequest mounted_preview_request{};
    std::string mounted_preview_error_message;
    if (!host.BuildRenderPreviewRequest(
          0u,
          algorithm_index,
          &mounted_preview_request,
          &mounted_preview_error_message)) {
      host.SetRenderPreviewRequest({});
      if (out_error_message) {
        *out_error_message = mounted_preview_error_message.empty()
          ? "Submitted algorithm did not produce a drawable preview."
          : std::move(mounted_preview_error_message);
      }
      return false;
    }
    if (!mounted_preview_request.valid) {
      host.SetRenderPreviewRequest({});
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
    agent_composer_ui_state_.preview_request_dirty = false;
    return true;
  };

  const std::string algorithm_name = _TrimCopy(agent_composer_ui_state_.algorithm_name.data());

  const auto build_current_resource_bindings = [&]() {
    std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings;
    resource_bindings.reserve(agent_composer_ui_state_.resource_inputs.size());
    for (const auto& resource : agent_composer_ui_state_.resource_inputs) {
      resource_bindings.push_back(debug_tool::AlgorithmResourceBinding{
        .resource_name = resource.resource_name,
        .resource_kind = resource.resource_kind,
        .source_path = _TrimCopy(resource.resource_path.data()),
      });
    }
    return resource_bindings;
  };

  const auto build_current_descriptor_values = [&]() {
    std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values;
    descriptor_values.reserve(agent_composer_ui_state_.descriptor_inputs.size());
    for (const auto& descriptor : agent_composer_ui_state_.descriptor_inputs) {
      descriptor_values.push_back(debug_tool::AlgorithmDescriptorValue{
        .descriptor_name = descriptor.descriptor_name,
        .scalar_value = descriptor.scalar_value,
      });
    }
    return descriptor_values;
  };

  const auto clear_built_in_agent_mounts = [&](std::string* out_error_message) {
    std::string clear_error_message;
    debug_tool::AgentRuntimeSummary clear_summary{};
    while (host.GetAgentSummary(0u, &clear_summary) && !clear_summary.algorithms.empty()) {
      if (!host.DetachAlgorithmFromAgent(0u, 0u, &clear_error_message)) {
        if (out_error_message) {
          *out_error_message = clear_error_message.empty()
            ? "Failed to clear mounted algorithms from the built-in agent."
            : std::move(clear_error_message);
        }
        return false;
      }
    }
    agent_composer_ui_state_.selected_algorithm_index = -1;
    agent_composer_ui_state_.preview_request_dirty = true;
    host.SetRenderPreviewRequest({});
    host.ClearGpuRuntimeCaches();
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  };

  bool current_algorithm_is_pipeline = false;
  std::string pipeline_query_error_message;
  if (!algorithm_name.empty() &&
      !host.IsPipelineAlgorithm(
        algorithm_name,
        &current_algorithm_is_pipeline,
        &pipeline_query_error_message)) {
    current_algorithm_is_pipeline = false;
  }

  const auto build_testsubmit_pipeline_name = [&](const std::string& pipeline_algorithm_name) {
    std::unordered_set<std::string> used_pipeline_names{};
    for (const debug_tool::AlgorithmRuntimeSummary& algorithm_summary : selected_agent_summary.algorithms) {
      if (algorithm_summary.pipeline_stage && !algorithm_summary.pipeline_name.empty()) {
        used_pipeline_names.insert(algorithm_summary.pipeline_name);
      }
    }

    const std::string prefix = pipeline_algorithm_name + "::testsubmit";
    size_t suffix = 0u;
    while (true) {
      const std::string candidate = prefix + "_" + std::to_string(suffix);
      if (used_pipeline_names.find(candidate) == used_pipeline_names.end()) {
        return candidate;
      }
      ++suffix;
    }
  };

  if (selected_algorithm_summary) {
    runtime_systems::RenderPreviewRequest preview_request{};
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
      agent_composer_ui_state_.preview_request_dirty = false;
    } else if (selected_algorithm_summary->pipeline_name.empty()) {
      host.SetRenderPreviewRequest({});
      agent_composer_ui_state_.preview_request_dirty = true;
    }
  }

  const bool selected_runtime_is_pipeline =
    selected_algorithm_summary &&
    selected_algorithm_summary->pipeline_stage &&
    !selected_algorithm_summary->pipeline_name.empty();
  const bool selected_runtime_is_normal =
    selected_algorithm_summary && !selected_algorithm_summary->pipeline_stage;
  const std::string selected_runtime_pipeline_root_algorithm_name =
    selected_runtime_is_pipeline && selected_algorithm_summary
      ? selected_algorithm_summary->pipeline_root_stage_name
      : std::string{};
  const bool selected_runtime_pipeline_matches_requested =
    selected_runtime_is_pipeline &&
    !selected_runtime_pipeline_root_algorithm_name.empty() &&
    selected_runtime_pipeline_root_algorithm_name == algorithm_name;
  const bool selected_runtime_normal_matches_requested =
    selected_runtime_is_normal &&
    selected_algorithm_summary &&
    selected_algorithm_summary->algorithm_name == algorithm_name;

  const auto apply_pipeline_run_policy = [&](const std::string& mounted_name, const char* mounted_state_text) {
    if (agent_composer_ui_state_.pipeline_run_from_stage0_to_end) {
      host.StartTicking();
      host.ui_status_message() = std::string(mounted_state_text) + " '" + mounted_name + "' and started ticking.";
    } else {
      host.PauseTicking();
      host.ui_status_message() = std::string(mounted_state_text) + " '" + mounted_name + "' for stage-by-stage debug.";
    }
  };

  if (current_algorithm_is_pipeline) {
    ImGui::SeparatorText("Pipeline Controls");
    ImGui::Checkbox(
      "Run From Stage0 To End",
      &agent_composer_ui_state_.pipeline_run_from_stage0_to_end);
    ImGui::TextUnformatted(
      "Select a mounted stage in Agent Manager to inspect its reflection snapshot while the pipeline is paused.");

    const char* mount_pipeline_label =
      selected_runtime_pipeline_matches_requested ? "Reset Pipeline" : "Mount Pipeline";
    if (ImGui::Button(mount_pipeline_label, ImVec2(180.0f, 0.0f))) {
      if (algorithm_name.empty()) {
        host.ui_status_message() = "Algorithm name must not be empty.";
      } else {
        const std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings = build_current_resource_bindings();
        const std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values = build_current_descriptor_values();
        std::string attach_error_message;
        host.PauseTicking();
        if (!clear_built_in_agent_mounts(&attach_error_message)) {
          host.ui_status_message() = attach_error_message;
        } else {
          const std::string pipeline_instance_name = algorithm_name;
          const std::string pipeline_algorithm_name = algorithm_name;
          size_t attached_algorithm_index = 0u;
          if (!host.AttachPipelinePackageToAgent(
                0u,
                pipeline_instance_name,
                pipeline_algorithm_name,
                resource_bindings,
                descriptor_values,
                &attached_algorithm_index,
                &attach_error_message,
                agent_composer_ui_state_.execution_preference)) {
            DEBUG_TOOL_ASSERT(false, "Failed to mount pipeline package to the built-in agent.");
            host.ui_status_message() = attach_error_message.empty()
              ? "Failed to mount pipeline package."
              : ("Failed to mount pipeline package: " + attach_error_message);
          } else {
            agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(attached_algorithm_index);
            host.SetRenderPreviewRequest({});
            host.ui_status_message() =
              "Pipeline mounted as a render pipeline. Submit a resource batch to run it.";
          }
        }
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Submit Resource Batch", ImVec2(180.0f, 0.0f))) {
      if (algorithm_name.empty()) {
        host.ui_status_message() = "Algorithm name must not be empty.";
      } else {
        const std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings = build_current_resource_bindings();
        const std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values = build_current_descriptor_values();
        const std::string pipeline_instance_name = build_testsubmit_pipeline_name(algorithm_name);
        size_t attached_algorithm_index = 0u;
        std::string attach_error_message;
        if (!host.AttachPipelinePackageToAgent(
              0u,
              pipeline_instance_name,
              algorithm_name,
              resource_bindings,
              descriptor_values,
              &attached_algorithm_index,
              &attach_error_message,
              agent_composer_ui_state_.execution_preference)) {
          DEBUG_TOOL_ASSERT(false, "Failed to append a pipeline resource submission.");
          host.ui_status_message() = attach_error_message.empty()
            ? "Failed to append the pipeline resource submission."
            : ("Failed to append the pipeline resource submission: " + attach_error_message);
        } else {
          agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(attached_algorithm_index);
          std::string preview_error_message;
          if (!build_and_apply_preview_for_algorithm(attached_algorithm_index, &preview_error_message)) {
            host.ui_status_message() = preview_error_message.empty()
              ? "Pipeline resource submission mounted, but no drawable result was produced."
              : std::move(preview_error_message);
          } else {
            apply_pipeline_run_policy(pipeline_instance_name, "Submitted pipeline resource batch");
          }
        }
      }
    }
  } else {
    ImGui::SeparatorText("Algorithm Controls");
    const char* mount_algorithm_label =
      selected_runtime_normal_matches_requested ? "Reset Mount" : "Mount Algorithm";
    if (ImGui::Button(mount_algorithm_label, ImVec2(180.0f, 0.0f))) {
      if (algorithm_name.empty()) {
        host.ui_status_message() = "Algorithm name must not be empty.";
      } else {
        const std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings = build_current_resource_bindings();
        const std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values = build_current_descriptor_values();
        std::string attach_error_message;
        host.PauseTicking();
        if (!clear_built_in_agent_mounts(&attach_error_message)) {
          host.ui_status_message() = attach_error_message;
        } else {
          size_t attached_algorithm_index = 0u;
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
              ? "Failed to mount algorithm on built-in agent."
              : ("Failed to mount algorithm on built-in agent: " + attach_error_message);
          } else {
            agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(attached_algorithm_index);
            std::string preview_error_message;
            const bool preview_ready =
              build_and_apply_preview_for_algorithm(attached_algorithm_index, &preview_error_message);
            if (!preview_ready) {
              host.SetRenderPreviewRequest({});
              agent_composer_ui_state_.preview_request_dirty = true;
              host.ui_status_message() = preview_error_message.empty()
                ? "Algorithm mounted. No drawable preview is available, but ticking can still start."
                : ("Algorithm mounted without drawable preview: " + preview_error_message);
            } else {
              host.ui_status_message() = "Algorithm mounted. Press Start Tick to run it.";
            }
          }
        }
      }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!selected_algorithm_summary);
    if (ImGui::Button("Start Tick", ImVec2(180.0f, 0.0f))) {
      if (!selected_algorithm_summary) {
        host.ui_status_message() = "Mount an algorithm first.";
      } else if (!selected_algorithm_summary->pipeline_stage) {
        std::string preview_error_message;
        const bool preview_ready = build_and_apply_preview_for_algorithm(
          static_cast<size_t>(selected_algorithm_index),
          &preview_error_message);
        if (!preview_ready) {
          host.SetRenderPreviewRequest({});
          agent_composer_ui_state_.preview_request_dirty = true;
        }
        host.StartTicking();
        host.ui_status_message() = preview_ready
          ? "Mounted algorithm started."
          : (preview_error_message.empty()
              ? "Mounted algorithm started without a drawable preview."
              : ("Mounted algorithm started without a drawable preview: " + preview_error_message));
      } else {
        std::string preview_error_message;
        if (!build_and_apply_preview_for_algorithm(
              static_cast<size_t>(selected_algorithm_index),
              &preview_error_message)) {
          host.ui_status_message() = preview_error_message.empty()
            ? "Mounted pipeline did not produce a drawable preview."
            : std::move(preview_error_message);
        } else {
          host.StartTicking();
          host.ui_status_message() = "Mounted pipeline started.";
        }
      }
    }
    ImGui::EndDisabled();
  }

  ImGui::Spacing();
  ImGui::BeginDisabled(!selected_algorithm_summary);
  if (ImGui::Button("Hot Update DLL + SPV", ImVec2(180.0f, 0.0f))) {
    size_t rebuilt_algorithm_index = static_cast<size_t>(selected_algorithm_index);
    std::string hot_reload_error_message;
    if (host.HotReloadAlgorithmPackage(
          0u,
          static_cast<size_t>(selected_algorithm_index),
          &rebuilt_algorithm_index,
          &hot_reload_error_message)) {
      agent_composer_ui_state_.selected_algorithm_index = static_cast<int>(rebuilt_algorithm_index);
      host.ui_status_message() = "Algorithm hot rebuilt and reloaded.";
    } else {
      host.ui_status_message() = hot_reload_error_message.empty()
        ? "Algorithm hot rebuild failed."
        : std::move(hot_reload_error_message);
    }
  }
  ImGui::EndDisabled();

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
