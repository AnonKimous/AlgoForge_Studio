#include "interact_ui_panel.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <imgui.h>
#include <sstream>

#include "cJSON.h"

namespace interact_ui {

namespace {

std::string _AlgorithmCatalogPath() {
  const std::filesystem::path candidates[] = {
    "src/capabilities/algorithm_library/algorithm_catalog.json",
    "../src/capabilities/algorithm_library/algorithm_catalog.json",
    "../../src/capabilities/algorithm_library/algorithm_catalog.json",
    "../../../src/capabilities/algorithm_library/algorithm_catalog.json",
  };

  std::error_code ec;
  for (const std::filesystem::path& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
      return candidate.string();
    }
  }
  return candidates[0].string();
}

std::string _AlgorithmLibraryRootPath() {
  const std::filesystem::path candidates[] = {
    "src/capabilities/algorithm_library",
    "../src/capabilities/algorithm_library",
    "../../src/capabilities/algorithm_library",
    "../../../src/capabilities/algorithm_library",
  };

  std::error_code ec;
  for (const std::filesystem::path& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
      return candidate.string();
    }
  }
  return candidates[0].string();
}

std::string _ResolveAlgorithmShaderPath(
  const std::string& algorithm_name,
  const std::string& shader_path) {
  if (shader_path.empty()) {
    return {};
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    return path.string();
  }

  return (std::filesystem::path(_AlgorithmLibraryRootPath()) / algorithm_name / shader_path).string();
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
  const std::vector<AlgorithmCatalogEntry>& catalog_entries,
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

std::string _ReadTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GetJsonStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
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

std::vector<AlgorithmCatalogEntry> _LoadAlgorithmCatalog(std::string* out_error_message) {
  std::vector<AlgorithmCatalogEntry> entries;
  const std::string catalog_path = _AlgorithmCatalogPath();
  const std::string json_text = _ReadTextFile(catalog_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read algorithm catalog: " + catalog_path;
    }
    return entries;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse algorithm catalog: " + catalog_path;
    }
    return entries;
  }

  const cJSON* algorithms = cJSON_GetObjectItemCaseSensitive(root, "algorithms");
  if (!algorithms || !cJSON_IsArray(algorithms)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Algorithm catalog is missing the algorithms array.";
    }
    return entries;
  }

  const int count = cJSON_GetArraySize(algorithms);
  entries.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(algorithms, i);
    if (!item || !cJSON_IsObject(item)) {
      continue;
    }

    AlgorithmCatalogEntry entry{};
    entry.algorithm_name = _GetJsonStringField(item, "name");
    entry.display_name = _GetJsonStringField(item, "display_name");
    entry.folder_name = _GetJsonStringField(item, "folder");
    entry.container_manifest_name = _GetJsonStringField(item, "container_manifest");
    entry.decomposer_name = _GetJsonStringField(item, "decomposer");
    entry.reflector_name = _GetJsonStringField(item, "reflector");
    entry.intervention_name = _GetJsonStringField(item, "intervention");

    if (entry.display_name.empty()) {
      entry.display_name = entry.algorithm_name;
    }
    if (!entry.algorithm_name.empty()) {
      entries.push_back(std::move(entry));
    }
  }

  cJSON_Delete(root);
  if (entries.empty() && out_error_message) {
    *out_error_message = "Algorithm catalog does not contain any entries.";
  }
  return entries;
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

struct RequestedResourceEntry {
  std::string resource_name;
  std::string resource_kind;
  bool required{true};
};

struct RequestedDescriptorEntry {
  std::string descriptor_name;
  std::string container_name;
  uint32_t array_index{0u};
};

bool _QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  std::vector<RequestedResourceEntry>* out_resources,
  std::vector<RequestedDescriptorEntry>* out_descriptors,
  std::string* out_error_message) {
  if (!out_resources || !out_descriptors) {
    if (out_error_message) {
      *out_error_message = "Requested binding output pointers are null.";
    }
    return false;
  }

  out_resources->clear();
  out_descriptors->clear();

  std::shared_ptr<agent::IAlgorithmPackageDecomposer> decomposer;
  std::string error_message;
  if (!codec::CreateAlgorithmPackageDecomposerByName(algorithm_name, &decomposer, &error_message) || !decomposer) {
    if (out_error_message) {
      *out_error_message = error_message.empty()
        ? ("Failed to create decomposer for algorithm '" + algorithm_name + "'.")
        : std::move(error_message);
    }
    return false;
  }

  AlgorithmProfile profile{};
  profile.algorithm_name = algorithm_name;

  agent::AlgorithmRequestedResources requested_resources{};
  agent::AlgorithmRequestedDescriptorBindings requested_descriptor_bindings{};
  const bool resources_ok = decomposer->GetRequestedResources(profile, &requested_resources);
  const bool descriptors_ok = decomposer->GetRequestedDescriptorBindings(profile, &requested_descriptor_bindings);
  if (!resources_ok && !descriptors_ok) {
    if (out_error_message) {
      *out_error_message = "Algorithm decomposer did not expose requested resources or descriptors for '" +
        algorithm_name + "'.";
    }
    return false;
  }

  for (const agent::AlgorithmRequestedResources::RequiredResource& resource : requested_resources.required_resources) {
    out_resources->push_back(RequestedResourceEntry{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .required = resource.required,
    });
  }
  for (const agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot& descriptor :
       requested_descriptor_bindings.descriptor_slots) {
    out_descriptors->push_back(RequestedDescriptorEntry{
      .descriptor_name = descriptor.descriptor_name,
      .container_name = descriptor.container_name,
      .array_index = descriptor.array_index,
    });
  }

  return true;
}

