#pragma once

#include "algomanager/bridge/algorithm_abi.h"
#include "algomanager/bridge/algorithm_interaction_protocol.h"
#include "algomanager/bridge/algorithm_package_location.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace algomanager { namespace bridge {

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message = nullptr);

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  PipelineStageBridgeDebugSet* in_out_debug_set,
  std::string* out_error_message = nullptr);

class PipelineStageBridge {
 public:
  explicit PipelineStageBridge(std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map)
    : transfer_map_(std::move(transfer_map)) {}

  bool IngestFromPreviousStage(
    const std::string& target_stage_name,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    algorithm::AlgorithmContainerSet* out_target_container_set,
    std::string* out_error_message = nullptr) const {
    return PipelineStageBridgeIngress(
      *transfer_map_,
      target_stage_name,
      stage_container_sets,
      out_target_container_set,
      out_error_message);
  }

  bool IngestFromPreviousStage(
    const std::string& target_stage_name,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    const JobsPipelineInterStageBufferRuntimeState& inter_stage_buffer,
    algorithm::AlgorithmContainerSet* out_target_container_set,
    std::string* out_error_message = nullptr) const {
    return PipelineStageBridgeIngress(
      *transfer_map_,
      target_stage_name,
      stage_container_sets,
      inter_stage_buffer,
      out_target_container_set,
      out_error_message);
  }

  bool EmitToNextStage(
    const std::string& source_stage_name,
    const algorithm::AlgorithmContainerSet& source_container_set,
    std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
    std::string* out_error_message = nullptr) const {
    return PipelineStageBridgeEgress(
      *transfer_map_,
      source_stage_name,
      source_container_set,
      stage_container_sets,
      out_error_message);
  }

  bool EmitToNextStage(
    const std::string& source_stage_name,
    const algorithm::AlgorithmContainerSet& source_container_set,
    JobsPipelineInterStageBufferRuntimeState* inter_stage_buffer,
    std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
    std::string* out_error_message = nullptr) const {
    return PipelineStageBridgeEgress(
      *transfer_map_,
      source_stage_name,
      source_container_set,
      inter_stage_buffer,
      stage_container_sets,
      out_error_message);
  }

  bool CaptureIngressDebugSet(
    const std::string& pipeline_name,
    const std::string& target_stage_name,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    const algorithm::AlgorithmContainerSet& target_container_set,
    PipelineStageBridgeDebugSet* out_debug_set,
    std::string* out_error_message = nullptr) const {
    return PipelineStageBridgeCaptureIngressDebugSet(
      *transfer_map_,
      pipeline_name,
      target_stage_name,
      stage_container_sets,
      target_container_set,
      out_debug_set,
      out_error_message);
  }

  bool CaptureEgressDebugSet(
    const std::string& pipeline_name,
    const std::string& source_stage_name,
    const algorithm::AlgorithmContainerSet& source_container_set,
    const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
    PipelineStageBridgeDebugSet* in_out_debug_set,
    std::string* out_error_message = nullptr) const {
    return PipelineStageBridgeCaptureEgressDebugSet(
      *transfer_map_,
      pipeline_name,
      source_stage_name,
      source_container_set,
      stage_container_sets,
      in_out_debug_set,
      out_error_message);
  }

 private:
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> transfer_map_{};
};

}  // namespace bridge
}  // namespace algomanager
