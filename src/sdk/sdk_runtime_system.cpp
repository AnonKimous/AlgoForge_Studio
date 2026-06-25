#define SDK_LAYER_INTERNAL_BUILD 1
#include "sdk_runtime_system.h"
#undef SDK_LAYER_INTERNAL_BUILD

#include "agent_management/agent_management.h"
#include "common_data/kernel_cfg.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace sdk {

namespace {

void _SetError(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

std::vector<agent::AlgorithmResourceBinding> _ToAgentResourceBindings(
  const std::vector<ResourceBinding>& bindings) {
  std::vector<agent::AlgorithmResourceBinding> result;
  result.reserve(bindings.size());
  for (const ResourceBinding& binding : bindings) {
    result.push_back(agent::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
    });
  }
  return result;
}

std::vector<agent::AlgorithmDescriptorValue> _ToAgentDescriptorValues(
  const std::vector<DescriptorValue>& values) {
  std::vector<agent::AlgorithmDescriptorValue> result;
  result.reserve(values.size());
  for (const DescriptorValue& value : values) {
    result.push_back(agent::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  return result;
}

}  // namespace

SdkRuntimeSystem::SdkRuntimeSystem()
  : agent_manager_(std::make_unique<agent_management::AgentManager>()) {}

SdkRuntimeSystem::~SdkRuntimeSystem() = default;

SdkRuntimeSystem::SdkRuntimeSystem(SdkRuntimeSystem&&) noexcept = default;

SdkRuntimeSystem& SdkRuntimeSystem::operator=(SdkRuntimeSystem&&) noexcept = default;

void SdkRuntimeSystem::Reset() {
  drafts_.clear();
  agents_.clear();
  next_agent_handle_ = 1;
  next_algorithm_handle_ = 1;
  if (agent_manager_) {
    agent_manager_->Destroy();
  }
}

SdkRuntimeSystem::AgentRecord* SdkRuntimeSystem::FindAgentRecord(AgentHandle agent_handle) {
  for (AgentRecord& record : agents_) {
    if (record.handle == agent_handle) {
      return &record;
    }
  }
  return nullptr;
}

const SdkRuntimeSystem::AgentRecord* SdkRuntimeSystem::FindAgentRecord(AgentHandle agent_handle) const {
  for (const AgentRecord& record : agents_) {
    if (record.handle == agent_handle) {
      return &record;
    }
  }
  return nullptr;
}

SdkRuntimeSystem::AlgorithmDraft* SdkRuntimeSystem::FindDraft(AlgorithmHandle algorithm_handle) {
  for (AlgorithmDraft& draft : drafts_) {
    if (draft.handle == algorithm_handle) {
      return &draft;
    }
  }
  return nullptr;
}

const SdkRuntimeSystem::AlgorithmDraft* SdkRuntimeSystem::FindDraft(AlgorithmHandle algorithm_handle) const {
  for (const AlgorithmDraft& draft : drafts_) {
    if (draft.handle == algorithm_handle) {
      return &draft;
    }
  }
  return nullptr;
}

void SdkRuntimeSystem::ShiftAgentIndicesAfterErase(size_t erased_agent_index) {
  for (AgentRecord& record : agents_) {
    if (record.agent_index > erased_agent_index) {
      --record.agent_index;
    }
  }
  for (AlgorithmDraft& draft : drafts_) {
    if (draft.agent_index > erased_agent_index) {
      --draft.agent_index;
    }
  }
}

void SdkRuntimeSystem::ShiftSubmittedAlgorithmIndicesAfterErase(AgentHandle agent_handle, size_t erased_algorithm_index) {
  for (AlgorithmDraft& draft : drafts_) {
    if (draft.agent_handle == agent_handle && draft.submitted && draft.submitted_algorithm_index > erased_algorithm_index) {
      --draft.submitted_algorithm_index;
    }
  }
}

AgentHandle SdkRuntimeSystem::CreateAgent(
  std::string agent_name,
  std::string* out_error_message) {
  return CreateAgent(
    std::move(agent_name),
    common_data::DefaultAgentLimitFpsFlag(),
    out_error_message);
}

AgentHandle SdkRuntimeSystem::CreateAgent(
  std::string agent_name,
  uint32_t limit_fps_flag,
  std::string* out_error_message) {
  if (!agent_manager_) {
    _SetError(out_error_message, "Agent manager is unavailable.");
    return 0;
  }

  AgentCreateSpec spec{};
  spec.agent_name = std::move(agent_name);
  spec.limit_fps_flag = limit_fps_flag;
  size_t agent_index = 0u;
  if (!agent_manager_->CreateAgent(std::move(spec), &agent_index)) {
    _SetError(out_error_message, "Failed to create agent.");
    return 0;
  }

  const AgentHandle handle = next_agent_handle_++;
  agents_.push_back(AgentRecord{
    .handle = handle,
    .agent_index = agent_index,
  });
  _SetError(out_error_message, {});
  return handle;
}

bool SdkRuntimeSystem::DestroyAgent(AgentHandle agent_handle, std::string* out_error_message) {
  if (!agent_manager_) {
    _SetError(out_error_message, "Agent manager is unavailable.");
    return false;
  }

  AgentRecord* record = FindAgentRecord(agent_handle);
  if (!record) {
    _SetError(out_error_message, "Agent handle is invalid.");
    return false;
  }

  const size_t erased_agent_index = record->agent_index;
  drafts_.erase(
    std::remove_if(
      drafts_.begin(),
      drafts_.end(),
      [&](const AlgorithmDraft& draft) {
        return draft.agent_handle == agent_handle;
      }),
    drafts_.end());

  if (!agent_manager_->DestroyAgent(erased_agent_index)) {
    _SetError(out_error_message, "Failed to destroy agent.");
    return false;
  }

  agents_.erase(
    std::remove_if(
      agents_.begin(),
      agents_.end(),
      [&](const AgentRecord& current) {
        return current.handle == agent_handle;
      }),
    agents_.end());
  ShiftAgentIndicesAfterErase(erased_agent_index);
  _SetError(out_error_message, {});
  return true;
}

AlgorithmHandle SdkRuntimeSystem::MountAlgorithm(
  AgentHandle agent_handle,
  const std::string& algorithm_name,
  std::string* out_error_message) {
  if (!agent_manager_) {
    _SetError(out_error_message, "Agent manager is unavailable.");
    return 0;
  }

  const AgentRecord* record = FindAgentRecord(agent_handle);
  if (!record) {
    _SetError(out_error_message, "Agent handle is invalid.");
    return 0;
  }

  agent::AlgorithmRequestedResources requested_resources{};
  agent::AlgorithmRequestedDescriptorBindings requested_descriptors{};
  const bool queried_bindings_ok = agent::QueryAlgorithmRequestedBindingsByName(
    algorithm_name,
    &requested_resources,
    &requested_descriptors,
    out_error_message);
  if (!queried_bindings_ok) {
    if (out_error_message && out_error_message->empty()) {
      *out_error_message = "Failed to query requested bindings for '" + algorithm_name + "'.";
    }
    return 0;
  }

  AlgorithmDraft draft{};
  draft.handle = next_algorithm_handle_++;
  draft.agent_handle = agent_handle;
  draft.agent_index = record->agent_index;
  draft.algorithm_name = requested_resources.algorithm_name.empty()
    ? algorithm_name
    : requested_resources.algorithm_name;
  draft.resource_bindings.reserve(requested_resources.required_resources.size());
  for (const agent::AlgorithmRequestedResources::RequiredResource& resource : requested_resources.required_resources) {
    draft.resource_bindings.push_back(ResourceBinding{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .source_path = {},
      .required = resource.required,
    });
  }
  draft.descriptor_values.reserve(requested_descriptors.descriptor_slots.size());
  std::unordered_set<std::string> seen_descriptor_names{};
  for (const agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot& descriptor :
       requested_descriptors.descriptor_slots) {
    if (!seen_descriptor_names.insert(descriptor.descriptor_name).second) {
      continue;
    }
    draft.descriptor_values.push_back(DescriptorValue{
      .descriptor_name = descriptor.descriptor_name,
      .scalar_value = 0.0,
    });
  }

  drafts_.push_back(std::move(draft));
  _SetError(out_error_message, {});
  return drafts_.back().handle;
}

bool SdkRuntimeSystem::UnmountAlgorithm(AlgorithmHandle algorithm_handle, std::string* out_error_message) {
  if (!agent_manager_) {
    _SetError(out_error_message, "Agent manager is unavailable.");
    return false;
  }

  AlgorithmDraft* draft = FindDraft(algorithm_handle);
  if (!draft) {
    _SetError(out_error_message, "Algorithm handle is invalid.");
    return false;
  }

  if (draft->submitted) {
    const AgentRecord* record = FindAgentRecord(draft->agent_handle);
    if (!record) {
      _SetError(out_error_message, "Agent handle is invalid.");
      return false;
    }
    if (!agent_manager_->DetachAlgorithmFromAgent(record->agent_index, draft->submitted_algorithm_index, out_error_message)) {
      return false;
    }
    ShiftSubmittedAlgorithmIndicesAfterErase(draft->agent_handle, draft->submitted_algorithm_index);
  }

  drafts_.erase(
    std::remove_if(
      drafts_.begin(),
      drafts_.end(),
      [&](const AlgorithmDraft& current) {
        return current.handle == algorithm_handle;
      }),
    drafts_.end());
  _SetError(out_error_message, {});
  return true;
}

bool SdkRuntimeSystem::MountResource(
  AlgorithmHandle algorithm_handle,
  const std::string& resource_name,
  const std::string& resource_kind,
  const std::string& source_path,
  std::string* out_error_message) {
  AlgorithmDraft* draft = FindDraft(algorithm_handle);
  if (!draft) {
    _SetError(out_error_message, "Algorithm handle is invalid.");
    return false;
  }
  if (draft->submitted) {
    _SetError(out_error_message, "Submitted algorithms are immutable; unmount and remount first.");
    return false;
  }

  for (ResourceBinding& binding : draft->resource_bindings) {
    if (binding.resource_name == resource_name && binding.resource_kind == resource_kind) {
      binding.source_path = source_path;
      _SetError(out_error_message, {});
      return true;
    }
  }

  _SetError(
    out_error_message,
    "Resource '" + resource_name + "' with kind '" + resource_kind + "' is not requested by the algorithm.");
  return false;
}

bool SdkRuntimeSystem::SubmitAlgorithm(
  AlgorithmHandle algorithm_handle,
  size_t* out_algorithm_index,
  std::string* out_error_message) {
  if (!agent_manager_) {
    _SetError(out_error_message, "Agent manager is unavailable.");
    return false;
  }

  AlgorithmDraft* draft = FindDraft(algorithm_handle);
  if (!draft) {
    _SetError(out_error_message, "Algorithm handle is invalid.");
    return false;
  }
  if (draft->submitted) {
    if (out_algorithm_index) {
      *out_algorithm_index = draft->submitted_algorithm_index;
    }
    _SetError(out_error_message, {});
    return true;
  }

  const AgentRecord* record = FindAgentRecord(draft->agent_handle);
  if (!record) {
    _SetError(out_error_message, "Agent handle is invalid.");
    return false;
  }

  std::vector<agent::AlgorithmResourceBinding> resource_bindings =
    _ToAgentResourceBindings(draft->resource_bindings);
  std::vector<agent::AlgorithmDescriptorValue> descriptor_values =
    _ToAgentDescriptorValues(draft->descriptor_values);
  size_t submitted_algorithm_index = 0u;
  if (!agent_manager_->AttachAlgorithmToAgent(
        record->agent_index,
        draft->algorithm_name.c_str(),
        resource_bindings,
        descriptor_values,
        &submitted_algorithm_index,
        out_error_message)) {
    return false;
  }

  draft->submitted = true;
  draft->submitted_algorithm_index = submitted_algorithm_index;
  draft->agent_index = record->agent_index;
  if (out_algorithm_index) {
    *out_algorithm_index = submitted_algorithm_index;
  }
  _SetError(out_error_message, {});
  return true;
}

}  // namespace sdk