template <typename T>
void _ResizePreservingValues(std::vector<T>* values, size_t new_size) {
  if (!values) {
    return;
  }
  values->resize(new_size);
}

bool _BuildRenderPreviewRequest(
  const agent::Agent& agent,
  size_t algorithm_index,
  runtime_systems::RenderPreviewRequest* out_request,
  std::string* out_error_message) {
  if (!out_request) {
    if (out_error_message) {
      *out_error_message = "Preview request output pointer is null.";
    }
    return false;
  }

  out_request->Clear();

  const agent::AgentAlgorithmCodecGroup* group = agent.algorithm_codec_group(algorithm_index);
  if (!group || !group->intervention) {
    return false;
  }
  const std::string algorithm_name = group->algorithm_profile.algorithm_name;

  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  if (!group->intervention->GetInterventionStageSpecs(&stage_specs) || stage_specs.empty()) {
    return false;
  }

  const agent::AlgorithmInterventionStageSpec* result_stage = nullptr;
  for (const agent::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      result_stage = &stage_spec;
      break;
    }
  }
  if (!result_stage) {
    return false;
  }
  if (result_stage->shader.vertex_shader_path.empty() || result_stage->shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Result-render stage is missing shader paths.";
    }
    return false;
  }

  const agent::AlgorithmObject* algorithm_object = agent.algorithm_object(algorithm_index);
  if (!algorithm_object) {
    if (out_error_message) {
      *out_error_message = "Algorithm object is unavailable.";
    }
    return false;
  }

  const AlgorithmContainerSet* container_set = algorithm_object->container_set();
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "Algorithm container set is unavailable.";
    }
    return false;
  }

  out_request->stage_name = result_stage->stage_name;
  out_request->vertex_shader_path = _ResolveAlgorithmShaderPath(algorithm_name, result_stage->shader.vertex_shader_path);
  out_request->fragment_shader_path = _ResolveAlgorithmShaderPath(algorithm_name, result_stage->shader.fragment_shader_path);
  out_request->storage_buffers.reserve(result_stage->used_algorithm_containers.size());

  uint32_t instance_count = 0u;
  bool have_instance_count = false;
  for (const agent::AlgorithmInterventionContainerBinding& binding : result_stage->used_algorithm_containers) {
    const AlgorithmContainer* container = FindAlgorithmContainer(*container_set, binding.container_name);
    if (!container) {
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Missing preview container: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }
    if (container->storage_kind != AlgorithmContainerStorageKind::Array) {
      if (out_error_message) {
        *out_error_message = "Preview container is not an array: " + binding.container_name;
      }
      out_request->Clear();
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Preview container has no data: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }

    runtime_systems::RenderPreviewBuffer preview_buffer{};
    preview_buffer.binding_name = binding.container_name;
    preview_buffer.element_stride = container->element_stride;
    preview_buffer.bytes = container->bytes;
    out_request->storage_buffers.push_back(std::move(preview_buffer));

    const uint32_t buffer_instances = static_cast<uint32_t>(container->bytes.size() / container->element_stride);
    if (!have_instance_count) {
      instance_count = buffer_instances;
      have_instance_count = true;
    } else {
      instance_count = std::min(instance_count, buffer_instances);
    }
  }

  if (out_request->storage_buffers.empty()) {
    if (out_error_message) {
      *out_error_message = "Result-render stage does not expose any usable array container.";
    }
    out_request->Clear();
    return false;
  }

  out_request->instance_count = instance_count;
  out_request->valid = out_request->instance_count > 0u;
  if (!out_request->valid && out_error_message) {
    *out_error_message = "Preview request has no drawable instances.";
  }
  return out_request->valid;
}

}  // namespace

void InteractUiPanel::InitializeAgentComposerDefaults() {
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
  agent_composer_ui_state_.algorithm_catalog_loaded = false;
  agent_composer_ui_state_.algorithm_catalog_error.clear();
  agent_composer_ui_state_.reflected_algorithm_name.clear();
  agent_composer_ui_state_.reflection_error.clear();
  agent_composer_ui_state_.reflection_valid = false;

  std::string catalog_error;
  agent_composer_ui_state_.algorithm_catalog_entries = _LoadAlgorithmCatalog(&catalog_error);
  agent_composer_ui_state_.algorithm_catalog_loaded = !agent_composer_ui_state_.algorithm_catalog_entries.empty();
  if (!catalog_error.empty()) {
    agent_composer_ui_state_.algorithm_catalog_error = std::move(catalog_error);
  }
  agent_composer_defaults_initialized_ = true;
}

