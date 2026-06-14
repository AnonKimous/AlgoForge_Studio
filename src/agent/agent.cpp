#include "agent/agent.h"

#include "algorithm_management/algorithm_manager.h"

#include <algorithm>
#include <utility>

namespace agent {

namespace {

void _CollectDebugState(
  const AlgorithmObject& object,
  AlgorithmPackageDebugState* out_debug_state) {
  if (out_debug_state) {
    *out_debug_state = {};
  }

  if (auto* complex_support = dynamic_cast<IComplexAlgorithmPackageSupport*>(object.reflector.get())) {
    if (out_debug_state) {
      complex_support->CollectDebugState(out_debug_state);
    }
  }
}

bool _CollectReflectionSnapshot(
  const AlgorithmObject& object,
  const AlgorithmContainerSet& container_set,
  AlgorithmReflectionSnapshot* out_snapshot) {
  if (!out_snapshot) {
    return false;
  }

  out_snapshot->Clear();
  out_snapshot->algorithm_name = object.algorithm_profile.algorithm_name;
  if (!object.algorithm_reflector) {
    return false;
  }

  for (const auto& [reflection_object_name, binding] : object.algorithm_reflector->container_bindings_by_reflection_object_name) {
    for (const std::string& container_name : binding.container_names) {
      const AlgorithmContainer* container = FindAlgorithmContainer(container_set, container_name);
      if (!container) {
        continue;
      }

      AlgorithmReflectionValue value{};
      value.reflection_object_name = reflection_object_name;
      value.container_name = container_name;
      value.filter_name = binding.filter_name;
      value.storage_kind = container->storage_kind;
      value.bytes.assign(container->bytes.begin(), container->bytes.end());

      if (container->storage_kind == algorithm::AlgorithmContainerStorageKind::Array) {
        out_snapshot->variable_arrays.push_back(std::move(value));
      } else {
        out_snapshot->variables.push_back(std::move(value));
      }
    }
  }

  out_snapshot->valid = !out_snapshot->variables.empty() || !out_snapshot->variable_arrays.empty();
  return out_snapshot->valid;
}

bool _SignalBlocksTick(
  const AlgorithmObject& object,
  const AgentToAlgorithmSignal& signal) {
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  if (!signal.needs_intervention) {
    return false;
  }
  return !object.intervention || object.intervention->SupportsIntervention();
}

struct BuiltAlgorithmMount {
  AlgorithmObject object{};
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
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  AlgorithmMountMode mount_mode,
  AlgorithmExecutionPreference execution_preference,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets) {
  BuiltAlgorithmMount result{};

  std::string error_message;
  AlgorithmObject object{};
  if (!CreateAlgorithmObjectByName(algorithm_name, &object, &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to create algorithm object for '" + algorithm_name + "'.")
      : std::move(error_message);
    return result;
  }

  object.resource_bindings = resource_bindings;
  object.descriptor_values = descriptor_values;
  object.mount_mode = mount_mode;
  object.execution_preference = execution_preference;

  std::shared_ptr<algorithm::AlgorithmContainerSet> container_set_handle = object.shared_container_set;
  if (!container_set_handle) {
    result.error_message = "Algorithm containers are unavailable for '" + algorithm_name + "'.";
    return result;
  }

  const bool use_shared_standard_container =
    mount_mode == AlgorithmMountMode::StandardContainer &&
    container_set_handle->standard_layout.enabled() &&
    standard_shared_container_sets;
  const bool cache_shared_standard_container = use_shared_standard_container;
  std::string shared_key;

  if (use_shared_standard_container) {
    shared_key = _StandardLayoutKey(*container_set_handle);
    if (!shared_key.empty()) {
      auto found = standard_shared_container_sets->find(shared_key);
      if (found != standard_shared_container_sets->end() && found->second) {
        container_set_handle = found->second;
      }
    }
  }

  if (!algorithm_management::DecomposeAlgorithmObject(
        object,
        object.resource_bindings,
        object.descriptor_values,
        container_set_handle.get(),
        &error_message)) {
    result.error_message = error_message.empty()
      ? ("Failed to decompose algorithm inputs for '" + algorithm_name + "'.")
      : std::move(error_message);
    return result;
  }

  if (cache_shared_standard_container && !shared_key.empty()) {
    (*standard_shared_container_sets)[shared_key] = container_set_handle;
  }

  object.shared_container_set = container_set_handle;
  result.object = std::move(object);
  result.container_set = std::move(container_set_handle);
  result.ok = true;
  return result;
}

}  // namespace

bool CreateAlgorithmObjectByName(
  const std::string& algorithm_name,
  AlgorithmObject* out_group,
  std::string* out_error_message) {
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  return algorithm_management::CreateAlgorithmObjectFromLocation(package_location, out_group, out_error_message);
}

bool Agent::Init(AgentInitConfig config) {
  agent_name_ = std::move(config.agent_name);
  algorithm_objects_ = std::move(config.algorithm_objects);
  algorithm_runtime_states_.clear();
  algorithm_assembly_states_.clear();
  standard_shared_container_sets_.clear();
  algorithm_runtime_states_.reserve(algorithm_objects_.size());
  algorithm_assembly_states_.reserve(algorithm_objects_.size());
  for (const AlgorithmObject& object : algorithm_objects_) {
    AgentAlgorithmRuntimeState runtime_state{};
    runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    algorithm_runtime_states_.push_back(std::move(runtime_state));
    algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);
  }
  initialized_ = true;
  return true;
}

bool Agent::MountAlgorithm(
  const std::string& algorithm_name,
  const std::vector<AlgorithmResourceBinding>& resource_bindings,
  const std::vector<AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_index,
  std::string* out_error_message,
  AlgorithmMountMode mount_mode,
  AlgorithmExecutionPreference execution_preference) {
  auto set_error = [&](const std::string& message) {
    if (out_error_message) {
      *out_error_message = message;
    }
  };

  if (!initialized_) {
    set_error("Agent is not initialized.");
    return false;
  }

  BuiltAlgorithmMount built_mount = _BuildAlgorithmMount(
    algorithm_name,
    resource_bindings,
    descriptor_values,
    mount_mode,
    execution_preference,
    &standard_shared_container_sets_);
  if (!built_mount.ok) {
    set_error(built_mount.error_message);
    return false;
  }

  size_t algorithm_index = 0u;
  if (!AppendAlgorithmObject(std::move(built_mount.object), &algorithm_index)) {
    set_error("Failed to append algorithm object to agent.");
    return false;
  }
  if (!BeginAlgorithmAssembly(algorithm_index)) {
    MarkAlgorithmAssemblyFailed(algorithm_index);
    RemoveAlgorithm(algorithm_index);
    set_error("Failed to begin algorithm assembly.");
    return false;
  }

  AlgorithmObject* mounted_algorithm_object = algorithm_object(algorithm_index);
  if (!mounted_algorithm_object) {
    MarkAlgorithmAssemblyFailed(algorithm_index);
    RemoveAlgorithm(algorithm_index);
    set_error("Failed to access algorithm object.");
    return false;
  }
  mounted_algorithm_object->SetContainerSet(std::move(built_mount.container_set));
  MarkAlgorithmAssemblyReady(algorithm_index);

  if (out_index) {
    *out_index = algorithm_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool Agent::AppendAlgorithmObject(AlgorithmObject object, size_t* out_index) {
  if (!initialized_) {
    return false;
  }

  algorithm_objects_.push_back(std::move(object));
  algorithm_runtime_states_.push_back(AgentAlgorithmRuntimeState{});
  algorithm_runtime_states_.back().algorithm_name = algorithm_objects_.back().algorithm_profile.algorithm_name;
  algorithm_assembly_states_.push_back(AlgorithmAssemblyState::Pending);

  if (out_index) {
    *out_index = algorithm_objects_.size() - 1u;
  }
  return true;
}

bool Agent::RemoveAlgorithm(size_t index) {
  if (!initialized_ || index >= algorithm_objects_.size() || index >= algorithm_runtime_states_.size() ||
      index >= algorithm_assembly_states_.size()) {
    return false;
  }

  algorithm_objects_.erase(algorithm_objects_.begin() + static_cast<std::ptrdiff_t>(index));
  algorithm_runtime_states_.erase(algorithm_runtime_states_.begin() + static_cast<std::ptrdiff_t>(index));
  algorithm_assembly_states_.erase(algorithm_assembly_states_.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

void Agent::RefreshInterventionSignals(const AgentTickContext& context) {
  for (size_t i = 0; i < algorithm_objects_.size(); ++i) {
    if (i >= algorithm_assembly_states_.size() || algorithm_assembly_states_[i] != AlgorithmAssemblyState::Ready) {
      if (i < algorithm_runtime_states_.size()) {
        algorithm_runtime_states_[i].agent_to_algorithm_signal = {};
      }
      continue;
    }
    AgentAlgorithmRuntimeState& runtime_state = algorithm_runtime_states_[i];
    runtime_state.agent_to_algorithm_signal = {};
    if (algorithm_objects_[i].intervention) {
      algorithm_objects_[i].intervention->FillAgentToAlgorithmSignal(
        context,
        &runtime_state.agent_to_algorithm_signal);
    }
  }
}

bool Agent::SubmitAlgorithm(
  const AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  AgentTickResult* out_result) {
  if (!out_result) {
    return false;
  }
  if (!initialized_) {
    return false;
  }

  out_result->algorithm_to_agent_signal = {};
  out_result->algorithm_runtime_states.clear();
  out_result->algorithm_runtime_states.reserve(algorithm_objects_.size());
  algorithm_execution_end_queue_.clear();

  std::vector<AgentAlgorithmRuntimeState> updated_runtime_states(algorithm_objects_.size());

  auto process_index = [&](size_t i) {
    const AlgorithmObject& object = algorithm_objects_[i];
    AgentAlgorithmRuntimeState runtime_state{};
    if (i < algorithm_runtime_states_.size()) {
      runtime_state = algorithm_runtime_states_[i];
    } else {
      runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    }
    runtime_state.algorithm_to_agent_signal = {};
    runtime_state.debug_state = {};
    runtime_state.reflection_snapshot.Clear();

    const bool allow_tick = i < allow_tick_mask.size() ? allow_tick_mask[i] : true;
    const bool is_ready =
      i < algorithm_assembly_states_.size() && algorithm_assembly_states_[i] == AlgorithmAssemblyState::Ready;
    if (allow_tick && is_ready) {
      runtime_state.algorithm_to_agent_signal.intervention_needed =
        runtime_state.agent_to_algorithm_signal.needs_intervention &&
        object.intervention &&
        object.intervention->SupportsIntervention();
      runtime_state.algorithm_to_agent_signal.control_bits = runtime_state.agent_to_algorithm_signal.control_bits;
      bool gpu_tick_executed = false;
      if (object.execution_preference == AlgorithmExecutionPreference::Gpu) {
        if (!object.gpu_symbol) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        } else {
          std::string gpu_error_message;
          gpu_tick_executed = algorithm_management::job_gpu::Execute(
            object,
            algorithm_objects_[i].mutable_container_set(),
            context,
            &gpu_error_message);
          if (!gpu_tick_executed) {
            runtime_state.algorithm_to_agent_signal.stop_requested = true;
          } else {
            algorithm_execution_end_queue_.push_back(i);
          }
        }
      } else {
        if (!object.cpu_symbol || !object.temporaryTest_main_thread_executor) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        } else if (!algorithm_management::job_cpu::Execute(
                     object,
                     context,
                     runtime_state.agent_to_algorithm_signal,
                     algorithm_objects_[i].mutable_container_set(),
                     &runtime_state.algorithm_to_agent_signal,
                     &runtime_state.debug_state)) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        }
      }
      const bool needs_gpu_state_sync =
        gpu_tick_executed &&
        algorithm_objects_[i].container_set() &&
        (
          (object.algorithm_reflector && !object.algorithm_reflector->empty()) ||
          runtime_state.agent_to_algorithm_signal.reflection_collection_requested);
      if (needs_gpu_state_sync) {
        std::string gpu_sync_error_message;
        if (!algorithm_management::job_gpu::Synchronize(
              object,
              algorithm_objects_[i].mutable_container_set(),
              &gpu_sync_error_message)) {
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
        }
      }
      AlgorithmPackageDebugState collected_debug_state{};
      _CollectDebugState(object, &collected_debug_state);
      runtime_state.debug_state.signals.insert(
        runtime_state.debug_state.signals.end(),
        collected_debug_state.signals.begin(),
        collected_debug_state.signals.end());
      const bool has_runtime_reflector = object.algorithm_reflector && !object.algorithm_reflector->empty();
      if (has_runtime_reflector && algorithm_objects_[i].container_set() &&
          _CollectReflectionSnapshot(object, *algorithm_objects_[i].container_set(), &runtime_state.reflection_snapshot)) {
        runtime_state.algorithm_to_agent_signal.reflection_collection_requested = true;
      } else if (runtime_state.agent_to_algorithm_signal.reflection_collection_requested) {
        runtime_state.algorithm_to_agent_signal.reflection_collection_requested = true;
        if (algorithm_objects_[i].container_set() &&
            _CollectReflectionSnapshot(object, *algorithm_objects_[i].container_set(), &runtime_state.reflection_snapshot)) {
          runtime_state.algorithm_to_agent_signal.reflection_collection_requested = true;
        }
      }
      runtime_state.algorithm_to_agent_signal.intervention_applied =
        runtime_state.algorithm_to_agent_signal.intervention_applied ||
        runtime_state.algorithm_to_agent_signal.intervention_needed;
    } else if (is_ready) {
      runtime_state.algorithm_to_agent_signal.pause_requested =
        runtime_state.agent_to_algorithm_signal.pause_requested;
      runtime_state.algorithm_to_agent_signal.stop_requested =
        runtime_state.agent_to_algorithm_signal.stop_requested;
      runtime_state.algorithm_to_agent_signal.intervention_needed =
        _SignalBlocksTick(object, runtime_state.agent_to_algorithm_signal) &&
        runtime_state.agent_to_algorithm_signal.needs_intervention;
      runtime_state.algorithm_to_agent_signal.reflection_collection_requested =
        runtime_state.agent_to_algorithm_signal.reflection_collection_requested;
      runtime_state.algorithm_to_agent_signal.control_bits = runtime_state.agent_to_algorithm_signal.control_bits;
    } else {
      runtime_state.agent_to_algorithm_signal = {};
      runtime_state.algorithm_to_agent_signal = {};
    }

    out_result->algorithm_to_agent_signal.intervention_applied =
      out_result->algorithm_to_agent_signal.intervention_applied ||
      runtime_state.algorithm_to_agent_signal.intervention_applied;
    out_result->algorithm_to_agent_signal.pause_requested =
      out_result->algorithm_to_agent_signal.pause_requested ||
      runtime_state.algorithm_to_agent_signal.pause_requested;
    out_result->algorithm_to_agent_signal.stop_requested =
      out_result->algorithm_to_agent_signal.stop_requested ||
      runtime_state.algorithm_to_agent_signal.stop_requested;
    out_result->algorithm_to_agent_signal.intervention_needed =
      out_result->algorithm_to_agent_signal.intervention_needed ||
      runtime_state.algorithm_to_agent_signal.intervention_needed;
    out_result->algorithm_to_agent_signal.reflection_collection_requested =
      out_result->algorithm_to_agent_signal.reflection_collection_requested ||
      runtime_state.algorithm_to_agent_signal.reflection_collection_requested;
    out_result->algorithm_to_agent_signal.control_bits |= runtime_state.algorithm_to_agent_signal.control_bits;

    updated_runtime_states[i] = std::move(runtime_state);
  };

  for (size_t index = 0; index < algorithm_objects_.size(); ++index) {
    process_index(index);
  }

  for (const AgentAlgorithmRuntimeState& runtime_state : updated_runtime_states) {
    out_result->algorithm_runtime_states.push_back(runtime_state);
  }

  algorithm_execution_end_queue_.clear();
  algorithm_runtime_states_ = std::move(updated_runtime_states);
  return true;
}

bool Agent::Tick(
  const AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  AgentTickResult* out_result) {
  return SubmitAlgorithm(context, allow_tick_mask, out_result);
}

bool Agent::BeginAlgorithmAssembly(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return false;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Assembling;
  return true;
}

void Agent::MarkAlgorithmAssemblyReady(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Ready;
}

void Agent::MarkAlgorithmAssemblyFailed(size_t index) {
  if (index >= algorithm_assembly_states_.size()) {
    return;
  }
  algorithm_assembly_states_[index] = AlgorithmAssemblyState::Failed;
}

bool Agent::GetAlgorithmAssemblySlot(size_t index, AlgorithmAssemblySlot* out_slot) {
  if (!out_slot || index >= algorithm_objects_.size() || index >= algorithm_assembly_states_.size()) {
    return false;
  }

  out_slot->index = index;
  out_slot->algorithm_object = &algorithm_objects_[index];
  out_slot->assembly_state = &algorithm_assembly_states_[index];
  return true;
}

bool Agent::CollectAlgorithmReflection(size_t index, AlgorithmReflectionSnapshot* out_snapshot) const {
  if (!out_snapshot || index >= algorithm_objects_.size()) {
    return false;
  }
  const AlgorithmObject& object = algorithm_objects_[index];
  const AlgorithmContainerSet* container_set = algorithm_objects_[index].container_set();
  if (!container_set) {
    return false;
  }
  return _CollectReflectionSnapshot(object, *container_set, out_snapshot);
}

void Agent::Destroy() {
  initialized_ = false;
  agent_name_.clear();
  algorithm_objects_.clear();
  algorithm_runtime_states_.clear();
  algorithm_assembly_states_.clear();
  algorithm_execution_end_queue_.clear();
  standard_shared_container_sets_.clear();
}

}  // namespace agent
