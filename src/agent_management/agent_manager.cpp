#include "agent_manager.h"

#define AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "agent_management/agent_ticker.h"
#undef AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

#include "codec/codec_manager.h"

#include <memory>
#include <utility>

namespace agent_management {

struct AgentManager::ManagedAgentEntry {
  std::shared_ptr<agent::Agent> agent{};
  AgentTicker ticker{};
};

AgentManager::AgentManager() = default;

AgentManager::~AgentManager() {
  Destroy();
}

bool AgentManager::CreateAgent(AgentCreateSpec spec, size_t* out_agent_index) {
  auto agent_instance = std::make_shared<agent::Agent>();
  agent::AgentInitConfig agent_config{};
  agent_config.agent_name = std::move(spec.agent_name);
  for (const AgentCreateSpec::AlgorithmMountSpec& mount_spec : spec.algorithm_mount_specs) {
    agent::AgentAlgorithmCodecGroup group{};
    std::string error_message;
    if (!codec::CreateAlgorithmCodecGroupByName(mount_spec.algorithm_name, &group, &error_message)) {
      return false;
    }
    group.algorithm_profile.algorithm_name = mount_spec.algorithm_name;
    group.resource_bindings = mount_spec.resource_bindings;
    group.descriptor_values = mount_spec.descriptor_values;
    agent_config.algorithm_codec_groups.push_back(std::move(group));
  }

  if (!agent::agent_init(agent_instance.get(), std::move(agent_config))) {
    return false;
  }

  auto managed_agent = std::make_shared<ManagedAgentEntry>();
  managed_agent->agent = std::move(agent_instance);
  managed_agent->ticker.Init(managed_agent->agent);

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
  for (std::shared_ptr<ManagedAgentEntry>& managed_agent : managed_agents_) {
    if (!managed_agent) {
      continue;
    }
    managed_agent->ticker.Tick(input, mouse_pixel, dt_seconds);
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

  agent::AgentAlgorithmCodecGroup group{};
  std::string error_message;
  if (!codec::CreateAlgorithmCodecGroupByName(algorithm_name, &group, &error_message)) {
    set_error(error_message.empty()
      ? ("Failed to create algorithm codec group for '" + algorithm_name + "'.")
      : error_message);
    return false;
  }

  group.algorithm_profile.algorithm_name = algorithm_name;
  group.resource_bindings = resource_bindings;
  group.descriptor_values = descriptor_values;

  AlgorithmContainerSet container_set{};
  if (!AlgorithmManager_CreateContainersFromManifestName(
        group.algorithm_profile.container_manifest_name,
        &container_set,
        &error_message)) {
    set_error(error_message.empty()
      ? ("Failed to create containers for '" + algorithm_name + "'.")
      : error_message);
    return false;
  }

  if (!group.decomposer) {
    set_error("Algorithm decomposer is unavailable for '" + algorithm_name + "'.");
    return false;
  }
  if (!group.decomposer->Decompose(
        group.algorithm_profile,
        group.resource_bindings,
        group.descriptor_values,
        &container_set,
        &error_message)) {
    set_error(error_message.empty()
      ? ("Failed to decompose algorithm inputs for '" + algorithm_name + "'.")
      : error_message);
    return false;
  }

  size_t algorithm_index = 0u;
  if (!managed_agents_[agent_index]->agent->AppendAlgorithmCodecGroup(std::move(group), &algorithm_index)) {
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
  *algorithm_object->mutable_container_set() = std::move(container_set);
  managed_agents_[agent_index]->agent->MarkAlgorithmAssemblyReady(algorithm_index);

  if (out_algorithm_index) {
    *out_algorithm_index = algorithm_index;
  }
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
