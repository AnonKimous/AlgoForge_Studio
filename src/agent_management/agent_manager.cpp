#include "agent_manager.h"

#define AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "agent_management/agent_ticker.h"
#undef AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

#include <cassert>
#include <memory>
#include <chrono>
#include <utility>

namespace agent_management {

struct AgentManager::ManagedAgentEntry {
  std::shared_ptr<agent::Agent> agent{};
  AgentTicker ticker{};
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
  if (!agent_instance->Init(std::move(agent_config))) {
    return false;
  }
  for (const AgentCreateSpec::AlgorithmMountSpec& mount_spec : spec.algorithm_mount_specs) {
    if (!agent_instance->MountAlgorithm(
          mount_spec.algorithm_name,
          mount_spec.resource_bindings,
          mount_spec.descriptor_values,
          nullptr,
          nullptr,
          mount_spec.mount_mode,
          agent::AlgorithmExecutionPreference::Gpu)) {
      agent_instance->Destroy();
      return false;
    }
  }

  auto managed_agent = std::make_shared<ManagedAgentEntry>();
  managed_agent->agent = std::move(agent_instance);
  managed_agent->ticker.Init(managed_agent->agent);
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

  if (managed_agents_[agent_index] && managed_agents_[agent_index]->agent) {
    managed_agents_[agent_index]->agent->Destroy();
  }
  managed_agents_[agent_index]->ticker.Destroy();
  managed_agents_.erase(managed_agents_.begin() + static_cast<std::ptrdiff_t>(agent_index));
  return true;
}

void AgentManager::ClearAgents() {
  for (std::shared_ptr<ManagedAgentEntry>& managed_agent : managed_agents_) {
    if (managed_agent) {
      if (managed_agent->agent) {
        managed_agent->agent->Destroy();
      }
      managed_agent->ticker.Destroy();
    }
  }
  managed_agents_.clear();
  combined_algorithm_to_agent_signal_ = {};
  tick_enabled_ = false;
}

void AgentManager::StartTicking() {
  tick_enabled_ = true;
}

void AgentManager::PauseTicking() {
  tick_enabled_ = false;
}

bool AgentManager::Tick(const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  combined_algorithm_to_agent_signal_ = {};
  if (!tick_enabled_) {
    return true;
  }
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
    combined_algorithm_to_agent_signal_.control_bits |= signal.control_bits;
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

  size_t algorithm_index = 0u;
  if (!managed_agents_[agent_index]->agent->MountAlgorithm(
    algorithm_name,
    resource_bindings,
    descriptor_values,
    &algorithm_index,
    out_error_message,
    mount_mode,
    execution_preference)) {
    assert(false && "Failed to mount algorithm.");
    if (out_error_message && out_error_message->empty()) {
      set_error("Failed to mount algorithm.");
    }
    return false;
  }
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

  const agent::AlgorithmObject* object = managed_agent->algorithm_object(algorithm_index);
  if (!object) {
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
    out_snapshot->algorithm_name = object->algorithm_profile.algorithm_name;
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
  out_snapshot->algorithm_name = object->algorithm_profile.algorithm_name;
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
