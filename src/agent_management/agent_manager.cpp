#include "agent_manager.h"

#include "algorithm_management/algorithm_manager.h"

#define AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "agent_management/agent_ticker.h"
#undef AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

#include "codec/codec_protocol.h"

#include <memory>
#include <chrono>
#include <unordered_map>
#include <utility>

namespace agent_management {

namespace {

struct BuiltAlgorithmMount {
  agent::AgentAlgorithmCodecGroup group{};
  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set{};
  std::string error_message;
  bool ok{false};
};

std::string _StandardLayoutKey(const algorithm::AlgorithmContainerSet& container_set) {
  if (container_set.standard_layout.enabled()) {
    return container_set.standard_layout.layout_name;
  }
  return container_set.algorithm_name;
}

BuiltAlgorithmMount _BuildAlgorithmMount(
  const std::string& algorithm_name,
  const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
  agent::AlgorithmMountMode mount_mode,
  agent::AlgorithmExecutionPreference execution_preference,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets) {
  BuiltAlgorithmMount result{};

  std::string error_message;
  agent::AgentAlgorithmCodecGroup group{};
  if (!codec::CreateAlgorithmCodecGroupByName(algorithm_name, &group, &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to create algorithm codec group for '" + algorithm_name + "'.")
      : std::move(error_message);
    return result;
  }

  group.algorithm_profile.algorithm_name = algorithm_name;
  group.resource_bindings = resource_bindings;
  group.descriptor_values = descriptor_values;
  group.mount_mode = mount_mode;
  group.execution_preference = execution_preference;

  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set_handle;
  algorithm::AlgorithmContainerSet container_set{};
  if (!algorithm_management::CreateAlgorithmContainersFromManifestName(
        group.algorithm_profile.container_manifest_name,
        &container_set,
        &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to create containers for '" + algorithm_name + "'.")
      : std::move(error_message);
    return result;
  }

  const bool use_shared_standard_container =
    mount_mode == agent::AlgorithmMountMode::StandardContainer &&
    container_set.standard_layout.enabled() &&
    standard_shared_container_sets;

  if (use_shared_standard_container) {
    const std::string shared_key = _StandardLayoutKey(container_set);
    if (!shared_key.empty()) {
      auto found = standard_shared_container_sets->find(shared_key);
      if (found != standard_shared_container_sets->end() && found->second) {
        container_set_handle = found->second;
      } else {
        container_set_handle = std::make_shared<algorithm::AlgorithmContainerSet>(std::move(container_set));
        (*standard_shared_container_sets)[shared_key] = container_set_handle;
      }
    }
  }

  if (!container_set_handle) {
    container_set_handle = std::make_shared<algorithm::AlgorithmContainerSet>(std::move(container_set));
  }

  if (!group.decomposer) {
    result.error_message = "Algorithm decomposer is unavailable for '" + algorithm_name + "'.";
    return result;
  }

  if (!group.decomposer->Decompose(
        group.algorithm_profile,
        group.resource_bindings,
        group.descriptor_values,
        container_set_handle.get(),
        &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to decompose algorithm inputs for '" + algorithm_name + "'.")
      : std::move(error_message);
    return result;
  }

  group.shared_container_set = container_set_handle;
  result.group = std::move(group);
  result.container_set = std::move(container_set_handle);
  result.ok = true;
  return result;
}

}  // namespace

struct AgentManager::ManagedAgentEntry {
  std::shared_ptr<agent::Agent> agent{};
  AgentTicker ticker{};
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>> standard_shared_container_sets{};
  uint32_t limit_fps_flag{120u};
  bool one_shot_tick_consumed{false};
  std::chrono::steady_clock::time_point last_tick_time{};
  bool last_tick_time_valid{false};

  void ResetTickBudget() {
    one_shot_tick_consumed = false;
    last_tick_time = {};
    last_tick_time_valid = false;
  }
};

AgentManager::AgentManager() = default;

AgentManager::~AgentManager() {
  Destroy();
}

bool AgentManager::CreateAgent(AgentCreateSpec spec, size_t* out_agent_index) {
  auto agent_instance = std::make_shared<agent::Agent>();
  agent::AgentInitConfig agent_config{};
  agent_config.agent_name = std::move(spec.agent_name);
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>> standard_shared_container_sets;
  for (const AgentCreateSpec::AlgorithmMountSpec& mount_spec : spec.algorithm_mount_specs) {
    BuiltAlgorithmMount built_mount = _BuildAlgorithmMount(
      mount_spec.algorithm_name,
      mount_spec.resource_bindings,
      mount_spec.descriptor_values,
      mount_spec.mount_mode,
      agent::AlgorithmExecutionPreference::Gpu,
      &standard_shared_container_sets);
    if (!built_mount.ok) {
      return false;
    }
    agent_config.algorithm_codec_groups.push_back(std::move(built_mount.group));
  }

  if (!agent::agent_init(agent_instance.get(), std::move(agent_config))) {
    return false;
  }

  auto managed_agent = std::make_shared<ManagedAgentEntry>();
  managed_agent->agent = std::move(agent_instance);
  managed_agent->ticker.Init(managed_agent->agent);
  managed_agent->standard_shared_container_sets = std::move(standard_shared_container_sets);
  managed_agent->limit_fps_flag = spec.limit_fps_flag;
  managed_agent->ResetTickBudget();

  managed_agents_.push_back(std::move(managed_agent));
  if (out_agent_index) {
    *out_agent_index = managed_agents_.size() - 1u;
  }
  return true;
}

bool AgentManager::DestroyAgent(size_t agent_index) {
  if (agent_index >= managed_agents_.size()) {
    return false;
  }

  managed_agents_[agent_index]->ticker.Destroy();
  managed_agents_.erase(managed_agents_.begin() + static_cast<std::ptrdiff_t>(agent_index));
  return true;
}

void AgentManager::ClearAgents() {
  for (std::shared_ptr<ManagedAgentEntry>& managed_agent : managed_agents_) {
    if (managed_agent) {
      managed_agent->ticker.Destroy();
    }
  }
  managed_agents_.clear();
  combined_algorithm_to_agent_signal_ = {};
}

bool AgentManager::Tick(const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  combined_algorithm_to_agent_signal_ = {};
  const auto now = std::chrono::steady_clock::now();
  for (std::shared_ptr<ManagedAgentEntry>& managed_agent : managed_agents_) {
    if (!managed_agent) {
      continue;
    }
    if (managed_agent->limit_fps_flag == 0u) {
      if (managed_agent->one_shot_tick_consumed) {
        continue;
      }
    } else {
      const float min_dt_seconds = 1.0f / static_cast<float>(managed_agent->limit_fps_flag);
      if (managed_agent->last_tick_time_valid) {
        const float elapsed_seconds =
          std::chrono::duration<float>(now - managed_agent->last_tick_time).count();
        if (elapsed_seconds < min_dt_seconds) {
          continue;
        }
      }
    }

    managed_agent->ticker.Tick(input, mouse_pixel, dt_seconds);
    if (managed_agent->limit_fps_flag == 0u) {
      managed_agent->one_shot_tick_consumed = true;
    } else {
      managed_agent->last_tick_time = now;
      managed_agent->last_tick_time_valid = true;
    }

    const AlgorithmToAgentSignal& signal = managed_agent->ticker.algorithm_to_agent_signal();
    combined_algorithm_to_agent_signal_.intervention_applied =
      combined_algorithm_to_agent_signal_.intervention_applied || signal.intervention_applied;
    combined_algorithm_to_agent_signal_.pause_requested =
      combined_algorithm_to_agent_signal_.pause_requested || signal.pause_requested;
    combined_algorithm_to_agent_signal_.stop_requested =
      combined_algorithm_to_agent_signal_.stop_requested || signal.stop_requested;
    combined_algorithm_to_agent_signal_.intervention_needed =
      combined_algorithm_to_agent_signal_.intervention_needed || signal.intervention_needed;
    combined_algorithm_to_agent_signal_.reflection_collection_requested =
      combined_algorithm_to_agent_signal_.reflection_collection_requested ||
      signal.reflection_collection_requested;
  }
  return true;
}

bool AgentManager::AttachAlgorithmToAgent(
  size_t agent_index,
  const std::string& algorithm_name,
  const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  agent::AlgorithmMountMode mount_mode,
  agent::AlgorithmExecutionPreference execution_preference) {
  auto set_error = [&](const std::string& message) {
    if (out_error_message) {
      *out_error_message = message;
    }
  };

  if (agent_index >= managed_agents_.size() || !managed_agents_[agent_index] || !managed_agents_[agent_index]->agent) {
    set_error("Selected agent is unavailable.");
    return false;
  }

  BuiltAlgorithmMount built_mount = _BuildAlgorithmMount(
    algorithm_name,
    resource_bindings,
    descriptor_values,
    mount_mode,
    execution_preference,
    &managed_agents_[agent_index]->standard_shared_container_sets);
  if (!built_mount.ok) {
    set_error(built_mount.error_message);
    return false;
  }

  size_t algorithm_index = 0u;
  if (!managed_agents_[agent_index]->agent->AppendAlgorithmCodecGroup(std::move(built_mount.group), &algorithm_index)) {
    set_error("Failed to append algorithm group to agent.");
    return false;
  }
  if (!managed_agents_[agent_index]->agent->BeginAlgorithmAssembly(algorithm_index)) {
    managed_agents_[agent_index]->agent->MarkAlgorithmAssemblyFailed(algorithm_index);
    set_error("Failed to begin algorithm assembly.");
    return false;
  }

  agent::AlgorithmObject* algorithm_object = managed_agents_[agent_index]->agent->algorithm_object(algorithm_index);
  if (!algorithm_object) {
    managed_agents_[agent_index]->agent->MarkAlgorithmAssemblyFailed(algorithm_index);
    set_error("Failed to access algorithm object.");
    return false;
  }
  algorithm_object->SetContainerSet(std::move(built_mount.container_set));
  managed_agents_[agent_index]->agent->MarkAlgorithmAssemblyReady(algorithm_index);
  managed_agents_[agent_index]->ResetTickBudget();

  if (out_algorithm_index) {
    *out_algorithm_index = algorithm_index;
  }
  return true;
}

bool AgentManager::DetachAlgorithmFromAgent(
  size_t agent_index,
  size_t algorithm_index,
  std::string* out_error_message) {
  auto set_error = [&](const std::string& message) {
    if (out_error_message) {
      *out_error_message = message;
    }
  };

  if (agent_index >= managed_agents_.size() || !managed_agents_[agent_index] || !managed_agents_[agent_index]->agent) {
    set_error("Selected agent is unavailable.");
    return false;
  }

  const std::shared_ptr<agent::Agent> managed_agent = managed_agents_[agent_index]->agent;
  if (!managed_agent->RemoveAlgorithm(algorithm_index)) {
    set_error("Selected algorithm is unavailable.");
    return false;
  }
  managed_agents_[agent_index]->ResetTickBudget();
  return true;
}

bool AgentManager::CollectAlgorithmReflection(
  size_t agent_index,
  size_t algorithm_index,
  AlgorithmReflectionSnapshot* out_snapshot) const {
  if (!out_snapshot) {
    return false;
  }

  const std::shared_ptr<agent::Agent> managed_agent = agent(agent_index);
  if (!managed_agent) {
    return false;
  }

  const agent::AgentAlgorithmCodecGroup* group = managed_agent->algorithm_codec_group(algorithm_index);
  if (!group) {
    return false;
  }

  const agent::AlgorithmReflectionSnapshot* runtime_snapshot =
    managed_agent->algorithm_runtime_state(algorithm_index)
      ? &managed_agent->algorithm_runtime_state(algorithm_index)->reflection_snapshot
      : nullptr;
  if (!runtime_snapshot || !runtime_snapshot->valid) {
    agent::AlgorithmReflectionSnapshot collected_snapshot{};
    if (!managed_agent->CollectAlgorithmReflection(algorithm_index, &collected_snapshot) ||
        !collected_snapshot.valid) {
      return false;
    }

    out_snapshot->Clear();
    out_snapshot->agent_index = agent_index;
    out_snapshot->algorithm_index = algorithm_index;
    out_snapshot->agent_name = managed_agent->agent_name();
    out_snapshot->algorithm_name = group->algorithm_profile.algorithm_name;
    out_snapshot->valid = true;
    for (const agent::AlgorithmReflectionValue& value : collected_snapshot.variables) {
      out_snapshot->variables.push_back(AlgorithmReflectionRecord{
        .reflection_object_name = value.reflection_object_name,
        .container_name = value.container_name,
        .filter_name = value.filter_name,
        .storage_kind = value.storage_kind,
        .bytes = value.bytes,
      });
    }
    for (const agent::AlgorithmReflectionValue& value : collected_snapshot.variable_arrays) {
      out_snapshot->variable_arrays.push_back(AlgorithmReflectionRecord{
        .reflection_object_name = value.reflection_object_name,
        .container_name = value.container_name,
        .filter_name = value.filter_name,
        .storage_kind = value.storage_kind,
        .bytes = value.bytes,
      });
    }
    return true;
  }

  out_snapshot->Clear();
  out_snapshot->agent_index = agent_index;
  out_snapshot->algorithm_index = algorithm_index;
  out_snapshot->agent_name = managed_agent->agent_name();
  out_snapshot->algorithm_name = group->algorithm_profile.algorithm_name;
  out_snapshot->valid = runtime_snapshot->valid;
  for (const agent::AlgorithmReflectionValue& value : runtime_snapshot->variables) {
    out_snapshot->variables.push_back(AlgorithmReflectionRecord{
      .reflection_object_name = value.reflection_object_name,
      .container_name = value.container_name,
      .filter_name = value.filter_name,
      .storage_kind = value.storage_kind,
      .bytes = value.bytes,
    });
  }
  for (const agent::AlgorithmReflectionValue& value : runtime_snapshot->variable_arrays) {
    out_snapshot->variable_arrays.push_back(AlgorithmReflectionRecord{
      .reflection_object_name = value.reflection_object_name,
      .container_name = value.container_name,
      .filter_name = value.filter_name,
      .storage_kind = value.storage_kind,
      .bytes = value.bytes,
    });
  }
  return out_snapshot->valid;
}

void AgentManager::Destroy() {
  ClearAgents();
}

size_t AgentManager::agent_count() const {
  return managed_agents_.size();
}

bool AgentManager::has_agents() const {
  return !managed_agents_.empty();
}

std::shared_ptr<agent::Agent> AgentManager::agent(size_t index) const {
  if (index >= managed_agents_.size() || !managed_agents_[index]) {
    return {};
  }
  return managed_agents_[index]->agent;
}

}  // namespace agent_management
