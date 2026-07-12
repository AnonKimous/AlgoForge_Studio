#pragma once

#include "debug_tool/debug_tool_host.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace debug_tool {

enum class DebugCommandId : uint8_t {
  SetRuntimeBuildFlavor,
  StartTick,
  PauseTick,
  ClearAgents,
  ClearVkRuntimeCaches,
  AttachAlgorithm,
  AttachPipelinePackage,
  EnqueuePipelineStage0,
  DetachAlgorithm,
  HotReloadAlgorithm,
  ReplayPipelineStage,
  RequestTimingLog,
  ExportPipelineTiming,
  SetRenderPreviewRequest,
  ClearRenderPreviewRequest,
  SetRenderPreviewExtent,
};

struct DebugCommandRegistration {
  const char* name;
  DebugCommandId id;
};

struct DebugCommand {
  DebugCommandId id{DebugCommandId::StartTick};
  size_t agent_index{0u};
  size_t algorithm_index{0u};
  std::string algorithm_name{};
  std::string pipeline_name{};
  AlgorithmRuntimeBuildFlavor runtime_build_flavor{AlgorithmRuntimeBuildFlavor::Debug};
  AlgorithmMountMode mount_mode{AlgorithmMountMode::Direct};
  AlgorithmExecutionPreference execution_preference{AlgorithmExecutionPreference::Vk};
  std::vector<AlgorithmResourceBinding> resource_bindings{};
  std::vector<AlgorithmDescriptorValue> descriptor_values{};
  std::vector<AlgorithmPipelineStageSubmission> stage_submissions{};
  runtime_systems::RenderPreviewRequest preview_request{};
  ImVec2 preview_extent{};
};

struct DebugCommandResult {
  bool ok{false};
  size_t algorithm_index{0u};
  std::string message{};
  std::string csv_path{};
  std::string mermaid_path{};
};

class DebugCmd final {
 public:
  static const std::vector<DebugCommandRegistration>& Registry() {
    static const std::vector<DebugCommandRegistration> registry{
      {"set-runtime-build-flavor", DebugCommandId::SetRuntimeBuildFlavor},
      {"start-tick", DebugCommandId::StartTick},
      {"pause-tick", DebugCommandId::PauseTick},
      {"clear-agents", DebugCommandId::ClearAgents},
      {"clear-vk-runtime-caches", DebugCommandId::ClearVkRuntimeCaches},
      {"attach-algorithm", DebugCommandId::AttachAlgorithm},
      {"attach-pipeline-package", DebugCommandId::AttachPipelinePackage},
      {"enqueue-pipeline-stage0", DebugCommandId::EnqueuePipelineStage0},
      {"detach-algorithm", DebugCommandId::DetachAlgorithm},
      {"hot-reload-algorithm", DebugCommandId::HotReloadAlgorithm},
      {"replay-pipeline-stage", DebugCommandId::ReplayPipelineStage},
      {"request-timing-log", DebugCommandId::RequestTimingLog},
      {"export-pipeline-timing", DebugCommandId::ExportPipelineTiming},
      {"set-render-preview-request", DebugCommandId::SetRenderPreviewRequest},
      {"clear-render-preview-request", DebugCommandId::ClearRenderPreviewRequest},
      {"set-render-preview-extent", DebugCommandId::SetRenderPreviewExtent},
    };
    return registry;
  }

  static bool Execute(
    IDebugToolHost& backend,
    const DebugCommand& command,
    DebugCommandResult* out_result) {
    DebugCommandResult result{};
    std::string error_message;
    switch (command.id) {
      case DebugCommandId::SetRuntimeBuildFlavor:
        backend.SetAlgorithmRuntimeBuildFlavor(command.runtime_build_flavor);
        result.ok = true;
        break;
      case DebugCommandId::StartTick:
        backend.StartTicking();
        result.ok = true;
        break;
      case DebugCommandId::PauseTick:
        backend.PauseTicking();
        result.ok = true;
        break;
      case DebugCommandId::ClearAgents:
        backend.ClearAgents();
        result.ok = true;
        break;
      case DebugCommandId::ClearVkRuntimeCaches:
        backend.ClearVkRuntimeCaches();
        result.ok = true;
        break;
      case DebugCommandId::AttachAlgorithm:
        result.ok = backend.AttachAlgorithmToAgent(
          command.agent_index,
          command.algorithm_name,
          command.resource_bindings,
          command.descriptor_values,
          &result.algorithm_index,
          &error_message,
          command.mount_mode,
          command.execution_preference);
        break;
      case DebugCommandId::AttachPipelinePackage:
        result.ok = backend.AttachPipelinePackageToAgent(
          command.agent_index,
          command.pipeline_name,
          command.algorithm_name,
          command.resource_bindings,
          command.descriptor_values,
          &result.algorithm_index,
          &error_message,
          command.execution_preference);
        break;
      case DebugCommandId::EnqueuePipelineStage0:
        result.ok = backend.EnqueuePipelineStage0Submission(
          command.agent_index,
          command.pipeline_name,
          command.resource_bindings,
          command.descriptor_values,
          &error_message);
        break;
      case DebugCommandId::DetachAlgorithm:
        result.ok = backend.DetachAlgorithmFromAgent(
          command.agent_index,
          command.algorithm_index,
          &error_message);
        break;
      case DebugCommandId::HotReloadAlgorithm:
        result.ok = backend.HotReloadAlgorithmPackage(
          command.agent_index,
          command.algorithm_index,
          &result.algorithm_index,
          &error_message);
        break;
      case DebugCommandId::ReplayPipelineStage:
        result.ok = backend.ReplayPipelineStageBridgeDebug(
          command.agent_index,
          command.algorithm_index,
          &error_message);
        break;
      case DebugCommandId::RequestTimingLog:
        result.ok = backend.RequestAgentTimingLog(command.agent_index, &error_message);
        break;
      case DebugCommandId::ExportPipelineTiming:
        result.ok = backend.ExportPipelineTimingArtifacts(
          command.agent_index,
          command.pipeline_name,
          &result.csv_path,
          &result.mermaid_path,
          &error_message);
        break;
      case DebugCommandId::SetRenderPreviewRequest:
        backend.SetRenderPreviewRequest(command.preview_request);
        result.ok = true;
        break;
      case DebugCommandId::ClearRenderPreviewRequest:
        backend.SetRenderPreviewRequest({});
        result.ok = true;
        break;
      case DebugCommandId::SetRenderPreviewExtent:
        backend.SetRenderPreviewExtent(command.preview_extent);
        result.ok = true;
        break;
    }
    result.message = result.ok ? std::string{} : std::move(error_message);
    if (out_result) {
      *out_result = std::move(result);
    }
    return out_result ? out_result->ok : result.ok;
  }
};

}  // namespace debug_tool
