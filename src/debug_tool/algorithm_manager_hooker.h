#pragma once

#include "algorithm_management/algorithm_manager.h"
#include "algorithm_support/algorithm_library_paths.h"
#include "algorithm_support/algorithm_protocol.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace debug_tool_backend::algorithm_manager_hooker {

inline std::string AlgorithmCatalogPath() {
  return (algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot() / "algorithm_catalog.json").string();
}

inline std::string ProjectRootPath() {
  const std::filesystem::path root = algorithm::library_paths::ResolveProjectRootFromAlgorithmLibraryRoot(
    algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot());
  if (!root.empty()) {
    return root.string();
  }
  return ".";
}

inline std::filesystem::path ResolveAlgorithmLibrarySourceRoot() {
  return algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimeRoot() {
  return algorithm::library_paths::ResolveAlgorithmLibraryRuntimeRoot();
}

inline std::filesystem::path ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() {
  return algorithm::library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot();
}

inline std::string HotReloadBuildCommand(const std::string& algorithm_name) {
  const std::string root = ProjectRootPath();
  const std::string script_path = (std::filesystem::path(root) / "build_algorithm.bat").string();
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
  return "\"" + script_path + "\" \"" + normalized_target_name + "\"";
}

inline bool TryResolveAlgorithmPackageLocation(
  const std::string& algorithm_name,
  ::algorithm::AlgorithmPackageLocation* out_location,
  std::string* out_error_message = nullptr) {
  return algorithm_management::TryResolveAlgorithmPackageLocation(
    algorithm_name,
    out_location,
    out_error_message);
}

inline bool LoadAlgorithmPackageTransferMapFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map = nullptr,
  std::string* out_error_message = nullptr) {
  return algorithm_support::LoadAlgorithmPackageTransferMapFromLocation(
    package_location,
    out_transfer_map,
    out_has_transfer_map,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const ::algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algorithm_management::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algorithm_management::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return algorithm_support::LoadAlgorithmPackageDefaultBindingsFromLocation(
    package_location,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<algorithm_management::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algorithm_management::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file = nullptr,
  std::string* out_error_message = nullptr) {
  return algorithm_management::LoadAlgorithmPackageDefaultBindings(
    algorithm_name,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

inline bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  algorithm_management::AlgorithmRequestedResources* out_resources,
  algorithm_management::AlgorithmRequestedDescriptorBindings* out_descriptors,
  std::string* out_error_message = nullptr) {
  return algorithm_management::QueryAlgorithmRequestedBindings(
    algorithm_name,
    out_resources,
    out_descriptors,
    out_error_message);
}

inline bool TryGetMountedPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& agent_name,
  algorithm_management::CpuPipelineRuntimeState* out_runtime_state) {
  return algorithm_management::TryGetMountedPipelineRuntime(
    pipeline_name,
    agent_name,
    out_runtime_state);
}

inline bool TryGetMountedPipelineRegistration(
  const std::string& pipeline_name,
  algorithm_management::CpuPipelineRegistration* out_registration) {
  return algorithm_management::AlgorithmScheduler::Instance().TryGetPipelineRegistration(
    pipeline_name,
    out_registration);
}

inline void ClearAlgorithmScheduler() {
  algorithm_management::ClearAlgorithmScheduler();
}

inline void SetAlgorithmRuntimeShutdownHook() {
  algorithm_management::SetAlgorithmRuntimeShutdownHook();
}

inline void ClearAlgorithmExecutionCaches() {
  algorithm_management::ClearAlgorithmExecutionCaches();
}

inline bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message = nullptr) {
  return algorithm_management::ExecuteCpuAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

inline bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message = nullptr) {
  return algorithm_management::ExecuteGpuAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

inline bool HasExecutableGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
  return algorithm_management::HasExecutableGpuAlgorithmStage(object);
}

inline bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message = nullptr) {
  return algorithm_management::SynchronizeGpuAlgorithmObject(
    object,
    container_set,
    out_error_message);
}

}  // namespace debug_tool_backend::algorithm_manager_hooker
