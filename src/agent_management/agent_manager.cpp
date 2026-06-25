#define AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD 1
#include "agent_manager.h"
#undef AGENT_MANAGEMENT_LAYER_INTERNAL_BUILD

#include "algorithm_support/algorithm_library_paths.h"

#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <chrono>
#include <utility>

namespace agent_management {

namespace {

std::string _SanitizePipelineTimingExportName(const std::string& name) {
  if (name.empty()) {
    return "pipeline";
  }

  std::string sanitized{};
  sanitized.reserve(name.size());
  for (const char ch : name) {
    const bool keep =
      (ch >= 'a' && ch <= 'z') ||
      (ch >= 'A' && ch <= 'Z') ||
      (ch >= '0' && ch <= '9') ||
      ch == '_' ||
      ch == '-';
    sanitized.push_back(keep ? ch : '_');
  }
  return sanitized.empty() ? std::string("pipeline") : sanitized;
}

std::filesystem::path _ResolvePipelineTimingExportDirectory() {
  const std::filesystem::path algorithm_library_root =
    algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot();
  const std::filesystem::path project_root =
    algorithm::library_paths::ResolveProjectRootFromAlgorithmLibraryRoot(algorithm_library_root);
  if (!project_root.empty()) {
    return project_root / "artifacts" / "pipeline_timing";
  }
  return std::filesystem::current_path() / "artifacts" / "pipeline_timing";
}

bool _ExportAlgorithmPipelineTimingArtifacts(
  const AlgorithmPipelineStallReport& report,
  std::string* out_csv_path,
  std::string* out_mermaid_path,
  std::string* out_error_message) {
  namespace fs = std::filesystem;

  const uint64_t export_stamp =
    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
  const std::string export_name =
    _SanitizePipelineTimingExportName(report.algorithm_name.empty() ? std::string("pipeline") : report.algorithm_name);
  const fs::path export_directory = _ResolvePipelineTimingExportDirectory();

  std::error_code create_directory_error;
  fs::create_directories(export_directory, create_directory_error);
  if (create_directory_error) {
    if (out_error_message) {
      *out_error_message =
        "Failed to create pipeline timing export directory: " + export_directory.string();
    }
    return false;
  }

  const fs::path csv_path =
    export_directory / (export_name + "_" + std::to_string(export_stamp) + ".csv");
  const fs::path mermaid_path =
    export_directory / (export_name + "_" + std::to_string(export_stamp) + ".mmd");

  {
    std::ofstream csv_file(csv_path, std::ios::binary | std::ios::trunc);
    if (!csv_file) {
      if (out_error_message) {
        *out_error_message = "Failed to open pipeline timing CSV export: " + csv_path.string();
      }
      return false;
    }
    csv_file << "stage_name,elapsed_seconds,reason\n";
    for (const auto& stage_stat : report.stage_runtime_stats) {
      std::string escaped_reason = stage_stat.reason;
      size_t comma_pos = escaped_reason.find('"');
      while (comma_pos != std::string::npos) {
        escaped_reason.insert(comma_pos, 1u, '"');
        comma_pos = escaped_reason.find('"', comma_pos + 2u);
      }
      csv_file << '"' << stage_stat.stage_name << '"' << ','
               << stage_stat.elapsed_seconds << ','
               << '"' << escaped_reason << '"' << '\n';
    }
  }

  {
    float max_elapsed_seconds = 0.0f;
    for (const auto& stage_stat : report.stage_runtime_stats) {
      max_elapsed_seconds = std::max(max_elapsed_seconds, stage_stat.elapsed_seconds);
    }
    if (max_elapsed_seconds <= 0.0f) {
      max_elapsed_seconds = 1.0f;
    }

    std::ofstream mermaid_file(mermaid_path, std::ios::binary | std::ios::trunc);
    if (!mermaid_file) {
      if (out_error_message) {
        *out_error_message = "Failed to open pipeline timing Mermaid export: " + mermaid_path.string();
      }
      return false;
    }

    mermaid_file << "xychart-beta\n";
    mermaid_file << "    title \"Pipeline Timing - "
                 << (report.algorithm_name.empty() ? "pipeline" : report.algorithm_name)
                 << "\"\n";
    mermaid_file << "    x-axis [";
    for (size_t i = 0u; i < report.stage_runtime_stats.size(); ++i) {
      if (i > 0u) {
        mermaid_file << ", ";
      }
      mermaid_file << '"' << report.stage_runtime_stats[i].stage_name << '"';
    }
    mermaid_file << "]\n";
    mermaid_file << "    y-axis \"seconds\" 0 --> " << max_elapsed_seconds << '\n';
    mermaid_file << "    bar [";
    for (size_t i = 0u; i < report.stage_runtime_stats.size(); ++i) {
      if (i > 0u) {
        mermaid_file << ", ";
      }
      mermaid_file << report.stage_runtime_stats[i].elapsed_seconds;
    }
    mermaid_file << "]\n";
  }

  if (out_csv_path) {
    *out_csv_path = csv_path.string();
  }
  if (out_mermaid_path) {
    *out_mermaid_path = mermaid_path.string();
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace

bool ExportAlgorithmPipelineTimingArtifacts(
  const AlgorithmPipelineStallReport& report,
  std::string* out_csv_path,
  std::string* out_mermaid_path,
  std::string* out_error_message) {
  return _ExportAlgorithmPipelineTimingArtifacts(
    report,
    out_csv_path,
    out_mermaid_path,
    out_error_message);
}

bool ReportAlgorithmPipelineStall(
  const AlgorithmPipelineStallReport& report,
  std::string* out_error_message) {
  std::string csv_export_path{};
  std::string mermaid_export_path{};
  std::string export_error_message{};
  if (!ExportAlgorithmPipelineTimingArtifacts(
        report,
        &csv_export_path,
        &mermaid_export_path,
        &export_error_message)) {
    if (out_error_message) {
      *out_error_message = export_error_message.empty()
        ? "Failed to export pipeline timing artifacts."
        : std::move(export_error_message);
    }
#ifndef NDEBUG
    assert(false && "Failed to export pipeline timing artifacts.");
#endif
    return false;
  }

  std::ostringstream stream;
  stream << "Pipeline algorithm stalled";
  if (!report.algorithm_name.empty()) {
    stream << " for '" << report.algorithm_name << "'";
  }
  if (report.stalled_seconds > 0.0f) {
    stream << " after " << report.stalled_seconds << "s without progress";
  }
  if (!report.reason.empty()) {
    stream << ": " << report.reason;
  }
  stream << '\n';
  for (const auto& stage_stat : report.stage_runtime_stats) {
    stream << "  stage " << stage_stat.stage_name << ": " << stage_stat.elapsed_seconds << "s";
    if (!stage_stat.reason.empty()) {
      stream << " | " << stage_stat.reason;
    }
    stream << '\n';
  }
  stream << "  timing csv: " << csv_export_path << '\n';
  stream << "  timing mermaid: " << mermaid_export_path << '\n';

  const std::string log_text = stream.str();
  std::cerr << log_text;
  if (out_error_message) {
    *out_error_message = log_text;
  }
#ifndef NDEBUG
  assert(false && "Pipeline algorithm stalled.");
#endif
  return false;
}

namespace {

bool _SignalBlocksTick(
  const agent::AlgorithmObject& object,
  const AgentToAlgorithmSignal& signal) {
  if ((signal.control_bits & kInterventionControlStopAndEditBit) != 0u) {
    return true;
  }
  if (signal.stop_requested || signal.pause_requested) {
    return true;
  }
  (void)object;
  (void)signal;
  return false;
}

}  // namespace

class AgentTicker {
 public:
  void Init(std::shared_ptr<agent::Agent> agent);
  void Tick(const InputState& input, Vec2 mouse_pixel, float dt_seconds);
  void Destroy();

  void ApplyInterventionRequest(const InteractionInterventionRequest& request);

  const AlgorithmToAgentSignal& algorithm_to_agent_signal() const { return algorithm_to_agent_signal_; }
  const InteractionInterventionRequest& intervention_request() const { return intervention_request_; }
  const std::string& last_timing_log() const { return last_timing_log_; }

 private:
  std::shared_ptr<agent::Agent> agent_binding_{};
  AlgorithmToAgentSignal algorithm_to_agent_signal_{};
  InteractionInterventionRequest intervention_request_{};
  std::string last_timing_log_{};
};

void AgentTicker::Init(std::shared_ptr<agent::Agent> agent) {
  agent_binding_ = std::move(agent);
  algorithm_to_agent_signal_ = {};
  intervention_request_ = {};
  intervention_request_.enabled = true;
  last_timing_log_.clear();
}

void AgentTicker::Tick(
  const InputState& input,
  Vec2 mouse_pixel,
  float dt_seconds) {
  algorithm_to_agent_signal_ = {};
  last_timing_log_.clear();
  if (!agent_binding_) {
    return;
  }

  const agent::AgentTickContext context{
    .input = &input,
    .mouse_pixel = mouse_pixel,
    .dt_seconds = dt_seconds,
    .intervention_request = &intervention_request_,
  };
  agent_binding_->RefreshInterventionSignals(context);

  std::vector<bool> allow_tick_mask(agent_binding_->algorithm_count(), true);
  for (size_t i = 0; i < agent_binding_->algorithm_count(); ++i) {
    const agent::AlgorithmObject* object = agent_binding_->algorithm_object(i);
    const agent::AgentAlgorithmRuntimeState* runtime_state = agent_binding_->algorithm_runtime_state(i);
    if (!object || !runtime_state) {
      allow_tick_mask[i] = false;
      continue;
    }
    if (_SignalBlocksTick(*object, runtime_state->agent_to_algorithm_signal)) {
      allow_tick_mask[i] = false;
    }
  }

  agent::AgentTickResult result{};
  if (agent_binding_->Tick(context, allow_tick_mask, &result)) {
    algorithm_to_agent_signal_ = result.algorithm_to_agent_signal;
    last_timing_log_ = std::move(result.timing_log);
  } else {
    algorithm_to_agent_signal_ = {};
    last_timing_log_.clear();
  }
}

void AgentTicker::Destroy() {
  agent_binding_.reset();
  algorithm_to_agent_signal_ = {};
  intervention_request_ = {};
  last_timing_log_.clear();
}

struct AgentManager::ManagedAgentEntry {
  std::shared_ptr<agent::Agent> agent{};
  AgentTicker ticker{};
  uint32_t limit_fps_flag{common_data::DefaultAgentLimitFpsFlag()};
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
    if (!managed_agent->ticker.last_timing_log().empty()) {
      std::cerr << managed_agent->ticker.last_timing_log();
    }
    if (managed_agent->agent) {
      const auto& algorithm_objects = managed_agent->agent->algorithm_objects();
      const auto& runtime_states = managed_agent->agent->algorithm_runtime_states();
      const size_t paired_count = std::min(algorithm_objects.size(), runtime_states.size());
      for (size_t i = 0; i < paired_count; ++i) {
        const agent::AlgorithmObject& object = algorithm_objects[i];
        const agent::AgentAlgorithmRuntimeState& runtime_state = runtime_states[i];
        if (!object.pipeline_stage || object.pipeline_stage_index != 0u) {
          continue;
        }
        if (!runtime_state.pipeline_stall_report_requested) {
          continue;
        }

        AlgorithmPipelineStallReport report{};
        report.algorithm_name = object.pipeline_name.empty()
          ? object.algorithm_profile.algorithm_name
          : object.pipeline_name;
        report.stalled_seconds = runtime_state.pipeline_no_progress_seconds;
        report.reason = runtime_state.pipeline_stall_reason.empty()
          ? "No observable pipeline progress."
          : runtime_state.pipeline_stall_reason;
        report.stage_runtime_stats = runtime_state.pipeline_stage_runtime_stats;

        std::string report_error_message;
        if (!ReportAlgorithmPipelineStall(report, &report_error_message)) {
          return false;
        }

        if (auto* mutable_runtime_state = managed_agent->agent->algorithm_runtime_state(i)) {
          mutable_runtime_state->pipeline_stall_report_requested = false;
        }
        combined_algorithm_to_agent_signal_.stop_requested = true;
        return false;
      }
    }
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

bool AgentManager::AttachPipelineAlgorithmToAgent(
  size_t agent_index,
  const std::string& pipeline_name,
  const std::vector<agent::AlgorithmPipelineStageSubmission>& stage_submissions,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  agent::AlgorithmExecutionPreference execution_preference,
  agent::AlgorithmPipelineTopology topology,
  agent::AlgorithmPipelineSyncMode sync_mode) {
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
  if (!managed_agents_[agent_index]->agent->MountPipelineAlgorithm(
        pipeline_name,
        stage_submissions,
        &algorithm_index,
        out_error_message,
        execution_preference,
        topology,
        sync_mode)) {
    assert(false && "Failed to mount pipeline algorithm.");
    if (out_error_message && out_error_message->empty()) {
      set_error("Failed to mount pipeline algorithm.");
    }
    return false;
  }
  managed_agents_[agent_index]->ResetTickBudget();

  if (out_algorithm_index) {
    *out_algorithm_index = algorithm_index;
  }
  return true;
}

bool AgentManager::EnqueuePipelineStage0Submission(
  size_t agent_index,
  const std::string& pipeline_name,
  const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
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

  if (!managed_agents_[agent_index]->agent->EnqueuePipelineStage0Submission(
        pipeline_name,
        resource_bindings,
        descriptor_values,
        out_error_message)) {
    assert(false && "Failed to enqueue pipeline stage0 submission.");
    if (out_error_message && out_error_message->empty()) {
      set_error("Failed to enqueue pipeline stage0 submission.");
    }
    return false;
  }

  managed_agents_[agent_index]->ResetTickBudget();
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool AgentManager::RequestAgentTimingLog(
  size_t agent_index,
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

  managed_agents_[agent_index]->agent->RequestTimingLog();
  managed_agents_[agent_index]->ResetTickBudget();
  if (out_error_message) {
    out_error_message->clear();
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

bool AgentManager::ReplayPipelineStageBridgeDebug(
  size_t agent_index,
  size_t algorithm_index,
  const agent::AgentTickContext& context,
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

  if (!managed_agents_[agent_index]->agent->ReplayPipelineStageBridgeDebug(
        algorithm_index,
        context,
        out_error_message)) {
    assert(false && "Failed to replay pipeline stage bridge debug input.");
    if (out_error_message && out_error_message->empty()) {
      set_error("Failed to replay pipeline stage bridge debug input.");
    }
    return false;
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
