#pragma once

#include "algomanager/algorithm_manager.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace debug_tool_backend::algomanager_hooker {

inline std::string ProjectRootPath() {
  const std::filesystem::path root = algomanager::ResolveProjectRootFromAlgorithmLibraryRoot(
    algomanager::ResolveAlgorithmLibrarySourceRoot());
  if (!root.empty()) {
    return root.string();
  }
  return ".";
}

inline std::filesystem::path ResolveAlgorithmLibrarySourceRoot() {
  return algomanager::ResolveAlgorithmLibrarySourceRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimeRoot() {
  return algomanager::ResolveAlgorithmLibraryRuntimeRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() {
  return algomanager::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot();
}

inline std::string HotReloadBuildCommand(const std::string& algorithm_name) {
  const std::string root = ProjectRootPath();
  const std::string script_path = (std::filesystem::path(root) / "boot" / "booterNinjaClang.py").string();
  const std::string target_name = algorithm_name;
  std::string normalized_target_name;
  normalized_target_name.reserve(target_name.size() + 1u);
  for (size_t i = 0; i < target_name.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(target_name[i]);
    const bool is_identifier_char =
      (ch >= 'a' && ch <= 'z') ||
      (ch >= 'A' && ch <= 'Z') ||
      (ch >= '0' && ch <= '9') ||
      ch == '_';
    if (i == 0u && (ch >= '0' && ch <= '9')) {
      normalized_target_name.push_back('_');
    }
    normalized_target_name.push_back(is_identifier_char ? static_cast<char>(ch) : '_');
  }
  return "python \"" + script_path + "\" \"" + normalized_target_name + "\"";
}

inline bool TryResolveAlgorithmPackageLocation(
  const std::string& algorithm_name,
  ::algorithm::AlgorithmPackageLocation* out_location,
  std::string* out_error_message = nullptr) {
  return algorithm::TryResolveAlgorithmPackageLocation(
    algorithm_name,
    out_location,
    out_error_message);
}

inline bool LoadAlgorithmPackageTransferMapFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map = nullptr,
  std::string* out_error_message = nullptr) {
  return algomanager::LoadAlgorithmPackageTransferMapFromLocation(
    package_location,
    out_transfer_map,
    out_has_transfer_map,
    out_error_message);
}

inline bool LoadAlgorithmPipelineWrapperSpecFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  algomanager::AlgorithmPipelineWrapperSpec* out_wrapper_spec,
  std::string* out_error_message = nullptr) {
  return algomanager::LoadAlgorithmPipelineWrapperSpecFromLocation(
    package_location,
    out_wrapper_spec,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algomanager::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algomanager::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return algomanager::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<algomanager::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algomanager::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return algomanager::LoadAlgorithmPackageDefaultBindings(
    algorithm_name,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  algomanager::AlgorithmRequestedResources* out_resources,
  algomanager::AlgorithmRequestedDescriptorBindings* out_descriptors,
  std::string* out_error_message = nullptr) {
  return algomanager::QueryAlgorithmRequestedBindings(
    algorithm_name,
    out_resources,
    out_descriptors,
    out_error_message);
}

inline bool TryGetMountedPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& agent_name,
  algomanager::JobsPipelineRuntimeState* out_runtime_state) {
  return algomanager::TryGetMountedPipelineRuntime(
    pipeline_name,
    agent_name,
    out_runtime_state);
}

inline bool TryGetMountedPipelineRegistration(
  const std::string& pipeline_name,
  algomanager::JobsPipelineRegistration* out_registration) {
  return algomanager::TryGetMountedPipelineRegistration(
    pipeline_name,
    out_registration);
}

inline void ClearAlgorithmScheduler() {
  algomanager::ClearAlgorithmScheduler();
}

inline void BeginDebugToolRecording() {
  algomanager::BeginDebugToolRecording();
}

inline void EndDebugToolRecording() {
  algomanager::EndDebugToolRecording();
}

inline uint64_t DebugToolRecordingTickCount() {
  return algomanager::DebugToolRecordingTickCount();
}

inline void SetAlgorithmRuntimeShutdownHook() {
  algomanager::SetAlgorithmRuntimeShutdownHook();
}

inline void ClearAlgorithmExecutionCaches() {
  algomanager::ClearAlgorithmExecutionCaches();
}

inline bool ExecuteJobsAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  const ::agentmanager::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agentmanager::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return algomanager::ExecuteJobsAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

inline bool ExecuteVkAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agentmanager::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr) {
  return algomanager::ExecuteVkAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

inline bool HasExecutableVkAlgorithmStage(const ::agentmanager::agent::AlgorithmObject& object) {
  return algomanager::HasExecutableVkAlgorithmStage(object);
}

inline bool SynchronizeVkAlgorithmObject(
  const ::agentmanager::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  return algomanager::SynchronizeVkAlgorithmObject(
    object,
    container_set,
    out_error_message);
}

}  // namespace debug_tool_backend::algomanager_hooker
