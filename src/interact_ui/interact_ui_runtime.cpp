#include "interact_ui_runtime.h"

#include "codec/codec_manager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <utility>

namespace interact_ui {

namespace {

constexpr float kPreviewWorldMinX = -120.0f;
constexpr float kPreviewWorldMaxX = 120.0f;
constexpr float kPreviewWorldMinY = -30.0f;
constexpr float kPreviewWorldMaxY = 30.0f;
constexpr float kPreviewPadding = 24.0f;

#ifndef PROJECT_DATA_ROOT
#define PROJECT_DATA_ROOT "data"
#endif

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

ImVec2 _ProjectPreviewPoint(
  const ImVec2& canvas_origin,
  const ImVec2& canvas_size,
  const Vec3& position) {
  const float usable_width = std::max(1.0f, canvas_size.x - kPreviewPadding * 2.0f);
  const float usable_height = std::max(1.0f, canvas_size.y - kPreviewPadding * 2.0f);
  const float normalized_x = (position.x - kPreviewWorldMinX) / (kPreviewWorldMaxX - kPreviewWorldMinX);
  const float normalized_y = (position.y - kPreviewWorldMinY) / (kPreviewWorldMaxY - kPreviewWorldMinY);
  return ImVec2(
    canvas_origin.x + kPreviewPadding + normalized_x * usable_width,
    canvas_origin.y + canvas_size.y - kPreviewPadding - normalized_y * usable_height);
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

bool _LoadtemporaryTestMeshPlaceholder(
  const std::string& mesh_path,
  Mesh* out_mesh,
  std::string* out_error_message) {
  if (!out_mesh) {
    if (out_error_message) {
      *out_error_message = "Mesh output pointer is null.";
    }
    return false;
  }
  if (mesh_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Mesh resource path must not be empty.";
    }
    return false;
  }

  std::ifstream file(mesh_path, std::ios::binary);
  if (!file) {
    if (out_error_message) {
      *out_error_message = "Failed to open mesh resource file: " + mesh_path;
    }
    return false;
  }

  *out_mesh = Mesh{};
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace

InteractUiRuntime::~InteractUiRuntime() = default;

void InteractUiRuntime::InitializeAgentComposerDefaults() {
  if (agent_composer_defaults_initialized_) {
    return;
  }

  _CopyTextToBuffer("agent_01", &agent_composer_ui_state_.agent_name);
  _CopyTextToBuffer("temporary_test_line_motion", &agent_composer_ui_state_.algorithm_name);
  _CopyTextToBuffer("temporary_test_line_motion", &agent_composer_ui_state_.manifest_name);
  _CopyTextToBuffer(
    (std::string(PROJECT_DATA_ROOT) + "/temporary_test_empty_mesh.obj").c_str(),
    &agent_composer_ui_state_.mesh_resource_path);
  agent_composer_ui_state_.attach_temporary_test_line_motion = true;
  agent_composer_ui_state_.loaded_mesh_resource = Mesh{};
  agent_composer_ui_state_.has_loaded_mesh_resource = false;
  agent_composer_ui_state_.descriptor_scalar_values = {
    -100.0f,
    0.0f,
    0.0f,
  };
  agent_composer_ui_state_.use_intervention = false;
  agent_composer_ui_state_.use_reflector = false;
  agent_composer_defaults_initialized_ = true;
}

void InteractUiRuntime::SyncCustomInterventionUiState() {
  if (!agent_manager_.has_agents()) {
    active_custom_ui_slots_.clear();
    return;
  }

  std::vector<ActiveCustomInterventionUiSlot> updated_slots;
  for (size_t agent_index = 0; agent_index < agent_manager_.agent_count(); ++agent_index) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
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

void InteractUiRuntime::DrawCustomInterventionUi(
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

  std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
  if (!managed_agent) {
    ImGui::TextUnformatted("Managed agent is unavailable.");
    return;
  }
  agent::AgentAlgorithmRuntimeState* runtime_state = managed_agent->algorithm_runtime_state(algorithm_index);

  const AgentInterventionUiContext context{
    .agent = managed_agent.get(),
    .mesh = &mesh_,
    .input = &runtime_environment_.input(),
    .mouse_pixel = runtime_environment_.MousePosition(),
    .dt_seconds = frame_dt_,
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

void InteractUiRuntime::DrawAgentComposerUi() {
  InitializeAgentComposerDefaults();

  ImGui::SeparatorText("Create Agent");
  ImGui::InputText("Agent Name", agent_composer_ui_state_.agent_name.data(), agent_composer_ui_state_.agent_name.size());
  ImGui::Checkbox("Attach temporary_test_line_motion", &agent_composer_ui_state_.attach_temporary_test_line_motion);
  ImGui::InputText("Algorithm Name", agent_composer_ui_state_.algorithm_name.data(), agent_composer_ui_state_.algorithm_name.size());
  ImGui::InputText("Manifest Name", agent_composer_ui_state_.manifest_name.data(), agent_composer_ui_state_.manifest_name.size());
  ImGui::Checkbox("Use Intervention", &agent_composer_ui_state_.use_intervention);
  ImGui::Checkbox("Use Reflector", &agent_composer_ui_state_.use_reflector);

  const std::string requested_algorithm_name = _TrimCopy(agent_composer_ui_state_.algorithm_name.data());
  std::string error_message;
  agent::AgentAlgorithmCodecGroup reflected_group{};
  const bool has_reflected_group =
    !requested_algorithm_name.empty() &&
    codec::CreateAlgorithmCodecGroupByName(requested_algorithm_name, &reflected_group, &error_message);
  agent::AlgorithmResourceReflection resource_reflection{};
  agent::AlgorithmDescriptorReflection descriptor_reflection{};
  const bool has_resource_reflection =
    has_reflected_group &&
    reflected_group.decomposer &&
    reflected_group.decomposer->ReflectRequiredResources(
      reflected_group.algorithm_profile,
      &resource_reflection);
  const bool has_descriptor_reflection =
    has_reflected_group &&
    reflected_group.decomposer &&
    reflected_group.decomposer->ReflectDescriptorBindings(
      reflected_group.algorithm_profile,
      &descriptor_reflection);

  if (has_resource_reflection && resource_reflection.valid) {
    ImGui::SeparatorText("Decomposer Resources");
    for (const agent::AlgorithmResourceReflection::RequiredResource& resource : resource_reflection.required_resources) {
      const std::string resource_label =
        resource.resource_name + " (" + resource.resource_kind + (resource.required ? ", required)" : ", optional)");
      ImGui::TextUnformatted(resource_label.c_str());
      if (resource.resource_kind == "mesh") {
        ImGui::InputText(
          "Mesh Resource Path",
          agent_composer_ui_state_.mesh_resource_path.data(),
          agent_composer_ui_state_.mesh_resource_path.size());
        if (ImGui::Button("Load Mesh Resource")) {
          const std::string mesh_path = _TrimCopy(agent_composer_ui_state_.mesh_resource_path.data());
          if (_LoadtemporaryTestMeshPlaceholder(
                mesh_path,
                &agent_composer_ui_state_.loaded_mesh_resource,
                &error_message)) {
            agent_composer_ui_state_.has_loaded_mesh_resource = true;
            ui_status_message_ = "Loaded mesh resource for decomposer.";
          } else {
            agent_composer_ui_state_.has_loaded_mesh_resource = false;
            ui_status_message_ = error_message;
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Empty Mesh File")) {
          const std::string mesh_path = std::string(PROJECT_DATA_ROOT) + "/temporary_test_empty_mesh.obj";
          _CopyTextToBuffer(mesh_path.c_str(), &agent_composer_ui_state_.mesh_resource_path);
          if (_LoadtemporaryTestMeshPlaceholder(
                mesh_path,
                &agent_composer_ui_state_.loaded_mesh_resource,
                &error_message)) {
            agent_composer_ui_state_.has_loaded_mesh_resource = true;
            ui_status_message_ = "Loaded temporary test empty mesh resource.";
          } else {
            agent_composer_ui_state_.has_loaded_mesh_resource = false;
            ui_status_message_ = error_message;
          }
        }
        ImGui::Text(
          "Mesh resource status: %s",
          agent_composer_ui_state_.has_loaded_mesh_resource ? "loaded" : "not loaded");
      }
    }
  }

  if (has_descriptor_reflection && descriptor_reflection.valid) {
    ImGui::SeparatorText("Decomposer Descriptors");
    for (size_t i = 0; i < descriptor_reflection.descriptor_slots.size() &&
         i < agent_composer_ui_state_.descriptor_scalar_values.size(); ++i) {
      const agent::AlgorithmDescriptorReflection::DescriptorSlot& slot =
        descriptor_reflection.descriptor_slots[i];
      const std::string label =
        slot.descriptor_name + " -> " + slot.container_name + "[" + std::to_string(slot.array_index) + "]";
      ImGui::InputFloat(label.c_str(), &agent_composer_ui_state_.descriptor_scalar_values[i], 0.1f, 1.0f, "%.3f");
    }
  }

  if (agent_composer_ui_state_.use_intervention) {
    ImGui::TextColored(
      ImVec4(1.0f, 0.65f, 0.35f, 1.0f),
      "Intervention is requested, but this validation algorithm does not provide one yet.");
  }
  if (agent_composer_ui_state_.use_reflector) {
    ImGui::TextColored(
      ImVec4(1.0f, 0.65f, 0.35f, 1.0f),
      "Reflector is requested, but there is no runtime reflector adapter yet.");
  }

  if (!ImGui::Button("Create Agent")) {
    return;
  }

  if (!agent_composer_ui_state_.attach_temporary_test_line_motion) {
    ui_status_message_ = "No algorithm is attached. Agent creation was skipped.";
    return;
  }
  if (!has_reflected_group || !reflected_group.decomposer) {
    ui_status_message_ = error_message.empty()
      ? "Failed to build the reflected decomposer group."
      : error_message;
    return;
  }
  if (has_resource_reflection && resource_reflection.valid) {
    for (const agent::AlgorithmResourceReflection::RequiredResource& resource : resource_reflection.required_resources) {
      if (resource.required && resource.resource_kind == "mesh" &&
          !agent_composer_ui_state_.has_loaded_mesh_resource) {
        ui_status_message_ = "Required mesh resource is not loaded yet.";
        return;
      }
    }
  }
  if (!has_descriptor_reflection || !descriptor_reflection.valid) {
    ui_status_message_ = "Descriptor reflection is unavailable for the selected algorithm.";
    return;
  }
  if (agent_composer_ui_state_.use_intervention) {
    ui_status_message_ = "Intervention creation is not available for temporary_test_line_motion yet.";
    return;
  }
  if (agent_composer_ui_state_.use_reflector) {
    ui_status_message_ = "Reflector attachment is not available until a runtime reflector adapter exists.";
    return;
  }

  agent::AgentAlgorithmCodecGroup algorithm_group{};
  if (!codec::CreateAlgorithmCodecGroupByName(requested_algorithm_name, &algorithm_group, &error_message)) {
    ui_status_message_ = error_message.empty()
      ? "Failed to build the validation algorithm group."
      : error_message;
    return;
  }

  const std::string algorithm_name = requested_algorithm_name;
  const std::string manifest_name = _TrimCopy(agent_composer_ui_state_.manifest_name.data());
  const std::string agent_name = _TrimCopy(agent_composer_ui_state_.agent_name.data());

  if (algorithm_name.empty()) {
    ui_status_message_ = "Algorithm name must not be empty.";
    return;
  }
  if (manifest_name.empty()) {
    ui_status_message_ = "Manifest name must not be empty.";
    return;
  }

  algorithm_group.algorithm_profile.algorithm_name = algorithm_name;
  algorithm_group.algorithm_profile.container_manifest_name = manifest_name;
  algorithm_group.resource_bindings.clear();
  algorithm_group.descriptor_values.clear();

  if (has_resource_reflection && resource_reflection.valid) {
    for (const agent::AlgorithmResourceReflection::RequiredResource& resource : resource_reflection.required_resources) {
      agent::AlgorithmResourceBinding binding{};
      binding.resource_name = resource.resource_name;
      binding.resource_kind = resource.resource_kind;
      if (resource.resource_kind == "mesh" && agent_composer_ui_state_.has_loaded_mesh_resource) {
        binding.source_path = _TrimCopy(agent_composer_ui_state_.mesh_resource_path.data());
        binding.mesh = agent_composer_ui_state_.loaded_mesh_resource;
        binding.has_mesh = true;
      }
      algorithm_group.resource_bindings.push_back(std::move(binding));
    }
  }
  for (size_t i = 0; i < descriptor_reflection.descriptor_slots.size() &&
       i < agent_composer_ui_state_.descriptor_scalar_values.size(); ++i) {
    algorithm_group.descriptor_values.push_back(agent::AlgorithmDescriptorValue{
      .descriptor_name = descriptor_reflection.descriptor_slots[i].descriptor_name,
      .scalar_value = agent_composer_ui_state_.descriptor_scalar_values[i],
    });
  }

  agent::AgentInitConfig agent_config{};
  agent_config.agent_name = agent_name.empty() ? "unnamed_agent" : agent_name;
  agent_config.algorithm_codec_groups.push_back(std::move(algorithm_group));

  AgentCreateSpec create_spec{
    .agent_config = std::move(agent_config),
  };
  if (!CreateAgent(std::move(create_spec))) {
    if (ui_status_message_.empty()) {
      ui_status_message_ = "Agent creation failed.";
    }
    return;
  }
}

void InteractUiRuntime::DrawAgentBindingUi() {
  DrawAgentComposerUi();
  ImGui::Separator();

  ImGui::Text("Managed agents: %zu", agent_manager_.agent_count());
  if (agent_manager_.has_agents()) {
    if (ImGui::Button("Clear All Agents")) {
      agent_manager_.ClearAgents();
      managed_agent_ui_bindings_.clear();
      active_custom_ui_slots_.clear();
      ui_status_message_ = "Cleared all managed agents.";
    }
  }

  if (!agent_manager_.has_agents()) {
    ImGui::TextUnformatted("No managed agents.");
    return;
  }

  for (size_t agent_index = 0; agent_index < agent_manager_.agent_count(); ++agent_index) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
    if (!managed_agent) {
      continue;
    }

    const std::string agent_header = managed_agent->agent_name().empty()
      ? ("Agent #" + std::to_string(agent_index))
      : ("Agent #" + std::to_string(agent_index) + "  " + managed_agent->agent_name());
    ImGui::SeparatorText(agent_header.c_str());
    if (ImGui::Button(("Destroy Agent##" + std::to_string(agent_index)).c_str())) {
      agent_manager_.DestroyAgent(agent_index);
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
      active_custom_ui_slots_.clear();
      ui_status_message_ = "Destroyed managed agent.";
      break;
    }

    ImGui::Text("Algorithms: %zu", managed_agent->algorithm_count());
    for (size_t algorithm_index = 0; algorithm_index < managed_agent->algorithm_count(); ++algorithm_index) {
      const agent::AgentAlgorithmCodecGroup* group = managed_agent->algorithm_codec_group(algorithm_index);
      if (!group) {
        continue;
      }
      const std::string manifest_name = ResolveAlgorithmManifestName(group->algorithm_profile);
      const std::string algorithm_name = group->algorithm_profile.algorithm_name.empty()
        ? ("<algorithm " + std::to_string(algorithm_index) + ">")
        : group->algorithm_profile.algorithm_name;
      ImGui::BulletText(
        "#%zu %s  [manifest: %s]",
        algorithm_index,
        algorithm_name.c_str(),
        manifest_name.empty() ? "<none>" : manifest_name.c_str());
    }
  }

  SyncCustomInterventionUiState();
  for (ActiveCustomInterventionUiSlot& slot : active_custom_ui_slots_) {
    const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(slot.agent_index);
    const agent::AgentAlgorithmCodecGroup* group =
      managed_agent ? managed_agent->algorithm_codec_group(slot.algorithm_index) : nullptr;
    if (!group || !slot.hook) {
      continue;
    }
    ImGui::Spacing();
    const char* custom_title = slot.hook->title();
    ImGui::SeparatorText(custom_title ? custom_title : "Custom Intervention UI");
    DrawCustomInterventionUi(slot.agent_index, slot.algorithm_index, *group, &slot);
  }
}

void InteractUiRuntime::DrawMeshPreviewUi() {
  ImGui::SeparatorText("temporaryTest Mesh Preview");

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const ImVec2 canvas_size{
    std::max(available.x, 320.0f),
    std::max(std::min(available.y, 260.0f), 220.0f),
  };

  const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(
    canvas_origin,
    ImVec2(canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y),
    IM_COL32(18, 22, 29, 255),
    8.0f);
  draw_list->AddRect(
    canvas_origin,
    ImVec2(canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y),
    IM_COL32(120, 140, 160, 255),
    8.0f,
    0,
    1.5f);

  if (!mesh_.positions.empty()) {
    const ImVec2 point = _ProjectPreviewPoint(canvas_origin, canvas_size, mesh_.positions.front());
    draw_list->AddCircleFilled(point, 8.0f, IM_COL32(255, 214, 102, 255));
    draw_list->AddCircle(point, 12.0f, IM_COL32(255, 240, 200, 255), 0, 2.0f);
  }

  ImGui::InvisibleButton("temporaryTest_mesh_preview_canvas", canvas_size);

  if (!mesh_.positions.empty()) {
    ImGui::Text(
      "Point position: (%.2f, %.2f, %.2f)",
      mesh_.positions.front().x,
      mesh_.positions.front().y,
      mesh_.positions.front().z);
  } else {
    ImGui::TextUnformatted("No preview point.");
  }
}

void InteractUiRuntime::DrawInteractUi() {
  const InputState& input = runtime_environment_.input();
  const Vec2 mouse_position = runtime_environment_.MousePosition();
  agent_manager_.Tick(mesh_, input, mouse_position, frame_dt_);

  ImGui::Begin("Interact & UI");
  ImGui::Text("Status: %s", ui_status_message_.empty() ? "ready" : ui_status_message_.c_str());
  ImGui::Text("Mesh: %zu vertices, %zu triangles", mesh_.positions.size(), mesh_.triangles.size());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Agent Binding", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawAgentBindingUi();
  }

  ImGui::Separator();
  DrawMeshPreviewUi();
  ImGui::Separator();
  ImGui::Text(
    "Algorithm signal: %s",
    agent_manager_.combined_algorithm_to_agent_signal().pause_requested ? "pause requested" : "idle");
  ImGui::End();
}

bool InteractUiRuntime::Init(const Mesh& mesh, const char* window_title, int width, int height) {
  mesh_ = mesh;
  InitializeAgentComposerDefaults();
  if (!runtime_environment_.Init(window_title ? window_title : "Interact & UI", width, height)) {
    return false;
  }
  runtime_environment_.SetDrawCallback([this]() {
    DrawInteractUi();
  });
  ui_status_message_ = mesh_.positions.empty() ? "Mesh is empty." : "Mesh is preloaded.";
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool InteractUiRuntime::CreateAgent(
  AgentCreateSpec spec,
  std::vector<AgentInterventionUiBinding> ui_bindings) {
  size_t created_agent_index = 0u;
  if (!agent_manager_.CreateAgent(std::move(spec), &created_agent_index)) {
    ui_status_message_ = "Failed to create managed agent.";
    return false;
  }
  managed_agent_ui_bindings_.push_back(ManagedAgentInterventionUiBindings{
    .agent_index = created_agent_index,
    .bindings = std::move(ui_bindings),
  });
  active_custom_ui_slots_.clear();
  ui_status_message_ = "Agent created and registered in agent manager.";
  return true;
}

bool InteractUiRuntime::Tick() {
  if (!runtime_environment_.has_window()) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;
  return runtime_environment_.Tick();
}

void InteractUiRuntime::Destroy() {
  agent_manager_.Destroy();
  runtime_environment_.Destroy();
  mesh_ = Mesh{};
  ui_status_message_.clear();
  managed_agent_ui_bindings_.clear();
  active_custom_ui_slots_.clear();
}

}  // namespace interact_ui