void InteractUiPanel::InitializeFileBrowserDefaults() {
  if (file_browser_defaults_initialized_) {
    return;
  }

  const std::filesystem::path current_root = std::filesystem::current_path();
  _CopyTextToBuffer(current_root.string().c_str(), &file_browser_ui_state_.root_path);
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

void InteractUiPanel::RegisterAgentUiBindings(
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

void InteractUiPanel::SyncCustomInterventionUiState(IInteractUiHost& host) {
  const AgentManager& agent_manager = host.agent_manager();
  if (!agent_manager.has_agents()) {
    active_custom_ui_slots_.clear();
    return;
  }

  std::vector<ActiveCustomInterventionUiSlot> updated_slots;
  for (size_t agent_index = 0; agent_index < agent_manager.agent_count(); ++agent_index) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager.agent(agent_index);
    if (!managed_agent) {
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

    updated_slots.reserve(updated_slots.size() + managed_agent->algorithm_count());
    for (size_t algorithm_index = 0; algorithm_index < managed_agent->algorithm_count(); ++algorithm_index) {
      const agent::AgentAlgorithmCodecGroup* group = managed_agent->algorithm_codec_group(algorithm_index);
      if (!group) {
        continue;
      }

      const AgentInterventionUiBinding* binding =
        _FindUiBinding(*ui_bindings, group->algorithm_profile.algorithm_name);
      if (!binding || !binding->hook) {
        continue;
      }

      ActiveCustomInterventionUiSlot slot{};
      slot.agent_index = agent_index;
      slot.algorithm_index = algorithm_index;
      slot.algorithm_name = group->algorithm_profile.algorithm_name;
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

void InteractUiPanel::DrawCustomInterventionUi(
  IInteractUiHost& host,
  size_t agent_index,
  size_t algorithm_index,
  const agent::AgentAlgorithmCodecGroup& group,
  ActiveCustomInterventionUiSlot* slot) {
  if (!slot || !slot->hook) {
    ImGui::TextUnformatted("No custom intervention UI.");
    return;
  }
  if (!slot->state) {
    ImGui::TextUnformatted("Custom intervention UI state is unavailable.");
    return;
  }

  const std::shared_ptr<agent::Agent> managed_agent = host.agent_manager().agent(agent_index);
  if (!managed_agent) {
    ImGui::TextUnformatted("Managed agent is unavailable.");
    return;
  }
  agent::AgentAlgorithmRuntimeState* runtime_state = managed_agent->algorithm_runtime_state(algorithm_index);

  const AgentInterventionUiContext context{
    .agent = managed_agent.get(),
    .input = &host.input(),
    .mouse_pixel = host.mouse_position(),
    .dt_seconds = host.frame_dt_seconds(),
    .agent_to_algorithm_signal = runtime_state ? &runtime_state->agent_to_algorithm_signal : nullptr,
    .algorithm_to_agent_signal = runtime_state ? &runtime_state->algorithm_to_agent_signal : nullptr,
  };
  const std::string header_label = group.algorithm_profile.algorithm_name.empty()
    ? ("Algorithm " + std::to_string(algorithm_index))
    : group.algorithm_profile.algorithm_name;
  ImGui::PushID(static_cast<int>(algorithm_index));
  ImGui::SeparatorText(header_label.c_str());
  slot->hook->DrawUi(context, slot->state.get());
  ImGui::PopID();
}

void InteractUiPanel::DrawAgentComposerUi(IInteractUiHost& host) {
  InitializeAgentComposerDefaults();

  ImGui::SeparatorText("Create Agent");
  ImGui::InputText("Agent Name", agent_composer_ui_state_.agent_name.data(), agent_composer_ui_state_.agent_name.size());
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

  if (ImGui::Button("Create Agent")) {
    const std::string agent_name = _TrimCopy(agent_composer_ui_state_.agent_name.data());
    AgentCreateSpec create_spec{};
    create_spec.agent_name = agent_name.empty() ? "unnamed_agent" : agent_name;

    size_t created_agent_index = 0u;
    if (!host.agent_manager().CreateAgent(std::move(create_spec), &created_agent_index)) {
      host.ui_status_message() = "Failed to create managed agent shell.";
    } else {
      agent_composer_ui_state_.selected_agent_index = static_cast<int>(created_agent_index);
      _CopyTextToBuffer("", &agent_composer_ui_state_.agent_name);
      host.ui_status_message() = "Agent shell created.";
    }
  }

  ImGui::SeparatorText("Mount Algorithm");
  AgentManager& agent_manager = host.agent_manager();
  if (!agent_manager.has_agents()) {
    ImGui::TextUnformatted("Create an agent shell first.");
    return;
  }

  if (agent_composer_ui_state_.selected_agent_index < 0 ||
      agent_composer_ui_state_.selected_agent_index >= static_cast<int>(agent_manager.agent_count())) {
    agent_composer_ui_state_.selected_agent_index = static_cast<int>(agent_manager.agent_count() - 1u);
  }

  const std::shared_ptr<agent::Agent> selected_agent =
    agent_manager.agent(static_cast<size_t>(agent_composer_ui_state_.selected_agent_index));
  const std::string current_agent_display = selected_agent
    ? (selected_agent->agent_name().empty()
      ? ("Agent #" + std::to_string(agent_composer_ui_state_.selected_agent_index))
      : ("Agent #" + std::to_string(agent_composer_ui_state_.selected_agent_index) + "  " +
          selected_agent->agent_name()))
    : "Select agent...";
  if (ImGui::BeginCombo("Target Agent", current_agent_display.c_str())) {
    for (size_t i = 0; i < agent_manager.agent_count(); ++i) {
      const std::shared_ptr<agent::Agent> managed_agent = agent_manager.agent(i);
      if (!managed_agent) {
        continue;
      }
      const std::string label = managed_agent->agent_name().empty()
        ? ("Agent #" + std::to_string(i))
        : ("Agent #" + std::to_string(i) + "  " + managed_agent->agent_name());
      const bool is_selected = static_cast<int>(i) == agent_composer_ui_state_.selected_agent_index;
      if (ImGui::Selectable(label.c_str(), is_selected)) {
        agent_composer_ui_state_.selected_agent_index = static_cast<int>(i);
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::TextUnformatted("Use the Files page to browse folders and drag paths into resource slots.");

  const std::string algorithm_name = _TrimCopy(agent_composer_ui_state_.algorithm_name.data());
  if (algorithm_name.empty()) {
    agent_composer_ui_state_.selected_algorithm_catalog_index = -1;
    if (!agent_composer_ui_state_.reflected_algorithm_name.empty() ||
        agent_composer_ui_state_.reflection_valid ||
        !agent_composer_ui_state_.resource_inputs.empty() ||
        !agent_composer_ui_state_.descriptor_inputs.empty()) {
      agent_composer_ui_state_.reflected_algorithm_name.clear();
      agent_composer_ui_state_.reflection_error.clear();
      agent_composer_ui_state_.reflection_valid = false;
      agent_composer_ui_state_.resource_inputs.clear();
      agent_composer_ui_state_.descriptor_inputs.clear();
    }
  } else {
    const int catalog_index = _FindAlgorithmCatalogIndex(agent_composer_ui_state_.algorithm_catalog_entries, algorithm_name);
    agent_composer_ui_state_.selected_algorithm_catalog_index = catalog_index;
    if (algorithm_name != agent_composer_ui_state_.reflected_algorithm_name) {
      std::vector<RequestedResourceEntry> requested_resources;
      std::vector<RequestedDescriptorEntry> requested_descriptors;
      std::string error_message;
      if (_QueryAlgorithmRequestedBindings(
            algorithm_name,
            &requested_resources,
            &requested_descriptors,
            &error_message)) {
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
      } else {
        agent_composer_ui_state_.reflected_algorithm_name = algorithm_name;
        agent_composer_ui_state_.reflection_error = std::move(error_message);
        agent_composer_ui_state_.reflection_valid = false;
        agent_composer_ui_state_.resource_inputs.clear();
        agent_composer_ui_state_.descriptor_inputs.clear();
      }
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

  ImGui::SeparatorText("Requested Resources");
  if (agent_composer_ui_state_.resource_inputs.empty()) {
    ImGui::TextUnformatted("No requested resources exposed by the decomposer.");
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
    ImGui::TextUnformatted("No requested descriptors exposed by the decomposer.");
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

  if (!ImGui::Button("Attach Algorithm")) {
    return;
  }

  if (agent_composer_ui_state_.selected_agent_index < 0) {
    host.ui_status_message() = "Create an agent shell first.";
    return;
  }
  if (algorithm_name.empty()) {
    host.ui_status_message() = "Algorithm name must not be empty.";
    return;
  }

  if (static_cast<size_t>(agent_composer_ui_state_.selected_agent_index) >= agent_manager.agent_count()) {
    host.ui_status_message() = "Selected agent is no longer available.";
    return;
  }

  std::vector<agent::AlgorithmResourceBinding> resource_bindings;
  for (const auto& resource : agent_composer_ui_state_.resource_inputs) {
    resource_bindings.push_back(agent::AlgorithmResourceBinding{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .source_path = _TrimCopy(resource.resource_path.data()),
    });
  }
  std::vector<agent::AlgorithmDescriptorValue> descriptor_values;
  for (const auto& descriptor : agent_composer_ui_state_.descriptor_inputs) {
    descriptor_values.push_back(agent::AlgorithmDescriptorValue{
      .descriptor_name = descriptor.descriptor_name,
      .scalar_value = descriptor.scalar_value,
    });
  }

  size_t attached_algorithm_index = 0u;
  std::string attach_error_message;
  if (!agent_manager.AttachAlgorithmToAgent(
        static_cast<size_t>(agent_composer_ui_state_.selected_agent_index),
        algorithm_name,
        resource_bindings,
        descriptor_values,
        &attached_algorithm_index,
        &attach_error_message)) {
    host.ui_status_message() = attach_error_message.empty()
      ? "Failed to attach algorithm to agent."
      : std::move(attach_error_message);
    return;
  }

  (void)attached_algorithm_index;
  host.ui_status_message() = "Algorithm attached to agent.";
}

void InteractUiPanel::DrawAgentBindingUi(IInteractUiHost& host) {
  DrawAgentComposerUi(host);
  ImGui::Separator();

  AgentManager& agent_manager = host.agent_manager();
  ImGui::Text("Managed agents: %zu", agent_manager.agent_count());
  if (agent_manager.has_agents()) {
    if (ImGui::Button("Clear All Agents")) {
      agent_manager.ClearAgents();
      managed_agent_ui_bindings_.clear();
      active_custom_ui_slots_.clear();
      agent_composer_ui_state_.selected_agent_index = -1;
      host.ui_status_message() = "Cleared all managed agents.";
    }
  }

  if (!agent_manager.has_agents()) {
    ImGui::TextUnformatted("No managed agents.");
    return;
  }

  for (size_t agent_index = 0; agent_index < agent_manager.agent_count(); ++agent_index) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager.agent(agent_index);
    if (!managed_agent) {
      continue;
    }

    const std::string agent_header = managed_agent->agent_name().empty()
      ? ("Agent #" + std::to_string(agent_index))
      : ("Agent #" + std::to_string(agent_index) + "  " + managed_agent->agent_name());
    ImGui::SeparatorText(agent_header.c_str());
    if (ImGui::Button(("Destroy Agent##" + std::to_string(agent_index)).c_str())) {
      agent_manager.DestroyAgent(agent_index);
      managed_agent_ui_bindings_.erase(
        std::remove_if(
          managed_agent_ui_bindings_.begin(),
          managed_agent_ui_bindings_.end(),
          [&](const ManagedAgentInterventionUiBindings& bindings) {
            return bindings.agent_index == agent_index;
          }),
        managed_agent_ui_bindings_.end());
      for (ManagedAgentInterventionUiBindings& bindings : managed_agent_ui_bindings_) {
        if (bindings.agent_index > agent_index) {
          --bindings.agent_index;
        }
      }
      if (agent_composer_ui_state_.selected_agent_index == static_cast<int>(agent_index)) {
        const size_t remaining_agents = agent_manager.agent_count();
        if (remaining_agents == 0u) {
          agent_composer_ui_state_.selected_agent_index = -1;
        } else if (agent_index >= remaining_agents) {
          agent_composer_ui_state_.selected_agent_index = static_cast<int>(remaining_agents - 1u);
        }
      } else if (agent_composer_ui_state_.selected_agent_index > static_cast<int>(agent_index)) {
        --agent_composer_ui_state_.selected_agent_index;
      }
      active_custom_ui_slots_.clear();
      host.ui_status_message() = "Destroyed managed agent.";
      break;
    }

    ImGui::Text("Algorithms: %zu", managed_agent->algorithm_count());
    for (size_t algorithm_index = 0; algorithm_index < managed_agent->algorithm_count(); ++algorithm_index) {
      const agent::AgentAlgorithmCodecGroup* group = managed_agent->algorithm_codec_group(algorithm_index);
      if (!group) {
        continue;
      }
      const std::string algorithm_name = group->algorithm_profile.algorithm_name.empty()
        ? ("<algorithm " + std::to_string(algorithm_index) + ">")
        : group->algorithm_profile.algorithm_name;
      ImGui::BulletText("#%zu %s", algorithm_index, algorithm_name.c_str());
    }
  }

  SyncCustomInterventionUiState(host);
  for (ActiveCustomInterventionUiSlot& slot : active_custom_ui_slots_) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager.agent(slot.agent_index);
    const agent::AgentAlgorithmCodecGroup* group =
      managed_agent ? managed_agent->algorithm_codec_group(slot.algorithm_index) : nullptr;
    if (!group || !slot.hook) {
      continue;
    }
    ImGui::Spacing();
    const char* custom_title = slot.hook->title();
    ImGui::SeparatorText(custom_title ? custom_title : "Custom Intervention UI");
    DrawCustomInterventionUi(host, slot.agent_index, slot.algorithm_index, *group, &slot);
  }
}

void InteractUiPanel::DrawFileBrowserUi(IInteractUiHost&) {
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

void InteractUiPanel::DrawAgentManagerUi(IInteractUiHost& host) {
  InitializeAgentComposerDefaults();

  if (!ImGui::Begin("Agent Manager")) {
    ImGui::End();
    return;
  }
  AgentManager& agent_manager = host.agent_manager();

  ImGui::SeparatorText("Create Agent");
  ImGui::InputText("Agent Name", agent_composer_ui_state_.agent_name.data(), agent_composer_ui_state_.agent_name.size());
  if (ImGui::Button("Create Agent")) {
    const std::string agent_name = _TrimCopy(agent_composer_ui_state_.agent_name.data());
    AgentCreateSpec create_spec{};
    create_spec.agent_name = agent_name.empty() ? "unnamed_agent" : agent_name;

    size_t created_agent_index = 0u;
    if (!host.agent_manager().CreateAgent(std::move(create_spec), &created_agent_index)) {
      host.ui_status_message() = "Failed to create managed agent shell.";
    } else {
      agent_composer_ui_state_.selected_agent_index = static_cast<int>(created_agent_index);
      _CopyTextToBuffer("", &agent_composer_ui_state_.agent_name);
      host.ui_status_message() = "Agent shell created.";
    }
  }

  ImGui::SeparatorText("Managed Agents");
  ImGui::Text("Managed agents: %zu", agent_manager.agent_count());
  if (!agent_manager.has_agents()) {
    ImGui::TextUnformatted("No managed agents.");
    ImGui::End();
    return;
  }

  if (agent_composer_ui_state_.selected_agent_index < 0 ||
      agent_composer_ui_state_.selected_agent_index >= static_cast<int>(agent_manager.agent_count())) {
    agent_composer_ui_state_.selected_agent_index = static_cast<int>(agent_manager.agent_count() - 1u);
  }

  auto select_agent = [&](size_t agent_index, int algorithm_index) {
    agent_composer_ui_state_.selected_agent_index = static_cast<int>(agent_index);
    agent_composer_ui_state_.selected_algorithm_index = algorithm_index;
  };

  for (size_t agent_index = 0; agent_index < agent_manager.agent_count(); ++agent_index) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager.agent(agent_index);
    if (!managed_agent) {
      continue;
    }

    const std::string agent_header = managed_agent->agent_name().empty()
      ? ("Agent #" + std::to_string(agent_index))
      : ("Agent #" + std::to_string(agent_index) + "  " + managed_agent->agent_name());
    ImGui::PushID(static_cast<int>(agent_index));
    if (managed_agent->algorithm_count() == 0u) {
      const ImGuiTreeNodeFlags leaf_flags =
        ImGuiTreeNodeFlags_Leaf |
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ((static_cast<int>(agent_index) == agent_composer_ui_state_.selected_agent_index)
          ? ImGuiTreeNodeFlags_Selected
          : 0);
      ImGui::TreeNodeEx(agent_header.c_str(), leaf_flags);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        select_agent(agent_index, -1);
      }
      ImGui::TextUnformatted("No algorithms.");
    } else {
      ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;
      if (static_cast<int>(agent_index) == agent_composer_ui_state_.selected_agent_index) {
        flags |= ImGuiTreeNodeFlags_Selected;
      }

      const bool opened = ImGui::TreeNodeEx(agent_header.c_str(), flags);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        select_agent(agent_index, 0);
      }

      if (opened) {
        for (size_t algorithm_index = 0; algorithm_index < managed_agent->algorithm_count(); ++algorithm_index) {
          const agent::AgentAlgorithmCodecGroup* group = managed_agent->algorithm_codec_group(algorithm_index);
          if (!group) {
            continue;
          }
          const std::string algorithm_name = group->algorithm_profile.algorithm_name.empty()
            ? ("<algorithm " + std::to_string(algorithm_index) + ">")
            : group->algorithm_profile.algorithm_name;
          const bool algorithm_selected =
            static_cast<int>(agent_index) == agent_composer_ui_state_.selected_agent_index &&
            static_cast<int>(algorithm_index) == agent_composer_ui_state_.selected_algorithm_index;
          if (ImGui::Selectable(algorithm_name.c_str(), algorithm_selected)) {
            select_agent(agent_index, static_cast<int>(algorithm_index));
          }
        }
        ImGui::TreePop();
      }
    }
    ImGui::PopID();
  }

  ImGui::Separator();
  if (ImGui::Button("Destroy Selected Agent")) {
    const int selected_agent_index = agent_composer_ui_state_.selected_agent_index;
    if (selected_agent_index >= 0 &&
        static_cast<size_t>(selected_agent_index) < agent_manager.agent_count() &&
        agent_manager.DestroyAgent(static_cast<size_t>(selected_agent_index))) {
      managed_agent_ui_bindings_.erase(
        std::remove_if(
          managed_agent_ui_bindings_.begin(),
          managed_agent_ui_bindings_.end(),
          [&](const ManagedAgentInterventionUiBindings& bindings) {
            return bindings.agent_index == static_cast<size_t>(selected_agent_index);
          }),
        managed_agent_ui_bindings_.end());
      for (ManagedAgentInterventionUiBindings& bindings : managed_agent_ui_bindings_) {
        if (bindings.agent_index > static_cast<size_t>(selected_agent_index)) {
          --bindings.agent_index;
        }
      }
      active_custom_ui_slots_.clear();
      if (agent_manager.agent_count() == 0u) {
        agent_composer_ui_state_.selected_agent_index = -1;
        agent_composer_ui_state_.selected_algorithm_index = -1;
      } else if (static_cast<size_t>(selected_agent_index) >= agent_manager.agent_count()) {
        agent_composer_ui_state_.selected_agent_index = static_cast<int>(agent_manager.agent_count() - 1u);
        agent_composer_ui_state_.selected_algorithm_index =
          agent_manager.agent(agent_manager.agent_count() - 1u)->algorithm_count() > 0u ? 0 : -1;
      } else {
        const std::shared_ptr<agent::Agent> selected_agent = agent_manager.agent(static_cast<size_t>(selected_agent_index));
        agent_composer_ui_state_.selected_algorithm_index =
          selected_agent && selected_agent->algorithm_count() > 0u ? 0 : -1;
      }
      host.ui_status_message() = "Destroyed managed agent.";
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear All Agents")) {
    agent_manager.ClearAgents();
    managed_agent_ui_bindings_.clear();
    active_custom_ui_slots_.clear();
    agent_composer_ui_state_.selected_agent_index = -1;
    agent_composer_ui_state_.selected_algorithm_index = -1;
    host.ui_status_message() = "Cleared all managed agents.";
  }

  ImGui::End();
}

void InteractUiPanel::DrawAgentDetailUi(IInteractUiHost& host) {
  InitializeAgentComposerDefaults();

  if (!ImGui::Begin("Algorithm Detail")) {
    ImGui::End();
    return;
  }
  AgentManager& agent_manager = host.agent_manager();
  if (!agent_manager.has_agents()) {
    ImGui::TextUnformatted("Create an agent shell first.");
    ImGui::End();
    return;
  }

  if (agent_composer_ui_state_.selected_agent_index < 0 ||
      agent_composer_ui_state_.selected_agent_index >= static_cast<int>(agent_manager.agent_count())) {
    agent_composer_ui_state_.selected_agent_index = static_cast<int>(agent_manager.agent_count() - 1u);
  }

  const std::shared_ptr<agent::Agent> selected_agent =
    agent_manager.agent(static_cast<size_t>(agent_composer_ui_state_.selected_agent_index));
  if (!selected_agent) {
    ImGui::TextUnformatted("Selected agent is unavailable.");
    ImGui::End();
    return;
  }

  const std::string selected_agent_header = selected_agent->agent_name().empty()
    ? ("Agent #" + std::to_string(agent_composer_ui_state_.selected_agent_index))
    : ("Agent #" + std::to_string(agent_composer_ui_state_.selected_agent_index) + "  " + selected_agent->agent_name());
  ImGui::SeparatorText("Selected Algorithm");
  ImGui::Text("Target Agent: %s", selected_agent_header.c_str());

  int selected_algorithm_index = agent_composer_ui_state_.selected_algorithm_index;
  if (selected_algorithm_index < 0 ||
      selected_algorithm_index >= static_cast<int>(selected_agent->algorithm_count())) {
    selected_algorithm_index = selected_agent->algorithm_count() > 0u ? 0 : -1;
    agent_composer_ui_state_.selected_algorithm_index = selected_algorithm_index;
  }

  const agent::AgentAlgorithmCodecGroup* selected_group =
    selected_algorithm_index >= 0
      ? selected_agent->algorithm_codec_group(static_cast<size_t>(selected_algorithm_index))
      : nullptr;
  const std::string selected_algorithm_name = selected_group
    ? (selected_group->algorithm_profile.algorithm_name.empty()
      ? ("<algorithm " + std::to_string(selected_algorithm_index) + ">")
      : selected_group->algorithm_profile.algorithm_name)
    : "No algorithm selected";
  ImGui::Text("Current Algorithm: %s", selected_algorithm_name.c_str());
  if (selected_group) {
    const char* assembly_state_text = "failed";
    switch (selected_agent->algorithm_assembly_state(static_cast<size_t>(selected_algorithm_index))) {
      case agent::AlgorithmAssemblyState::Pending: assembly_state_text = "pending"; break;
      case agent::AlgorithmAssemblyState::Assembling: assembly_state_text = "assembling"; break;
      case agent::AlgorithmAssemblyState::Ready: assembly_state_text = "ready"; break;
      case agent::AlgorithmAssemblyState::Failed: assembly_state_text = "failed"; break;
    }
    ImGui::Text("Assembly State: %s", assembly_state_text);
    ImGui::Text("CPU Symbol: %s", selected_group->cpu_symbol ? "true" : "false");
    ImGui::Text("GPU Symbol: %s", selected_group->gpu_symbol ? "true" : "false");

    if (!selected_group->resource_bindings.empty()) {
      ImGui::SeparatorText("Bound Resources");
      for (const agent::AlgorithmResourceBinding& binding : selected_group->resource_bindings) {
        ImGui::BulletText("%s [%s] -> %s",
          binding.resource_name.c_str(),
          binding.resource_kind.c_str(),
          binding.source_path.c_str());
      }
    }
    if (!selected_group->descriptor_values.empty()) {
      ImGui::SeparatorText("Descriptor Values");
      for (const agent::AlgorithmDescriptorValue& value : selected_group->descriptor_values) {
        ImGui::BulletText("%s = %.3f", value.descriptor_name.c_str(), value.scalar_value);
      }
    }

    const agent::AgentAlgorithmRuntimeState* runtime_state = selected_agent->algorithm_runtime_state(static_cast<size_t>(selected_algorithm_index));
    if (runtime_state && runtime_state->reflection_snapshot.valid) {
      ImGui::SeparatorText("Reflection Snapshot");
      ImGui::Text("Variables: %zu", runtime_state->reflection_snapshot.variables.size());
      ImGui::Text("Variable Arrays: %zu", runtime_state->reflection_snapshot.variable_arrays.size());
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
    const agent::AgentAlgorithmCodecGroup* group = selected_agent->algorithm_codec_group(slot.algorithm_index);
    if (!group || !slot.hook) {
      continue;
    }
    ImGui::Spacing();
    const char* custom_title = slot.hook->title();
    ImGui::SeparatorText(custom_title ? custom_title : "Custom Intervention UI");
    DrawCustomInterventionUi(host, slot.agent_index, slot.algorithm_index, *group, &slot);
  }

  ImGui::SeparatorText("Mount New Algorithm");
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

  const std::string algorithm_name = _TrimCopy(agent_composer_ui_state_.algorithm_name.data());
  if (algorithm_name.empty()) {
    agent_composer_ui_state_.selected_algorithm_catalog_index = -1;
    if (!agent_composer_ui_state_.reflected_algorithm_name.empty() ||
        agent_composer_ui_state_.reflection_valid ||
        !agent_composer_ui_state_.resource_inputs.empty() ||
        !agent_composer_ui_state_.descriptor_inputs.empty()) {
      agent_composer_ui_state_.reflected_algorithm_name.clear();
      agent_composer_ui_state_.reflection_error.clear();
      agent_composer_ui_state_.reflection_valid = false;
      agent_composer_ui_state_.resource_inputs.clear();
      agent_composer_ui_state_.descriptor_inputs.clear();
    }
  } else {
    const int catalog_index = _FindAlgorithmCatalogIndex(agent_composer_ui_state_.algorithm_catalog_entries, algorithm_name);
    agent_composer_ui_state_.selected_algorithm_catalog_index = catalog_index;
    if (algorithm_name != agent_composer_ui_state_.reflected_algorithm_name) {
      std::vector<RequestedResourceEntry> requested_resources;
      std::vector<RequestedDescriptorEntry> requested_descriptors;
      std::string error_message;
      if (_QueryAlgorithmRequestedBindings(
            algorithm_name,
            &requested_resources,
            &requested_descriptors,
            &error_message)) {
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
      } else {
        agent_composer_ui_state_.reflected_algorithm_name = algorithm_name;
        agent_composer_ui_state_.reflection_error = std::move(error_message);
        agent_composer_ui_state_.reflection_valid = false;
        agent_composer_ui_state_.resource_inputs.clear();
        agent_composer_ui_state_.descriptor_inputs.clear();
      }
    }
  }

  ImGui::SeparatorText("Requested Resources");
  if (agent_composer_ui_state_.resource_inputs.empty()) {
    ImGui::TextUnformatted("No requested resources exposed by the decomposer.");
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
    ImGui::TextUnformatted("No requested descriptors exposed by the decomposer.");
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

  if (ImGui::Button("Attach Algorithm")) {
    if (algorithm_name.empty()) {
      host.ui_status_message() = "Algorithm name must not be empty.";
    } else {
      std::vector<agent::AlgorithmResourceBinding> resource_bindings;
      for (const auto& resource : agent_composer_ui_state_.resource_inputs) {
        resource_bindings.push_back(agent::AlgorithmResourceBinding{
          .resource_name = resource.resource_name,
          .resource_kind = resource.resource_kind,
          .source_path = _TrimCopy(resource.resource_path.data()),
        });
      }
      std::vector<agent::AlgorithmDescriptorValue> descriptor_values;
      for (const auto& descriptor : agent_composer_ui_state_.descriptor_inputs) {
        descriptor_values.push_back(agent::AlgorithmDescriptorValue{
          .descriptor_name = descriptor.descriptor_name,
          .scalar_value = descriptor.scalar_value,
        });
      }

      size_t attached_algorithm_index = 0u;
      std::string attach_error_message;
      if (!agent_manager.AttachAlgorithmToAgent(
            static_cast<size_t>(agent_composer_ui_state_.selected_agent_index),
            algorithm_name,
            resource_bindings,
            descriptor_values,
            &attached_algorithm_index,
            &attach_error_message)) {
        host.ui_status_message() = attach_error_message.empty()
          ? "Failed to attach algorithm to agent."
          : std::move(attach_error_message);
      } else {
        (void)attached_algorithm_index;
        host.ui_status_message() = "Algorithm attached to agent.";
      }
    }
  }

  ImGui::End();
}

void InteractUiPanel::DrawAlgorithmPreviewUi(IInteractUiHost& host) {
  if (!ImGui::Begin("Render Preview")) {
    ImGui::End();
    return;
  }

  const AgentManager& agent_manager = host.agent_manager();
  size_t preview_agent_index = 0u;
  if (agent_composer_ui_state_.selected_agent_index >= 0 &&
      static_cast<size_t>(agent_composer_ui_state_.selected_agent_index) < agent_manager.agent_count()) {
    preview_agent_index = static_cast<size_t>(agent_composer_ui_state_.selected_agent_index);
  }
  const std::shared_ptr<agent::Agent> preview_agent = agent_manager.agent(preview_agent_index);
  runtime_systems::RenderPreviewRequest preview_request{};
  std::string preview_error_message;
  if (preview_agent) {
    size_t preview_algorithm_index = 0u;
    if (agent_composer_ui_state_.selected_algorithm_index >= 0 &&
        static_cast<size_t>(agent_composer_ui_state_.selected_algorithm_index) < preview_agent->algorithm_count()) {
      preview_algorithm_index = static_cast<size_t>(agent_composer_ui_state_.selected_algorithm_index);
    }
    _BuildRenderPreviewRequest(
      *preview_agent,
      preview_algorithm_index,
      &preview_request,
      &preview_error_message);
  }

  if (preview_agent) {
    const std::string agent_label = preview_agent->agent_name().empty()
      ? ("Agent #" + std::to_string(preview_agent_index))
      : ("Agent #" + std::to_string(preview_agent_index) + "  " + preview_agent->agent_name());
    const agent::AgentAlgorithmCodecGroup* preview_group = nullptr;
    if (agent_composer_ui_state_.selected_algorithm_index >= 0 &&
        static_cast<size_t>(agent_composer_ui_state_.selected_algorithm_index) < preview_agent->algorithm_count()) {
      preview_group = preview_agent->algorithm_codec_group(static_cast<size_t>(agent_composer_ui_state_.selected_algorithm_index));
    }
    ImGui::Text("Preview Agent: %s", agent_label.c_str());
    ImGui::Text(
      "Preview Algorithm: %s",
      preview_group && !preview_group->algorithm_profile.algorithm_name.empty()
        ? preview_group->algorithm_profile.algorithm_name.c_str()
        : "none");
    if (preview_request.valid) {
      ImGui::Text("Stage: %s", preview_request.stage_name.c_str());
      ImGui::Text("Buffers: %zu", preview_request.storage_buffers.size());
      ImGui::Text("Instances: %u", preview_request.instance_count);
    } else if (!preview_error_message.empty()) {
      ImGui::TextWrapped("%s", preview_error_message.c_str());
    } else {
      ImGui::TextUnformatted("No drawable result-render stage.");
    }
  } else {
    ImGui::TextUnformatted("No preview agent selected.");
  }

  host.SetRenderPreviewRequest(std::move(preview_request));
  ImGui::End();
}

void InteractUiPanel::DrawInteractUi(IInteractUiHost& host) {
  const InputState& input = host.input();
  const Vec2 mouse_position = host.mouse_position();
  host.agent_manager().Tick(input, mouse_position, host.frame_dt_seconds());

  if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  }

  DrawAgentManagerUi(host);
  DrawAgentDetailUi(host);
  DrawFileBrowserUi(host);
  DrawAlgorithmPreviewUi(host);

  if (!ImGui::Begin("Runtime Status")) {
    ImGui::End();
    return;
  }
  ImGui::Text("Status: %s", host.ui_status_message().empty() ? "ready" : host.ui_status_message().c_str());
  ImGui::Text("Managed agents: %zu", host.agent_manager().agent_count());
  ImGui::Text(
    "Algorithm signal: %s",
    host.agent_manager().combined_algorithm_to_agent_signal().pause_requested ? "pause requested" : "idle");
  ImGui::End();
}

void InteractUiPanel::Draw(IInteractUiHost& host) {
  DrawInteractUi(host);
}

void InteractUiPanel::Destroy() {
  agent_composer_ui_state_ = {};
  agent_composer_defaults_initialized_ = false;
  managed_agent_ui_bindings_.clear();
  active_custom_ui_slots_.clear();
}

}  // namespace interact_ui
