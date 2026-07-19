#include "algomanager/bridge/algorithm_protocol.h"
#include "algomanager/bridge/algorithm_abi.h"

#include "algomanager/catalog/algorithm_container_manifest.h"
#include "algomanager/catalog/algorithm_assimp_support.h"
#include "algomanager/bridge/algorithm_library_paths.h"
#include "algomanager/catalog/algorithm_json_utils.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/catalog/algorithm_package_paths.h"

#include "cJSON.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <type_traits>
#include <utility>

namespace algomanager { namespace algocatalog {

namespace {

void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

void _CopyBridgeDebugBindings(
  const algorithm::AlgorithmRuntimeTransferEdge* edge,
  std::vector<algomanager::bridge::PipelineStageBridgeDebugBinding>* out_bindings) {
  if (!out_bindings) {
    return;
  }
  out_bindings->clear();
  if (!edge) {
    return;
  }
  out_bindings->reserve(edge->bindings.size());
  for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge->bindings) {
    out_bindings->push_back(algomanager::bridge::PipelineStageBridgeDebugBinding{
      .source_stage_name = edge->source_stage_name,
      .target_stage_name = edge->target_stage_name,
      .source_container_name = binding.from_name,
      .target_container_name = binding.to_name,
      .required = binding.required,
    });
  }
}

const algorithm::AlgorithmRuntimeTransferStageLayout* _FindRuntimeTransferStageLayout(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& stage_name) {
  return transfer_map.FindStageLayout(stage_name);
}

bool _ReadArrayScalarAt(
  const algorithm::AlgorithmContainer& container,
  uint32_t index,
  float* out_value,
  std::string* out_error_message) {
  if (!out_value) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer read output pointer is null.");
    return false;
  }
  if (container.storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer must be an array container.");
    return false;
  }
  if (container.element_stride < sizeof(float) || index >= container.element_count) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar read is out of range.");
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container.element_stride);
  if (byte_offset + sizeof(float) > container.bytes.size()) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar read exceeds container storage.");
    return false;
  }
  std::memcpy(out_value, container.bytes.data() + byte_offset, sizeof(float));
  return true;
}

bool _WriteArrayScalarAt(
  algorithm::AlgorithmContainer* container,
  uint32_t index,
  float value,
  std::string* out_error_message) {
  if (!container) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer write target is null.");
    return false;
  }
  if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer must be an array container.");
    return false;
  }
  if (container->element_stride < sizeof(float) || index >= container->element_count) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar write is out of range.");
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container->element_stride);
  if (byte_offset + sizeof(float) > container->bytes.size()) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar write exceeds container storage.");
    return false;
  }
  std::memcpy(container->bytes.data() + byte_offset, &value, sizeof(float));
  return true;
}

bool _ReadJobsInterStageScalarAt(
  const algomanager::bridge::JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  uint32_t index,
  float* out_value,
  std::string* out_error_message) {
  if (!inter_stage_buffer.valid) {
    _SetErrorMessage(out_error_message, "JOBS pipeline inter-stage buffer is invalid.");
    return false;
  }
  if (!out_value) {
    _SetErrorMessage(out_error_message, "JOBS pipeline inter-stage buffer read output pointer is null.");
    return false;
  }
  if (index >= inter_stage_buffer.scalar_slots.size() ||
      index >= inter_stage_buffer.scalar_slot_count) {
    _SetErrorMessage(out_error_message, "JOBS pipeline inter-stage buffer scalar read is out of range.");
    return false;
  }
  *out_value = inter_stage_buffer.scalar_slots[index];
  return true;
}

bool _WriteJobsInterStageScalarAt(
  algomanager::bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  uint32_t index,
  float value,
  std::string* out_error_message) {
  if (!inter_stage_buffer) {
    _SetErrorMessage(out_error_message, "JOBS pipeline inter-stage buffer write target is null.");
    return false;
  }
  if (!inter_stage_buffer->valid) {
    _SetErrorMessage(out_error_message, "JOBS pipeline inter-stage buffer is invalid.");
    return false;
  }
  if (index >= inter_stage_buffer->scalar_slots.size() ||
      index >= inter_stage_buffer->scalar_slot_count) {
    _SetErrorMessage(out_error_message, "JOBS pipeline inter-stage buffer scalar write is out of range.");
    return false;
  }
  inter_stage_buffer->scalar_slots[index] = value;
  return true;
}

bool _ApplyImplicitStageBufferIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const algomanager::bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::unordered_set<std::string>* written_target_names,
  std::string* out_error_message) {
  if (!target_container_set || !written_target_names) {
    _SetErrorMessage(out_error_message, "Pipeline implicit stage buffer ingress inputs are null.");
    return false;
  }
  const algorithm::AlgorithmRuntimeTransferStageLayout* stage_layout =
    _FindRuntimeTransferStageLayout(transfer_map, target_stage_name);
  if (!stage_layout) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer ingress layout is missing for stage '" + target_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_array_count != 0u) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer ingress does not support extra standard arrays for stage '" +
        target_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_variable_count == 0u) {
    return true;
  }

  for (uint32_t extra_index = 0u; extra_index < stage_layout->extra_variable_count; ++extra_index) {
    const std::string variable_name =
      target_container_set->standard_layout.MakeVariableName(stage_layout->shared_variable_count + extra_index);
    if (written_target_names->find(variable_name) != written_target_names->end()) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable is written more than once: " +
          target_stage_name + "." + variable_name);
      return false;
    }

    algorithm::AlgorithmContainer* target_variable =
      algorithm::FindAlgorithmContainer(target_container_set, variable_name);
    if (!target_variable) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable is missing: " +
          target_stage_name + "." + variable_name);
      return false;
    }
    if (target_variable->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister ||
        target_variable->bytes.size() < sizeof(float)) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable must be a scalar register: " +
          target_stage_name + "." + variable_name);
      return false;
    }

    float value = 0.0f;
    if (inter_stage_buffer) {
      if (!_ReadJobsInterStageScalarAt(
            *inter_stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            &value,
            out_error_message)) {
        return false;
      }
    } else {
      algorithm::AlgorithmContainer* stage_buffer =
        algorithm::FindAlgorithmContainer(target_container_set, transfer_map.pipeline_shared_stage_buffer_slot_name);
      if (!stage_buffer) {
        _SetErrorMessage(
          out_error_message,
          "Pipeline implicit stage buffer ingress cannot find shared stage buffer '" +
            transfer_map.pipeline_shared_stage_buffer_slot_name + "' for stage '" + target_stage_name + "'.");
        return false;
      }
      if (!_ReadArrayScalarAt(
            *stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            &value,
            out_error_message)) {
        return false;
      }
    }
    std::memcpy(target_variable->bytes.data(), &value, sizeof(float));
    written_target_names->insert(variable_name);
  }

  return true;
}

bool _ApplyImplicitStageBufferEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algomanager::bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::string* out_error_message) {
  const algorithm::AlgorithmRuntimeTransferStageLayout* stage_layout =
    _FindRuntimeTransferStageLayout(transfer_map, source_stage_name);
  if (!stage_layout) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer egress layout is missing for stage '" + source_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_array_count != 0u) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer egress does not support extra standard arrays for stage '" +
        source_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_variable_count == 0u) {
    return true;
  }

  for (uint32_t extra_index = 0u; extra_index < stage_layout->extra_variable_count; ++extra_index) {
    const std::string variable_name =
      source_container_set.standard_layout.MakeVariableName(stage_layout->shared_variable_count + extra_index);
    const algorithm::AlgorithmContainer* source_variable =
      algorithm::FindAlgorithmContainer(source_container_set, variable_name);
    if (!source_variable) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer egress source variable is missing: " +
          source_stage_name + "." + variable_name);
      return false;
    }
    if (source_variable->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister ||
        source_variable->bytes.size() < sizeof(float)) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer egress source variable must be a scalar register: " +
          source_stage_name + "." + variable_name);
      return false;
    }

    float value = 0.0f;
    std::memcpy(&value, source_variable->bytes.data(), sizeof(float));
    if (inter_stage_buffer) {
      if (!_WriteJobsInterStageScalarAt(
            inter_stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            value,
            out_error_message)) {
        return false;
      }
    } else {
      algorithm::AlgorithmContainer* stage_buffer =
        algorithm::FindAlgorithmContainer(
          const_cast<algorithm::AlgorithmContainerSet*>(&source_container_set),
          transfer_map.pipeline_shared_stage_buffer_slot_name);
      if (!stage_buffer) {
        _SetErrorMessage(
          out_error_message,
          "Pipeline implicit stage buffer egress cannot find shared stage buffer '" +
            transfer_map.pipeline_shared_stage_buffer_slot_name + "' for stage '" + source_stage_name + "'.");
        return false;
      }
      if (!_WriteArrayScalarAt(
            stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            value,
            out_error_message)) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace

bool _PipelineStageBridgeIngressImpl(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algomanager::bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!out_target_container_set) {
    _SetErrorMessage(out_error_message, "Runtime transfer target container set is null.");
    return false;
  }
  (void)stage_container_sets;
  if (inter_stage_buffer) {
    std::unordered_set<std::string> written_target_names;
    if (!_ApplyImplicitStageBufferIngress(
          transfer_map,
          target_stage_name,
          inter_stage_buffer,
          out_target_container_set,
          &written_target_names,
          out_error_message)) {
      return false;
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return _PipelineStageBridgeIngressImpl(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    nullptr,
    out_target_container_set,
    out_error_message);
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algomanager::bridge::JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return _PipelineStageBridgeIngressImpl(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    &inter_stage_buffer,
    out_target_container_set,
    out_error_message);
}

bool _PipelineStageBridgeEgressImpl(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algomanager::bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!stage_container_sets) {
    _SetErrorMessage(out_error_message, "Runtime transfer stage container set map is null.");
    return false;
  }
  (void)stage_container_sets;
  if (inter_stage_buffer) {
    if (!_ApplyImplicitStageBufferEgress(
          transfer_map,
          source_stage_name,
          source_container_set,
          inter_stage_buffer,
          out_error_message)) {
      return false;
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return _PipelineStageBridgeEgressImpl(
    transfer_map,
    source_stage_name,
    source_container_set,
    nullptr,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algomanager::bridge::JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return _PipelineStageBridgeEgressImpl(
    transfer_map,
    source_stage_name,
    source_container_set,
    inter_stage_buffer,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algomanager::bridge::PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message) {
  (void)stage_container_sets;
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!out_debug_set) {
    _SetErrorMessage(out_error_message, "Pipeline bridge debug set output pointer is null.");
    return false;
  }

  out_debug_set->Clear();
  out_debug_set->pipeline_name = pipeline_name.empty() ? transfer_map.algorithm_name : pipeline_name;
  out_debug_set->stage_name = target_stage_name;

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> incoming_edges =
    transfer_map.FindIncomingEdges(target_stage_name);
  if (incoming_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + target_stage_name + "' has multiple predecessors.");
    return false;
  }

  if (!incoming_edges.empty() && incoming_edges.front()) {
    out_debug_set->previous_stage_name = incoming_edges.front()->source_stage_name;
    _CopyBridgeDebugBindings(incoming_edges.front(), &out_debug_set->ingress_bindings);
  }

  (void)target_container_set;
  out_debug_set->valid = true;

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algomanager::bridge::PipelineStageBridgeDebugSet* in_out_debug_set,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!in_out_debug_set) {
    _SetErrorMessage(out_error_message, "Pipeline bridge debug set output pointer is null.");
    return false;
  }

  if (!in_out_debug_set->valid) {
    in_out_debug_set->Clear();
  }
  if (in_out_debug_set->pipeline_name.empty()) {
    in_out_debug_set->pipeline_name = pipeline_name.empty() ? transfer_map.algorithm_name : pipeline_name;
  }
  in_out_debug_set->stage_name = source_stage_name;

  (void)source_container_set;

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
    transfer_map.FindOutgoingEdges(source_stage_name);
  if (outgoing_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + source_stage_name + "' has multiple successors.");
    return false;
  }

  in_out_debug_set->next_stage_name.clear();
  in_out_debug_set->egress_bindings.clear();
  in_out_debug_set->next_stage_input_container_set = {};
  in_out_debug_set->has_next_stage_input_container_set = false;

  if (!outgoing_edges.empty() && outgoing_edges.front()) {
    const algorithm::AlgorithmRuntimeTransferEdge* outgoing_edge = outgoing_edges.front();
    in_out_debug_set->next_stage_name = outgoing_edge->target_stage_name;
    _CopyBridgeDebugBindings(outgoing_edge, &in_out_debug_set->egress_bindings);

    const auto target_stage_it = stage_container_sets.find(outgoing_edge->target_stage_name);
    if (target_stage_it == stage_container_sets.end() || !target_stage_it->second) {
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target stage is unavailable for bridge debug capture: " +
          outgoing_edge->target_stage_name);
      return false;
    }

    (void)target_stage_it;
  }

  in_out_debug_set->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}


}  // namespace algocatalog
}  // namespace algomanager
