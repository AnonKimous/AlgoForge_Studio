#include "algomanager/algorithm_manager.h"
#include "algomanager/scheduler/algorithm_scheduler.h"
#include "algomanager/bridge/algorithm_abi.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/bridge/algorithm_scheduler_facade.h"

#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtimesys/runtime_environment.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace algomanager { namespace algoscheduler {

void SetAlgorithmRuntimeShutdownHook() {
  runtimesys::SetRuntimeShutdownCallback(&ClearAlgorithmScheduler);
  runtimesys::SetRuntimeVkCacheClearCallback(&ClearAlgorithmExecutionCaches);
}

void ClearAlgorithmExecutionCaches() {
  runtimesys::ClearRuntimeVkJobCaches();
}



void ClearAlgorithmScheduler() {
  AlgorithmScheduler::Instance().Clear();
}

inline bool _TryBuildAlgorithmInterventionVkPhaseSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmPhaseSpec& phase_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

inline bool _TryBuildAlgorithmVkExecStageSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmVkExecSpec& vk_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message);

bool ExecuteJobsAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "Jobs algorithm execution output pointer is null.";
    }
    return false;
  }
  if (!object.jobs_executor) {
    if (out_error_message) {
      *out_error_message = "Jobs algorithm executor is unavailable.";
    }
    return false;
  }



  std::shared_ptr<::algomanager::algoscheduler::IAlgorithmJobsExecutor> jobs_executor = object.jobs_executor;
  algorithm::AlgorithmProfile algorithm_profile = object.algorithm_profile;
  const bool submit_ok = runtimesys::SubmitBlockingJob(
    context.job_priority == AlgorithmJobPriority::High
      ? runtimesys::RuntimeJobPriority::High
      : (context.job_priority == AlgorithmJobPriority::Normal
        ? runtimesys::RuntimeJobPriority::Normal
        : runtimesys::RuntimeJobPriority::Low),
    [
      jobs_executor,
      algorithm_profile,
      &context,
      &agent_to_algorithm_signal,
      container_set,
      out_algorithm_to_agent_signal,
      out_debug_state](std::string* out_job_error_message) {
      const bool ok = jobs_executor->ExecuteJobsAlgorithm(
        context,
        algorithm_profile,
        agent_to_algorithm_signal,
        container_set,
        out_algorithm_to_agent_signal,
        out_debug_state);
      if (!ok && out_job_error_message) {
        *out_job_error_message = "Jobs algorithm execution failed.";
      }
    },
    out_error_message);
  return submit_ok;
}

inline bool ExecuteCudaAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  common_data::AgentToAlgorithmSignal const& agent_to_algorithm_signal,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "CUDA algorithm execution output pointer is null.";
    }
    return false;
  }
  if (!object.cuda_executor) {
    if (out_error_message) {
      *out_error_message = "CUDA algorithm executor is unavailable.";
    }
    return false;
  }

  return runtimesys::SubmitBlockingJob(
    context.job_priority == AlgorithmJobPriority::High
      ? runtimesys::RuntimeJobPriority::High
      : (context.job_priority == AlgorithmJobPriority::Normal
        ? runtimesys::RuntimeJobPriority::Normal
        : runtimesys::RuntimeJobPriority::Low),
    [
      &object,
      &context,
      &agent_to_algorithm_signal,
      container_set,
      out_algorithm_to_agent_signal,
      out_debug_state](std::string* out_job_error_message) {
      const bool ok = object.cuda_executor->ExecuteCudaAlgorithm(
        context,
        object.algorithm_profile,
        agent_to_algorithm_signal,
        container_set,
        out_algorithm_to_agent_signal,
        out_debug_state);
      if (!ok && out_job_error_message) {
        *out_job_error_message = "CUDA algorithm execution failed.";
      }
    },
    out_error_message);
}

bool ExecuteVkAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  std::string* out_error_message) {
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "VK algorithm container set is unavailable.";
    }
    return false;
  }
  if (!object.vk_executor) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  ::algomanager::algoscheduler::AlgorithmVkExecSpec vk_exec_spec{};
  if (!object.vk_executor->GetVkExecSpec(&vk_exec_spec)) {
    if (out_error_message) {
       *out_error_message = "VK executor failed to provide its exec specification.";
    }
    return false;
  }

  runtimesys::RuntimeVkStageSubJob stage_job{};
  if (!_TryBuildAlgorithmVkExecStageSubJob(
        object,
        vk_exec_spec,
        container_set,
        &stage_job,
        out_error_message)) {
    return false;
  }

  runtimesys::RuntimeVkStageJob vk_job{};
  vk_job.debug_name = stage_job.debug_name;
  vk_job.shader_namespace = object.algorithm_profile.algorithm_name;
  vk_job.stage_name = stage_job.stage_name;
  vk_job.vertex_shader_path = stage_job.vertex_shader_path;
  vk_job.fragment_shader_path = stage_job.fragment_shader_path;
  vk_job.viewport_width = std::max(context.render_preview_extent.x, 1.0f);
  vk_job.viewport_height = std::max(context.render_preview_extent.y, 1.0f);
  vk_job.execution_key = container_set;
  vk_job.buffer_bindings = stage_job.buffer_bindings;
  vk_job.stage_jobs.push_back(std::move(stage_job));
  return runtimesys::ExecuteRuntimeVkJob(vk_job, out_error_message);
}

inline bool HasExecutableCudaAlgorithmStage(const ::algomanager::algoscheduler::AlgorithmObject& object) {
  return object.cuda_executor != nullptr;
}

bool HasExecutableVkAlgorithmStage(const ::algomanager::algoscheduler::AlgorithmObject& object) {
  if (!object.vk_executor) {
    return false;
  }
  ::algomanager::algoscheduler::AlgorithmVkExecSpec vk_exec_spec{};
  return object.vk_executor->GetVkExecSpec(&vk_exec_spec) &&
    !vk_exec_spec.shader.vertex_shader_path.empty() &&
    !vk_exec_spec.shader.fragment_shader_path.empty() &&
    !vk_exec_spec.used_algorithm_containers.empty();
}

bool SynchronizeVkAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "VK algorithm container set is unavailable.";
    }
    return false;
  }
  if (!object.vk_executor) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  ::algomanager::algoscheduler::AlgorithmVkExecSpec vk_exec_spec{};
  if (!object.vk_executor->GetVkExecSpec(&vk_exec_spec)) {
    if (out_error_message) {
       *out_error_message = "VK executor failed to provide its exec specification.";
    }
    return false;
  }

  runtimesys::RuntimeVkStageSubJob stage_job{};
  if (!_TryBuildAlgorithmVkExecStageSubJob(
        object,
        vk_exec_spec,
        container_set,
        &stage_job,
        out_error_message)) {
    return false;
  }

  runtimesys::RuntimeVkStageJob vk_job{};
  vk_job.debug_name = stage_job.debug_name;
  vk_job.shader_namespace = object.algorithm_profile.algorithm_name;
  vk_job.stage_name = stage_job.stage_name;
  vk_job.vertex_shader_path = stage_job.vertex_shader_path;
  vk_job.fragment_shader_path = stage_job.fragment_shader_path;
  vk_job.execution_key = container_set;
  vk_job.buffer_bindings = stage_job.buffer_bindings;
  vk_job.stage_jobs.push_back(std::move(stage_job));
  return runtimesys::SynchronizeRuntimeVkJob(vk_job, out_error_message);
}

inline bool SynchronizeCudaAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  (void)object;
  (void)container_set;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

inline std::string _ResolveAlgorithmVkShaderPath(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_error_message) {
  return ::algomanager::bridge::execution_bridge_support::ResolveAlgorithmVkShaderPath(
    object,
    shader_path,
    out_error_message);
}

inline bool _TryBuildAlgorithmInterventionVkPhaseSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmPhaseSpec& phase_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message) {
  return ::algomanager::bridge::execution_bridge_support::TryBuildAlgorithmInterventionVkPhaseSubJob(
    object,
    phase_spec,
    container_set,
    out_stage_job,
    out_error_message);
}

inline bool _TryBuildAlgorithmVkExecStageSubJob(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AlgorithmVkExecSpec& vk_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message) {
  return ::algomanager::bridge::execution_bridge_support::TryBuildAlgorithmVkExecStageSubJob(
    object,
    vk_exec_spec,
    container_set,
    out_stage_job,
    out_error_message);
}

bool ExecuteAlgorithmObjectStagePlan(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "Algorithm execution output pointer is null.";
    }
    return false;
  }
  struct StageEntry {
    ::algomanager::algoscheduler::AlgorithmExecutionPhase execution_phase{::algomanager::algoscheduler::AlgorithmExecutionPhase::Exec};
    ::algomanager::algoscheduler::AlgorithmExecutionPreference execution_preference{::algomanager::algoscheduler::AlgorithmExecutionPreference::Jobs};
    const ::algomanager::algoscheduler::AlgorithmPhaseSpec* intervention_phase{nullptr};
    bool exec_stage{false};
    bool reflect_stage{false};
  };

  std::vector<::algomanager::algoscheduler::AlgorithmPhaseSpec> intervention_phase_specs{};
  if (object.intervention &&
      !object.intervention->GetInterventionPhaseSpecs(&intervention_phase_specs)) {
    if (out_error_message) {
      *out_error_message = "Algorithm intervention phase specifications are unavailable.";
    }
    return false;
  }

  const ::algomanager::algoscheduler::AlgorithmPhaseSpec* pretick_phase{nullptr};
  const ::algomanager::algoscheduler::AlgorithmPhaseSpec* aftertick_phase{nullptr};
  const ::algomanager::algoscheduler::AlgorithmPhaseSpec* renderresult_phase{nullptr};
  for (const ::algomanager::algoscheduler::AlgorithmPhaseSpec& phase_spec : intervention_phase_specs) {
    switch (phase_spec.stage_kind) {
      case ::algomanager::algoscheduler::AlgorithmPhaseKind::Pretick:
        pretick_phase = &phase_spec;
        break;
      case ::algomanager::algoscheduler::AlgorithmPhaseKind::AfterTick:
        aftertick_phase = &phase_spec;
        break;
      case ::algomanager::algoscheduler::AlgorithmPhaseKind::ResultRender:
        renderresult_phase = &phase_spec;
        break;
      case ::algomanager::algoscheduler::AlgorithmPhaseKind::Exec:
      case ::algomanager::algoscheduler::AlgorithmPhaseKind::Reflect:
      case ::algomanager::algoscheduler::AlgorithmPhaseKind::Custom:
        break;
    }
  }

  std::vector<StageEntry> stage_entries{};
  if (pretick_phase) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algomanager::algoscheduler::AlgorithmExecutionPhase::Pretick,
      .execution_preference = pretick_phase->execution_preference,
      .intervention_phase = pretick_phase,
    });
  }
  if (object.jobs_executor || object.vk_executor || object.cuda_executor) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algomanager::algoscheduler::AlgorithmExecutionPhase::Exec,
      .execution_preference = object.execution_preference,
      .exec_stage = true,
    });
  }
  if (aftertick_phase) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algomanager::algoscheduler::AlgorithmExecutionPhase::AfterTick,
      .execution_preference = aftertick_phase->execution_preference,
      .intervention_phase = aftertick_phase,
    });
  }
  if (renderresult_phase) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algomanager::algoscheduler::AlgorithmExecutionPhase::RenderResult,
      .execution_preference = renderresult_phase->execution_preference,
      .intervention_phase = renderresult_phase,
    });
  }
  if (object.algorithm_reflector && !object.algorithm_reflector->empty()) {
    stage_entries.push_back(StageEntry{
      .execution_phase = ::algomanager::algoscheduler::AlgorithmExecutionPhase::Reflect,
      .execution_preference = ::algomanager::algoscheduler::AlgorithmExecutionPreference::Jobs,
      .reflect_stage = true,
    });
  }

  for (size_t stage_offset = 0u; stage_offset < stage_entries.size(); ) {
    const ::algomanager::algoscheduler::AlgorithmExecutionPreference bundle_preference =
      stage_entries[stage_offset].execution_preference;
    const size_t bundle_begin = stage_offset;
    size_t bundle_end = stage_offset + 1u;
    while (bundle_end < stage_entries.size() &&
           pipeline_scheduler_detail::AreExecutionPreferencesCompatible(
             stage_entries[bundle_end].execution_preference,
             bundle_preference)) {
      ++bundle_end;
    }
    if (bundle_preference == ::algomanager::algoscheduler::AlgorithmExecutionPreference::Vk) {
      runtimesys::RuntimeVkStageJob vk_job{};
      vk_job.shader_namespace = object.algorithm_profile.algorithm_name;
      vk_job.execution_key = container_set;
      vk_job.viewport_width = std::max(context.render_preview_extent.x, 1.0f);
      vk_job.viewport_height = std::max(context.render_preview_extent.y, 1.0f);
      vk_job.host_ingress_authoritative = true;
      for (size_t index = bundle_begin; index < bundle_end; ++index) {
        const StageEntry& entry = stage_entries[index];
        runtimesys::RuntimeVkStageSubJob stage_job{};
        if (entry.exec_stage) {
          if (!object.vk_executor) {
            if (out_error_message) {
               *out_error_message = "VK exec stage is unavailable.";
            }
            return false;
          }
          ::algomanager::algoscheduler::AlgorithmVkExecSpec vk_exec_spec{};
          if (!object.vk_executor->GetVkExecSpec(&vk_exec_spec)) {
            if (out_error_message) {
               *out_error_message = "VK exec stage failed to provide its execution spec.";
            }
            return false;
          }
          if (!_TryBuildAlgorithmVkExecStageSubJob(
                object,
                vk_exec_spec,
                container_set,
                &stage_job,
                out_error_message)) {
            return false;
          }
        } else if (entry.intervention_phase) {
          if (!_TryBuildAlgorithmInterventionVkPhaseSubJob(
                object,
                *entry.intervention_phase,
                container_set,
                &stage_job,
                out_error_message)) {
            return false;
          }
        } else {
          if (out_error_message) {
             *out_error_message = "VK bundle contains an unsupported stage.";
          }
          return false;
        }
        vk_job.stage_jobs.push_back(std::move(stage_job));
      }
      if (vk_job.stage_jobs.empty()) {
        if (out_error_message) {
           *out_error_message = "VK bundle does not contain any executable stages.";
        }
        return false;
      }
      vk_job.debug_name = vk_job.stage_jobs.front().debug_name;
      vk_job.stage_name = vk_job.stage_jobs.front().stage_name;
      vk_job.vertex_shader_path = vk_job.stage_jobs.front().vertex_shader_path;
      vk_job.fragment_shader_path = vk_job.stage_jobs.front().fragment_shader_path;
      vk_job.buffer_bindings = vk_job.stage_jobs.front().buffer_bindings;
      if (!runtimesys::ExecuteRuntimeVkJob(vk_job, out_error_message)) {
        std::cerr
          << "vk_bundle.execute.failed algorithm=" << object.algorithm_profile.algorithm_name
          << " bundle_begin=" << bundle_begin
          << " bundle_end=" << bundle_end
          << " error=" << (out_error_message ? *out_error_message : std::string{})
          << '\n';
        return false;
      }
      if (!runtimesys::SynchronizeRuntimeVkJob(vk_job, out_error_message)) {
        std::cerr
          << "vk_bundle.sync.failed algorithm=" << object.algorithm_profile.algorithm_name
          << " bundle_begin=" << bundle_begin
          << " bundle_end=" << bundle_end
          << " error=" << (out_error_message ? *out_error_message : std::string{})
          << '\n';
        return false;
      }
    } else if (bundle_preference == ::algomanager::algoscheduler::AlgorithmExecutionPreference::Cuda) {
      for (size_t index = bundle_begin; index < bundle_end; ++index) {
        const StageEntry& entry = stage_entries[index];
        if (!entry.exec_stage) {
          continue;
        }
        ::algomanager::algoscheduler::AgentTickContext stage_context = context;
        stage_context.execution_phase = entry.execution_phase;
        if (!ExecuteCudaAlgorithmObject(
              object,
              container_set,
              stage_context,
              agent_to_algorithm_signal,
              out_algorithm_to_agent_signal,
              out_debug_state,
              out_error_message)) {
          return false;
        }
      }
    } else {
      for (size_t index = bundle_begin; index < bundle_end; ++index) {
        const StageEntry& entry = stage_entries[index];
        if (!entry.exec_stage || entry.reflect_stage) {
          continue;
        }

        ::algomanager::algoscheduler::AgentTickContext stage_context = context;
        stage_context.execution_phase = entry.execution_phase;
        if (!ExecuteJobsAlgorithmObject(
              object,
              stage_context,
              agent_to_algorithm_signal,
              container_set,
              out_algorithm_to_agent_signal,
              out_debug_state,
              out_error_message)) {
          return false;
        }
      }
    }

    stage_offset = bundle_end;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

AlgorithmScheduler& AlgorithmScheduler::Instance() {
  static AlgorithmScheduler instance{};
  static const bool hook_registered = []() {
    SetAlgorithmRuntimeShutdownHook();
    return true;
  }();
  (void)hook_registered;
  return instance;
}

void AlgorithmScheduler::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  pipeline_registrations_.clear();
  pipeline_runtime_states_.clear();
  pipeline_runtime_ref_counts_.clear();
  debug_tool_recording_ = false;
  debug_tool_recording_tick_count_ = 0u;
  ClearAlgorithmExecutionCaches();
}

void AlgorithmScheduler::BeginDebugToolRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  debug_tool_recording_ = true;
  debug_tool_recording_tick_count_ = 0u;
}

void AlgorithmScheduler::EndDebugToolRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  debug_tool_recording_ = false;
}

bool AlgorithmScheduler::DebugToolRecording() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return debug_tool_recording_;
}

uint64_t AlgorithmScheduler::DebugToolRecordingTickCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return debug_tool_recording_tick_count_;
}

void AlgorithmScheduler::RecordDebugToolTick() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (debug_tool_recording_) {
    ++debug_tool_recording_tick_count_;
  }
}

bool AlgorithmScheduler::SubmitAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "Algorithm submit output pointer is null.";
    }
    return false;
  }
  if (object.execution_preference == AlgorithmExecutionPreference::Compatibility) {
    return ExecuteCompatibilityAlgorithmObject(
      object,
      context,
      agent_to_algorithm_signal,
      container_set,
      out_algorithm_to_agent_signal,
      out_debug_state,
      out_error_message);
  }
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "Algorithm container set is unavailable.";
    }
    return false;
  }
  const bool ok = ExecuteAlgorithmObjectStagePlan(
    object,
    agent_to_algorithm_signal,
    container_set,
    context,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
  if (ok && out_error_message) {
    out_error_message->clear();
  }
  return ok;
}

bool AlgorithmScheduler::RegisterPipeline(
  const JobsPipelineRegistration& registration,
  std::string* out_error_message) {
  if (registration.pipeline_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Jobs pipeline registration requires a pipeline name.";
    }
    return false;
  }
  if (registration.root_stage_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Jobs pipeline registration requires a root stage name.";
    }
    return false;
  }
  if (registration.stage_count == 0u) {
    if (out_error_message) {
      *out_error_message = "Jobs pipeline registration requires at least one stage.";
    }
    return false;
  }
  if (registration.mandatory_stage_buffer_slot_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Jobs pipeline registration requires the mandatory stage buffer slot name.";
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = pipeline_registrations_.find(registration.pipeline_name);
  if (found != pipeline_registrations_.end()) {
    if (found->second.root_stage_name != registration.root_stage_name ||
        found->second.stage_count != registration.stage_count ||
        found->second.body_begin_stage_index != registration.body_begin_stage_index ||
        found->second.body_stage_count != registration.body_stage_count ||
        found->second.effective_tail_stage_index != registration.effective_tail_stage_index ||
        found->second.topology != registration.topology ||
        found->second.sync_mode != registration.sync_mode ||
        found->second.max_concurrent_stage0_submissions != registration.max_concurrent_stage0_submissions ||
        found->second.mandatory_stage_buffer_slot_name != registration.mandatory_stage_buffer_slot_name) {
      if (out_error_message) {
        *out_error_message =
          "Jobs pipeline registration conflicts with an existing mounted pipeline: " + registration.pipeline_name;
      }
      return false;
    }
  } else {
    pipeline_registrations_.emplace(registration.pipeline_name, registration);
  }
  pipeline_runtime_states_.erase(registration.pipeline_name);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool AlgorithmScheduler::RegisterPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const JobsPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  if (pipeline_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Jobs pipeline runtime registration requires a pipeline name.";
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto registration_it = pipeline_registrations_.find(pipeline_name);
  if (registration_it != pipeline_registrations_.end()) {
    const JobsPipelineRegistration& registration = registration_it->second;
    if (runtime_state.mandatory_stage_buffer_slot_name != registration.mandatory_stage_buffer_slot_name) {
      if (out_error_message) {
        *out_error_message = "Jobs pipeline runtime registration stage buffer slot does not match the registration.";
      }
      return false;
    }
  }
  pipeline_runtime_states_[pipeline_name][owner_agent_name] = runtime_state;
  ++pipeline_runtime_ref_counts_[pipeline_name][owner_agent_name];
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

void AlgorithmScheduler::UnregisterPipeline(
  const std::string& pipeline_name,
  const std::string& owner_agent_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto ref_count_it = pipeline_runtime_ref_counts_.find(pipeline_name);
  if (ref_count_it != pipeline_runtime_ref_counts_.end()) {
    const auto owner_ref_count_it = ref_count_it->second.find(owner_agent_name);
    if (owner_ref_count_it != ref_count_it->second.end()) {
      if (owner_ref_count_it->second > 1u) {
        --owner_ref_count_it->second;
        return;
      }
      ref_count_it->second.erase(owner_ref_count_it);
    }
    if (ref_count_it->second.empty()) {
      pipeline_runtime_ref_counts_.erase(ref_count_it);
    }
  }
  const auto runtime_states_it = pipeline_runtime_states_.find(pipeline_name);
  if (runtime_states_it != pipeline_runtime_states_.end()) {
    runtime_states_it->second.erase(owner_agent_name);
    if (runtime_states_it->second.empty()) {
      pipeline_runtime_states_.erase(runtime_states_it);
    }
  }
}

bool AlgorithmScheduler::TryGetPipelineRegistration(
  const std::string& pipeline_name,
  JobsPipelineRegistration* out_registration) const {
  if (!out_registration || pipeline_name.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = pipeline_registrations_.find(pipeline_name);
  if (found == pipeline_registrations_.end()) {
    return false;
  }
  *out_registration = found->second;
  return true;
}

bool AlgorithmScheduler::TryGetPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  JobsPipelineRuntimeState* out_runtime_state) const {
  if (!out_runtime_state || pipeline_name.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = pipeline_runtime_states_.find(pipeline_name);
  if (found == pipeline_runtime_states_.end()) {
    return false;
  }
  const auto owner_found = found->second.find(owner_agent_name);
  if (owner_found == found->second.end()) {
    return false;
  }
  *out_runtime_state = owner_found->second;
  return true;
}

bool AlgorithmScheduler::UpdatePipelineRuntime(
  const std::string& pipeline_name,
  const std::string& owner_agent_name,
  const JobsPipelineRuntimeState& runtime_state,
  std::string* out_error_message) {
  std::lock_guard<std::mutex> lock(mutex_);
  pipeline_runtime_states_[pipeline_name][owner_agent_name] = runtime_state;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool AlgorithmScheduler::TickMountedPipeline(
  std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& owner_agent_name,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<::algomanager::bridge::AlgorithmAssemblyState>& assembly_states,
  bool collect_pipeline_timing,
  std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_pipeline_processing_failed,
  std::string* out_error_message) {
  auto set_error = [&](std::string message) {
    if (out_error_message) {
      *out_error_message = std::move(message);
    }
  };
  if (!mounted_objects || !inout_runtime_states || !out_pipeline_signal || !out_pipeline_processing_failed) {
    set_error("Mounted pipeline tick received a null output pointer.");
    return false;
  }
  if (begin_index >= end_index || end_index > mounted_objects->size()) {
    set_error("Mounted pipeline tick received an invalid pipeline range.");
    return false;
  }

  std::vector<::algomanager::bridge::AlgorithmObject>& algorithm_objects = *mounted_objects;
  std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>& updated_runtime_states =
    *inout_runtime_states;
  updated_runtime_states.resize(algorithm_objects.size());
  *out_pipeline_signal = {};
  *out_pipeline_processing_failed = false;

  ::algomanager::bridge::AlgorithmObject& root_object = algorithm_objects[begin_index];
  JobsPipelineRegistration registration{};
  JobsPipelineRuntimeState pipeline_state{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto registration_it = pipeline_registrations_.find(root_object.pipeline_name);
    if (registration_it == pipeline_registrations_.end()) {
      set_error("Mounted pipeline registration is unavailable.");
      return false;
    }
    registration = registration_it->second;
    const auto runtime_it = pipeline_runtime_states_.find(root_object.pipeline_name);
    if (runtime_it == pipeline_runtime_states_.end()) {
      set_error("Mounted pipeline runtime state is unavailable.");
      return false;
    }
    const auto owner_runtime_it = runtime_it->second.find(owner_agent_name);
    if (owner_runtime_it == runtime_it->second.end()) {
      set_error("Mounted pipeline runtime state is unavailable for the selected agent.");
      return false;
    }
    pipeline_state = owner_runtime_it->second;
  }
  const bool pipeline_stage_debug_all = root_object.pipeline_stage_debug_all;
  const uint32_t pipeline_stage_debug_index = root_object.pipeline_stage_debug_index;
  const size_t pipeline_stage_count = end_index - begin_index;
  const size_t body_begin_index = begin_index + static_cast<size_t>(registration.body_begin_stage_index);
  const size_t body_stage_count = static_cast<size_t>(registration.body_stage_count);
  const size_t body_end_index = body_begin_index + body_stage_count;
  const size_t effective_tail_index = begin_index + static_cast<size_t>(registration.effective_tail_stage_index);
  const bool has_wrapper_begin = static_cast<size_t>(registration.body_begin_stage_index) > 0u;
  const bool has_wrapper_end = effective_tail_index >= body_end_index && effective_tail_index < end_index;
  ALGORITHM_SCHEDULER_ASSERT(
    body_begin_index < end_index && body_stage_count > 0u && body_end_index <= end_index,
    "Mounted pipeline body stage range is invalid.");
  if (body_begin_index >= end_index || body_stage_count == 0u || body_end_index > end_index) {
    set_error("Mounted pipeline body stage range is invalid.");
    out_pipeline_signal->stop_requested = true;
    *out_pipeline_processing_failed = true;
    return false;
  }
  ::algomanager::bridge::AlgorithmObject& body_stage0_object = algorithm_objects[body_begin_index];
  const bool pipeline_uses_inter_stage_buffer =
    body_stage0_object.execution_preference == ::algomanager::bridge::AlgorithmExecutionPreference::Jobs;

  if (pipeline_state.lanes.empty()) {
    pipeline_scheduler_detail::PipelineLaneRuntimeState fallback_lane_state{};
    std::string fallback_lane_error_message;
    if (!pipeline_scheduler_detail::TryBuildInitialPipelineLaneRuntimeState(
          body_stage0_object,
          pipeline_stage_count,
          owner_agent_name,
          pipeline_state.topology == ::algomanager::bridge::AlgorithmPipelineTopology::Circular,
          pipeline_state.next_lane_id,
          pipeline_state.mandatory_stage_buffer_slot_name,
          &fallback_lane_state,
          &fallback_lane_error_message)) {
      const std::string failure_message = fallback_lane_error_message.empty()
        ? std::string("Failed to rebuild the primary pipeline lane runtime state.")
        : std::move(fallback_lane_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      set_error(failure_message);
      out_pipeline_signal->stop_requested = true;
      *out_pipeline_processing_failed = true;
      return false;
    }
    pipeline_state.current_lane_id = fallback_lane_state.lane_id;
    pipeline_state.lanes.push_back(std::move(fallback_lane_state));
    ++pipeline_state.next_lane_id;
  }
  pipeline_scheduler_detail::SyncPipelineLegacyStageStateFromPrimaryLane(&pipeline_state, pipeline_stage_count);
  if (pipeline_state.stage_has_data.size() != pipeline_stage_count) {
    pipeline_state.stage_has_data.assign(pipeline_stage_count, false);
  }
  pipeline_scheduler_detail::PipelineLaneRuntimeState* primary_lane_state =
    pipeline_scheduler_detail::FindPrimaryPipelineLaneRuntimeState(&pipeline_state);
  ALGORITHM_SCHEDULER_ASSERT(
    primary_lane_state != nullptr,
    "Primary pipeline lane runtime state is unavailable.");
  if (!primary_lane_state) {
    set_error("Primary pipeline lane runtime state is unavailable.");
    out_pipeline_signal->stop_requested = true;
    *out_pipeline_processing_failed = true;
    return false;
  }

  pipeline_scheduler_detail::PipelineGroupProgressState previous_progress_state{};
  if (begin_index < updated_runtime_states.size()) {
    previous_progress_state.signature = updated_runtime_states[begin_index].pipeline_progress_signature;
    previous_progress_state.signature_valid =
      updated_runtime_states[begin_index].pipeline_progress_signature_valid;
    previous_progress_state.no_progress_seconds =
      updated_runtime_states[begin_index].pipeline_no_progress_seconds;
    previous_progress_state.stall_report_requested =
      updated_runtime_states[begin_index].pipeline_stall_report_requested;
    previous_progress_state.stall_reported =
      updated_runtime_states[begin_index].pipeline_stall_reported;
    previous_progress_state.stall_reason =
      updated_runtime_states[begin_index].pipeline_stall_reason;
    if (collect_pipeline_timing) {
      previous_progress_state.stage_runtime_stats =
        updated_runtime_states[begin_index].pipeline_stage_runtime_stats;
    }
  }
  pipeline_scheduler_detail::PipelineGroupProgressState progress_state = previous_progress_state;
  progress_state.stall_reason.clear();
  std::vector<::algomanager::bridge::AlgorithmPipelineStageRuntimeStat>* pipeline_stage_runtime_stats =
    collect_pipeline_timing ? &progress_state.stage_runtime_stats : nullptr;
  if (collect_pipeline_timing) {
    progress_state.stage_runtime_stats.clear();
    progress_state.stage_runtime_stats.reserve(pipeline_stage_count);
  } else {
    progress_state.stage_runtime_stats.clear();
  }

  ALGORITHM_SCHEDULER_ASSERT(
    has_wrapper_begin && has_wrapper_end,
    "Mounted pipeline must expose concrete wrapper begin/end stages.");
  if (!has_wrapper_begin || !has_wrapper_end) {
    set_error("Mounted pipeline must expose concrete wrapper begin/end stages.");
    out_pipeline_signal->stop_requested = true;
    *out_pipeline_processing_failed = true;
    return false;
  }

  const size_t body_begin_offset = body_begin_index - begin_index;
  std::vector<bool> current_body_stage_has_data(body_stage_count, false);
  auto build_body_stage_has_data = [&](const std::vector<bool>& full_stage_has_data) {
    std::vector<bool> body_stage_has_data(body_stage_count, false);
    for (size_t body_offset = 0u; body_offset < body_stage_count; ++body_offset) {
      const size_t pipeline_stage_offset = body_begin_offset + body_offset;
      if (pipeline_stage_offset < full_stage_has_data.size()) {
        body_stage_has_data[body_offset] = full_stage_has_data[pipeline_stage_offset];
      }
    }
    return body_stage_has_data;
  };
  auto build_full_stage_has_data = [&](const std::vector<bool>& body_stage_has_data) {
    std::vector<bool> full_stage_has_data(pipeline_stage_count, false);
    for (size_t body_offset = 0u; body_offset < body_stage_has_data.size(); ++body_offset) {
      const size_t pipeline_stage_offset = body_begin_offset + body_offset;
      if (pipeline_stage_offset < full_stage_has_data.size()) {
        full_stage_has_data[pipeline_stage_offset] = body_stage_has_data[body_offset];
      }
    }
    return full_stage_has_data;
  };
  current_body_stage_has_data = build_body_stage_has_data(primary_lane_state->stage_has_data);
  body_stage0_object.resource_bindings = primary_lane_state->resource_bindings;
  body_stage0_object.descriptor_values = primary_lane_state->descriptor_values;

  auto resolve_pipeline_active_stage = [&](
    const std::vector<bool>& stage_has_data,
    const std::vector<size_t>& executable_stage_indices,
    size_t* out_active_stage_index,
    bool* out_active_stage_valid) {
    *out_active_stage_index = 0u;
    *out_active_stage_valid = false;
    for (size_t stage_offset = stage_has_data.size(); stage_offset > 0u; --stage_offset) {
      if (stage_has_data[stage_offset - 1u]) {
        *out_active_stage_index = stage_offset - 1u;
        *out_active_stage_valid = true;
        return;
      }
    }
    if (!executable_stage_indices.empty()) {
      *out_active_stage_index = executable_stage_indices.back() - begin_index;
      *out_active_stage_valid = true;
    }
  };

  auto resolve_pipeline_active_bundle = [&](
    const std::vector<bool>& stage_has_data,
    const std::vector<size_t>& executable_stage_indices,
    size_t* out_bundle_begin_stage_index,
    size_t* out_bundle_stage_count,
    ::algomanager::bridge::AlgorithmExecutionPreference* out_bundle_preference,
    bool* out_bundle_valid) {
    *out_bundle_begin_stage_index = 0u;
    *out_bundle_stage_count = 0u;
    *out_bundle_preference = ::algomanager::bridge::AlgorithmExecutionPreference::Vk;
    *out_bundle_valid = false;

    size_t active_stage_index = 0u;
    bool active_stage_valid = false;
    resolve_pipeline_active_stage(
      stage_has_data,
      executable_stage_indices,
      &active_stage_index,
      &active_stage_valid);
    if (!active_stage_valid) {
      return;
    }

    const size_t body_bundle_begin_stage_offset = body_begin_index - begin_index;
    const size_t body_bundle_end_stage_offset = body_bundle_begin_stage_offset + body_stage_count;
    if (active_stage_index < body_bundle_begin_stage_offset ||
        active_stage_index >= body_bundle_end_stage_offset) {
      const size_t active_object_index = begin_index + active_stage_index;
      if (active_object_index < algorithm_objects.size() &&
          algorithm_objects[active_object_index].pipeline_wrapper_role !=
            ::algomanager::bridge::AlgorithmPipelineWrapperRole::None) {
        *out_bundle_begin_stage_index = active_stage_index;
        *out_bundle_stage_count = 1u;
        *out_bundle_preference = algorithm_objects[active_object_index].execution_preference;
        *out_bundle_valid = true;
      }
      return;
    }

    const ::algomanager::bridge::AlgorithmExecutionPreference bundle_preference =
      algorithm_objects[begin_index + active_stage_index].execution_preference;
    size_t bundle_begin_stage_index = active_stage_index;
    while (bundle_begin_stage_index > 0u) {
      size_t previous_stage_offset = bundle_begin_stage_index - 1u;
      while (previous_stage_offset > 0u && !stage_has_data[previous_stage_offset]) {
        --previous_stage_offset;
      }
      const size_t previous_stage_index = begin_index + previous_stage_offset;
      if (!stage_has_data[previous_stage_offset] ||
          !pipeline_scheduler_detail::AreExecutionPreferencesCompatible(
            algorithm_objects[previous_stage_index].execution_preference,
            bundle_preference)) {
        break;
      }
      bundle_begin_stage_index = previous_stage_offset;
    }

    size_t bundle_end_stage_index = active_stage_index + 1u;
    while (bundle_end_stage_index < stage_has_data.size()) {
      if (!stage_has_data[bundle_end_stage_index]) {
        ++bundle_end_stage_index;
        continue;
      }
      const size_t next_stage_index = begin_index + bundle_end_stage_index;
      if (!pipeline_scheduler_detail::AreExecutionPreferencesCompatible(
            algorithm_objects[next_stage_index].execution_preference,
            bundle_preference)) {
        break;
      }
      ++bundle_end_stage_index;
    }

    *out_bundle_begin_stage_index = bundle_begin_stage_index;
    *out_bundle_stage_count = bundle_end_stage_index - bundle_begin_stage_index;
    *out_bundle_preference = bundle_preference;
    *out_bundle_valid = true;
  };

  bool imported_submission_this_tick = false;
  if (!current_body_stage_has_data.empty() &&
      pipeline_scheduler_detail::CountLivePipelineStages(current_body_stage_has_data) == 0u &&
      !pipeline_state.pending_stage0_submissions.empty()) {
    ::algomanager::bridge::AlgorithmObject& stage0_object = body_stage0_object;
    JobsPendingPipelineStage0Submission submission =
      std::move(pipeline_state.pending_stage0_submissions.front());
    pipeline_state.pending_stage0_submissions.erase(pipeline_state.pending_stage0_submissions.begin());
    if (pipeline_scheduler_detail::CountLivePipelineStages(current_body_stage_has_data) == 0u &&
        pipeline_state.current_lane_id != 0u &&
        pipeline_state.current_lane_id != submission.lane_id) {
      if (pipeline_scheduler_detail::PipelineLaneRuntimeState* previous_lane_state =
            pipeline_scheduler_detail::FindPipelineLaneRuntimeStateById(
              &pipeline_state,
              pipeline_state.current_lane_id)) {
        previous_lane_state->valid = false;
      }
    }
    submission.owner_agent_name = owner_agent_name;
    pipeline_state.current_lane_id = submission.lane_id;
    primary_lane_state = pipeline_scheduler_detail::FindPrimaryPipelineLaneRuntimeState(&pipeline_state);
    ALGORITHM_SCHEDULER_ASSERT(
      primary_lane_state != nullptr,
      "Submitted pipeline lane runtime state is unavailable.");
    if (!primary_lane_state) {
      set_error("Submitted pipeline lane runtime state is unavailable.");
      out_pipeline_signal->stop_requested = true;
      *out_pipeline_processing_failed = true;
      return false;
    }
    stage0_object.resource_bindings = submission.resource_bindings;
    stage0_object.descriptor_values = submission.descriptor_values;
    primary_lane_state->owner_agent_name = owner_agent_name;
    primary_lane_state->lane_id = submission.lane_id;
    primary_lane_state->loop_lane_active = submission.loop_lane_active;
    primary_lane_state->resource_bindings = stage0_object.resource_bindings;
    primary_lane_state->descriptor_values = stage0_object.descriptor_values;
    primary_lane_state->stage_has_data.assign(pipeline_stage_count, false);
    current_body_stage_has_data.assign(body_stage_count, false);
    current_body_stage_has_data.front() = true;
    pipeline_state.stage0_saturated = false;
    imported_submission_this_tick = true;
  }

  for (size_t index = begin_index; index < end_index; ++index) {
    algorithm_objects[index].SetContainerSet(primary_lane_state->standard_container_set);
  }
  body_stage0_object.resource_bindings = primary_lane_state->resource_bindings;
  body_stage0_object.descriptor_values = primary_lane_state->descriptor_values;

  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>> stage_container_sets{};
  std::string stage_set_error_message;
  if (!pipeline_scheduler_detail::BuildPipelineStageContainerSets(
        algorithm_objects,
        begin_index,
        end_index,
        &stage_container_sets,
        &stage_set_error_message)) {
    const std::string failure_message = stage_set_error_message.empty()
      ? std::string("Failed to build pipeline stage container sets.")
      : std::move(stage_set_error_message);
    ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
    set_error(failure_message);
    out_pipeline_signal->stop_requested = true;
    *out_pipeline_processing_failed = true;
    return false;
  }
  std::vector<size_t> executable_indices{};
  executable_indices.reserve(pipeline_stage_count);
  std::vector<bool> stage_allow_tick(pipeline_stage_count, false);
  std::vector<bool> stage_is_ready(pipeline_stage_count, false);
  std::vector<bool> stage_launch_once_completed(pipeline_stage_count, false);

  for (size_t index = begin_index; index < end_index; ++index) {
    const size_t stage_offset = index - begin_index;
    ::algomanager::bridge::AlgorithmObject& object = algorithm_objects[index];
    ::algomanager::bridge::AgentAlgorithmRuntimeState runtime_state{};
    if (index < updated_runtime_states.size()) {
      runtime_state = updated_runtime_states[index];
    } else {
      runtime_state.algorithm_name = object.algorithm_profile.algorithm_name;
    }
    pipeline_scheduler_detail::ResetRuntimeStateBase(runtime_state, &runtime_state);
    updated_runtime_states[index] = std::move(runtime_state);

    if (collect_pipeline_timing) {
      progress_state.stage_runtime_stats.push_back(::algomanager::bridge::AlgorithmPipelineStageRuntimeStat{
        .stage_name = object.algorithm_profile.algorithm_name,
        .elapsed_seconds = 0.0f,
        .reason = {},
      });
    }

    stage_allow_tick[stage_offset] = index < allow_tick_mask.size() ? allow_tick_mask[index] : true;
    stage_is_ready[stage_offset] =
      index < assembly_states.size() &&
      assembly_states[index] == ::algomanager::bridge::AlgorithmAssemblyState::Ready;
    const bool launch_once_then_hold =
      object.tick_lifetime == ::algomanager::bridge::AlgorithmTickLifetime::LaunchOnceThenHold;
    stage_launch_once_completed[stage_offset] =
      launch_once_then_hold && updated_runtime_states[index].launch_once_completed;
  }
  const bool forced_sync =
    pipeline_state.sync_mode == ::algomanager::bridge::AlgorithmPipelineSyncMode::Forced;
  std::vector<bool> next_body_stage_has_data(body_stage_count, false);

  auto tick_stage_without_execution = [&](
    size_t index,
    bool has_data,
    bool is_stage0_body,
    const std::string& blocked_reason) {
    const size_t stage_offset = index - begin_index;
    ::algomanager::bridge::AlgorithmObject& object = algorithm_objects[index];
    ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
    pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      !has_data
        ? pipeline_scheduler_detail::BuildPipelineStageNoInputReason(
            is_stage0_body,
            !pipeline_state.pending_stage0_submissions.empty())
        : blocked_reason);
    if (!pipeline_scheduler_detail::TickAlgorithmObject(
          object,
          context,
          stage_allow_tick[stage_offset],
          stage_is_ready[stage_offset],
          false,
          &runtime_state)) {
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
    }
    pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
      runtime_state.algorithm_to_agent_signal,
      out_pipeline_signal);
  };

  auto tick_wrapper_stage = [&](
    size_t index,
    bool collect_exit_reflection,
    const char* executed_reason) {
    const size_t stage_offset = index - begin_index;
    ::algomanager::bridge::AlgorithmObject& object = algorithm_objects[index];
    ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
    object.SetContainerSet(primary_lane_state->standard_container_set);
    object.resource_bindings = primary_lane_state->resource_bindings;
    object.descriptor_values = primary_lane_state->descriptor_values;
    const bool execute_now =
      stage_allow_tick[stage_offset] &&
      stage_is_ready[stage_offset] &&
      !stage_launch_once_completed[stage_offset];
    if (!execute_now) {
      tick_stage_without_execution(
        index,
        true,
        false,
        pipeline_scheduler_detail::BuildPipelineStageIdleReason(
          stage_allow_tick[stage_offset],
          stage_is_ready[stage_offset],
          stage_launch_once_completed[stage_offset]));
      return true;
    }
    executable_indices.push_back(index);
    pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      executed_reason);
    if (!pipeline_scheduler_detail::TickAlgorithmObject(
          object,
          context,
          stage_allow_tick[stage_offset],
          stage_is_ready[stage_offset],
          true,
          &runtime_state)) {
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error("Mounted pipeline wrapper stage execution failed.");
      *out_pipeline_processing_failed = true;
      return false;
    }
    pipeline_scheduler_detail::AddPipelineStageRuntimeElapsed(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      runtime_state.algorithm_exec_elapsed_seconds);
    if (collect_exit_reflection) {
      const algorithm::AlgorithmContainerSet* wrapper_container_set = object.container_set();
      ALGORITHM_SCHEDULER_ASSERT(
        wrapper_container_set != nullptr,
        "Pipeline wrapper end container set is unavailable.");
      if (!wrapper_container_set) {
        runtime_state.algorithm_to_agent_signal.stop_requested = true;
        pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
          runtime_state.algorithm_to_agent_signal,
          out_pipeline_signal);
        set_error("Pipeline wrapper end container set is unavailable.");
        *out_pipeline_processing_failed = true;
        return false;
      }
      if (pipeline_scheduler_detail::CollectPipelineExitReflectionSnapshot(
            object.pipeline_name.empty() ? object.algorithm_profile.algorithm_name : object.pipeline_name,
            *wrapper_container_set,
            &runtime_state.reflection_snapshot)) {
        runtime_state.reflection_snapshot_cached = false;
        pipeline_state.exit_reflection_snapshot = runtime_state.reflection_snapshot;
        pipeline_state.exit_reflection_snapshot_valid = true;
      } else {
        runtime_state.reflection_snapshot.Clear();
        pipeline_state.exit_reflection_snapshot.Clear();
        pipeline_state.exit_reflection_snapshot_valid = false;
      }
    }
    pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
      runtime_state.algorithm_to_agent_signal,
      out_pipeline_signal);
    return true;
  };

  auto execute_body_stage = [&](
    size_t body_stage_offset,
    bool allow_immediate_forward,
    std::vector<bool>* inout_working_body_stage_has_data) {
    const size_t index = body_begin_index + body_stage_offset;
    const size_t stage_offset = index - begin_index;
    ::algomanager::bridge::AlgorithmObject& object = algorithm_objects[index];
    ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
    pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      "Stage executed.");
    executable_indices.push_back(index);

    algomanager::bridge::PipelineStageBridge bridge(object.runtime_transfer_map);
    std::string ingress_error_message;
    const bool ingress_ok = pipeline_uses_inter_stage_buffer
      ? bridge.IngestFromPreviousStage(
          object.algorithm_profile.algorithm_name,
          stage_container_sets,
          primary_lane_state->inter_stage_buffer,
          object.mutable_container_set(),
          &ingress_error_message)
      : bridge.IngestFromPreviousStage(
          object.algorithm_profile.algorithm_name,
          stage_container_sets,
          object.mutable_container_set(),
          &ingress_error_message);
    if (!ingress_ok) {
      const std::string failure_message = ingress_error_message.empty()
        ? std::string("Failed to ingest pipeline stage input.")
        : std::move(ingress_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Ingress failed: " + failure_message);
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error(failure_message);
      *out_pipeline_processing_failed = true;
      return false;
    }
    if (!pipeline_scheduler_detail::TickAlgorithmObject(
          object,
          context,
          stage_allow_tick[stage_offset],
          stage_is_ready[stage_offset],
          true,
          &runtime_state)) {
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Execution failed.");
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error("Mounted pipeline stage execution failed.");
      *out_pipeline_processing_failed = true;
      return false;
    }
    pipeline_scheduler_detail::AddPipelineStageRuntimeElapsed(
      pipeline_stage_runtime_stats,
      object.algorithm_profile.algorithm_name,
      runtime_state.algorithm_exec_elapsed_seconds);
    pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
      runtime_state.algorithm_to_agent_signal,
      out_pipeline_signal);

    const algorithm::AlgorithmContainerSet* source_container_set = object.container_set();
    ALGORITHM_SCHEDULER_ASSERT(
      source_container_set != nullptr,
      "Pipeline stage source container set is unavailable.");
    if (!source_container_set) {
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Egress failed: source container set is unavailable.");
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error("Pipeline stage source container set is unavailable.");
      *out_pipeline_processing_failed = true;
      return false;
    }

    std::string egress_error_message;
    const bool is_body_last_stage = body_stage_offset + 1u == body_stage_count;
    if (is_body_last_stage) {
      if (pipeline_state.topology == ::algomanager::bridge::AlgorithmPipelineTopology::Circular) {
        const bool emit_loopback_ok = pipeline_uses_inter_stage_buffer
          ? bridge.EmitToNextStage(
              object.algorithm_profile.algorithm_name,
              *source_container_set,
              &primary_lane_state->inter_stage_buffer,
              &stage_container_sets,
              &egress_error_message)
          : bridge.EmitToNextStage(
              object.algorithm_profile.algorithm_name,
              *source_container_set,
              &stage_container_sets,
              &egress_error_message);
        if (!emit_loopback_ok) {
          const std::string failure_message = egress_error_message.empty()
            ? std::string("Failed to emit circular pipeline stage output.")
            : std::move(egress_error_message);
          ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
          runtime_state.algorithm_to_agent_signal.stop_requested = true;
          pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
            pipeline_stage_runtime_stats,
            object.algorithm_profile.algorithm_name,
            "Egress failed: " + failure_message);
          pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
            runtime_state.algorithm_to_agent_signal,
            out_pipeline_signal);
          set_error(failure_message);
          *out_pipeline_processing_failed = true;
          return false;
        }
        next_body_stage_has_data.front() = true;
      }
    } else {
      const bool emit_ok = pipeline_uses_inter_stage_buffer
        ? bridge.EmitToNextStage(
            object.algorithm_profile.algorithm_name,
            *source_container_set,
            &primary_lane_state->inter_stage_buffer,
            &stage_container_sets,
            &egress_error_message)
        : bridge.EmitToNextStage(
            object.algorithm_profile.algorithm_name,
            *source_container_set,
            &stage_container_sets,
            &egress_error_message);
      if (!emit_ok) {
        const std::string failure_message = egress_error_message.empty()
          ? std::string("Failed to emit pipeline stage output.")
          : std::move(egress_error_message);
        ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
        runtime_state.algorithm_to_agent_signal.stop_requested = true;
        pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
          pipeline_stage_runtime_stats,
          object.algorithm_profile.algorithm_name,
          "Egress failed: " + failure_message);
        pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
          runtime_state.algorithm_to_agent_signal,
          out_pipeline_signal);
        set_error(failure_message);
        *out_pipeline_processing_failed = true;
        return false;
      }
      if (allow_immediate_forward && inout_working_body_stage_has_data) {
        (*inout_working_body_stage_has_data)[body_stage_offset + 1u] = true;
      } else {
        next_body_stage_has_data[body_stage_offset + 1u] = true;
      }
    }

    std::string reset_error_message;
    if (!pipeline_scheduler_detail::ClearPipelineExternalWriteResetContainers(
          object,
          const_cast<algorithm::AlgorithmContainerSet*>(source_container_set),
          &reset_error_message)) {
      const std::string failure_message = reset_error_message.empty()
        ? std::string("Failed to clear pipeline external-write reset containers.")
        : std::move(reset_error_message);
      ALGORITHM_SCHEDULER_ASSERT(false, failure_message.c_str());
      runtime_state.algorithm_to_agent_signal.stop_requested = true;
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "External-write reset failed: " + failure_message);
      pipeline_scheduler_detail::MergeAlgorithmToAgentSignal(
        runtime_state.algorithm_to_agent_signal,
        out_pipeline_signal);
      set_error(failure_message);
      *out_pipeline_processing_failed = true;
      return false;
    }
    if (allow_immediate_forward && inout_working_body_stage_has_data) {
      (*inout_working_body_stage_has_data)[body_stage_offset] = false;
    }
    return true;
  };

  auto commit_progress_state = [&](pipeline_scheduler_detail::PipelineGroupProgressState&& applied_progress_state) {
    if (begin_index >= updated_runtime_states.size() || begin_index >= end_index) {
      return;
    }
    size_t active_stage_index = 0u;
    bool active_stage_valid = false;
    size_t active_bundle_begin_stage_index = 0u;
    size_t active_bundle_stage_count = 0u;
    ::algomanager::bridge::AlgorithmExecutionPreference active_bundle_preference =
    ::algomanager::bridge::AlgorithmExecutionPreference::Vk;
    bool active_bundle_valid = false;
    resolve_pipeline_active_stage(
      pipeline_state.stage_has_data,
      executable_indices,
      &active_stage_index,
      &active_stage_valid);
    resolve_pipeline_active_bundle(
      pipeline_state.stage_has_data,
      executable_indices,
      &active_bundle_begin_stage_index,
      &active_bundle_stage_count,
      &active_bundle_preference,
      &active_bundle_valid);
    for (size_t index = begin_index; index < end_index; ++index) {
      ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state = updated_runtime_states[index];
      runtime_state.pipeline_progress_signature = applied_progress_state.signature;
      runtime_state.pipeline_progress_signature_valid = applied_progress_state.signature_valid;
      runtime_state.pipeline_no_progress_seconds = applied_progress_state.no_progress_seconds;
      runtime_state.pipeline_stall_report_requested = applied_progress_state.stall_report_requested;
      runtime_state.pipeline_stall_reported = applied_progress_state.stall_reported;
      runtime_state.pipeline_stall_reason = applied_progress_state.stall_reason;
      runtime_state.pipeline_active_stage_index = static_cast<uint32_t>(active_stage_index);
      runtime_state.pipeline_active_stage_index_valid = active_stage_valid;
      runtime_state.pipeline_active_bundle_begin_stage_index = static_cast<uint32_t>(active_bundle_begin_stage_index);
      runtime_state.pipeline_active_bundle_stage_count = static_cast<uint32_t>(active_bundle_stage_count);
      runtime_state.pipeline_active_bundle_preference = active_bundle_preference;
      runtime_state.pipeline_active_bundle_valid = active_bundle_valid;
      if (collect_pipeline_timing) {
        runtime_state.pipeline_total_elapsed_seconds = applied_progress_state.total_elapsed_seconds;
        runtime_state.pipeline_stage_runtime_stats = applied_progress_state.stage_runtime_stats;
      } else {
        runtime_state.pipeline_total_elapsed_seconds = 0.0f;
        runtime_state.pipeline_stage_runtime_stats.clear();
      }
    }
  };
  const auto pipeline_time_begin = std::chrono::steady_clock::now();
  if (!tick_wrapper_stage(begin_index, false, "Wrapper begin executed.")) {
    return false;
  }
  auto build_body_execution_bundle = [&](
    size_t start_body_stage_offset,
    const std::vector<bool>& stage_has_data_state) {
    pipeline_scheduler_detail::PipelineStageExecutionBundle bundle{};
    bundle.begin_stage_offset = start_body_stage_offset;
    bundle.end_stage_offset = start_body_stage_offset + 1u;
    bundle.execution_preference =
      algorithm_objects[body_begin_index + start_body_stage_offset].execution_preference;
    for (; bundle.end_stage_offset < body_stage_count; ++bundle.end_stage_offset) {
      if (!stage_has_data_state[bundle.end_stage_offset]) {
        continue;
      }
      const size_t index = body_begin_index + bundle.end_stage_offset;
      const size_t stage_offset = index - begin_index;
      const bool execute_now =
        pipeline_scheduler_detail::AreExecutionPreferencesCompatible(
          algorithm_objects[index].execution_preference,
          bundle.execution_preference) &&
        stage_allow_tick[stage_offset] &&
        stage_is_ready[stage_offset] &&
        !stage_launch_once_completed[stage_offset] &&
        (pipeline_stage_debug_all || stage_offset == pipeline_stage_debug_index);
      if (!execute_now) {
        break;
      }
    }
    return bundle;
  };

  if (forced_sync) {
    std::vector<bool> working_body_stage_has_data = current_body_stage_has_data;
    for (size_t body_stage_offset = 0u; body_stage_offset < body_stage_count; ) {
      const size_t index = body_begin_index + body_stage_offset;
      const size_t stage_offset = index - begin_index;
      const bool has_data = working_body_stage_has_data[body_stage_offset];
      const bool execute_now =
        has_data &&
        stage_allow_tick[stage_offset] &&
        stage_is_ready[stage_offset] &&
        !stage_launch_once_completed[stage_offset] &&
        (pipeline_stage_debug_all || stage_offset == pipeline_stage_debug_index);
      if (!execute_now) {
        if (has_data) {
          next_body_stage_has_data[body_stage_offset] = true;
        }
        tick_stage_without_execution(
          index,
          has_data,
          body_stage_offset == 0u,
          has_data
            ? "Stage is holding data, but the downstream stage cannot accept output this tick."
            : pipeline_scheduler_detail::BuildPipelineStageIdleReason(
                stage_allow_tick[stage_offset],
                stage_is_ready[stage_offset],
                stage_launch_once_completed[stage_offset]));
        ++body_stage_offset;
        continue;
      }
      pipeline_scheduler_detail::PipelineStageExecutionBundle bundle = build_body_execution_bundle(
        body_stage_offset,
        working_body_stage_has_data);
      for (size_t bundle_stage_offset = bundle.begin_stage_offset; bundle_stage_offset < bundle.end_stage_offset; ++bundle_stage_offset) {
        if (!working_body_stage_has_data[bundle_stage_offset]) {
          continue;
        }
        if (!execute_body_stage(bundle_stage_offset, true, &working_body_stage_has_data)) {
          return false;
        }
      }
      body_stage_offset = bundle.end_stage_offset;
    }
  } else {
    next_body_stage_has_data = current_body_stage_has_data;
    for (size_t body_stage_offset = 0u; body_stage_offset < body_stage_count; ) {
      const size_t index = body_begin_index + body_stage_offset;
      const size_t stage_offset = index - begin_index;
      const bool has_data = current_body_stage_has_data[body_stage_offset];
      const bool execute_now =
        has_data &&
        stage_allow_tick[stage_offset] &&
        stage_is_ready[stage_offset] &&
        !stage_launch_once_completed[stage_offset] &&
        (pipeline_stage_debug_all || stage_offset == pipeline_stage_debug_index);
      if (!execute_now) {
        if (has_data) {
          next_body_stage_has_data[body_stage_offset] = true;
        }
        tick_stage_without_execution(
          index,
          has_data,
          body_stage_offset == 0u,
          has_data
            ? "Stage is holding data, but the downstream stage cannot accept output this tick."
            : pipeline_scheduler_detail::BuildPipelineStageIdleReason(
                stage_allow_tick[stage_offset],
                stage_is_ready[stage_offset],
                stage_launch_once_completed[stage_offset]));
        ++body_stage_offset;
        continue;
      }
      pipeline_scheduler_detail::PipelineStageExecutionBundle bundle = build_body_execution_bundle(
        body_stage_offset,
        current_body_stage_has_data);
      if (!execute_body_stage(bundle.begin_stage_offset, false, &next_body_stage_has_data)) {
        return false;
      }
      next_body_stage_has_data[bundle.begin_stage_offset] = false;
      break;
    }
  }

  if (!tick_wrapper_stage(effective_tail_index, true, "Wrapper end executed.")) {
    return false;
  }

  pipeline_scheduler_detail::CommitPipelineStageStateToPrimaryLane(
    &pipeline_state,
    build_full_stage_has_data(next_body_stage_has_data));
  progress_state.total_elapsed_seconds =
    std::chrono::duration<float>(std::chrono::steady_clock::now() - pipeline_time_begin).count();
  const uint64_t current_signature =
    pipeline_scheduler_detail::HashPipelineGroupState(
      algorithm_objects,
      executable_indices,
      pipeline_state.current_lane_id,
      pipeline_scheduler_detail::CountValidPipelineLanes(pipeline_state),
      pipeline_state.stage_has_data,
      pipeline_state.pending_stage0_submissions.size(),
      pipeline_state.stage0_saturated,
      updated_runtime_states,
       begin_index,
       end_index);
  const bool signature_unchanged =
    previous_progress_state.signature_valid &&
    previous_progress_state.signature == current_signature;
  pipeline_scheduler_detail::UpdatePipelineGroupProgressState(
    previous_progress_state,
    current_signature,
    true,
    context.dt_seconds,
    &progress_state);
  if (signature_unchanged) {
    progress_state.stall_reason =
      "Pipeline signature did not change after execute and bridge output completed.";
    for (size_t index : executable_indices) {
      ::algomanager::bridge::AlgorithmObject& object = algorithm_objects[index];
      pipeline_scheduler_detail::SetPipelineStageRuntimeReason(
        pipeline_stage_runtime_stats,
        object.algorithm_profile.algorithm_name,
        "Stage executed, but the observable pipeline state did not change.");
    }
  }
  commit_progress_state(std::move(progress_state));

  {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_runtime_states_[root_object.pipeline_name][owner_agent_name] = std::move(pipeline_state);
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool AlgorithmScheduler::MountPipelineAlgorithmObjects(
  std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
  std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::vector<::algomanager::bridge::AlgorithmAssemblyState>* inout_assembly_states,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
  const std::string& owner_agent_name,
  const std::string& pipeline_name,
  const std::vector<AlgorithmPipelineStageSubmission>& stage_submissions,
  AlgorithmExecutionPreference execution_preference,
  AlgorithmPipelineTopology topology,
  AlgorithmPipelineSyncMode sync_mode,
  size_t* out_begin_index,
  std::string* out_error_message,
  bool load_reflector) {
  auto set_error = [&](std::string message) {
    if (out_error_message) {
      *out_error_message = std::move(message);
    }
  };

  if (!mounted_objects || !inout_runtime_states || !inout_assembly_states) {
    set_error("Pipeline mount received a null output pointer.");
    return false;
  }
  if (owner_agent_name.empty()) {
    set_error("Owner agent name must not be empty.");
    return false;
  }
  if (pipeline_name.empty()) {
    set_error("Pipeline name must not be empty.");
    return false;
  }
  if (stage_submissions.empty()) {
    set_error("Pipeline stage submission list must not be empty.");
    return false;
  }
  if (pipeline_scheduler_detail::PipelineNameInUse(*mounted_objects, pipeline_name)) {
    set_error("Pipeline name is already in use.");
    return false;
  }
  ::algorithm::AlgorithmPackageLocation root_package_location{};
  std::string root_location_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        stage_submissions.front().stage_name,
        &root_package_location,
        &root_location_error_message)) {
    set_error(root_location_error_message.empty()
      ? ("Failed to resolve pipeline root algorithm package location for '" + stage_submissions.front().stage_name + "'.")
      : std::move(root_location_error_message));
    return false;
  }
  algomanager::bridge::AlgorithmPipelineWrapperSpec wrapper_spec{};
  std::string wrapper_error_message;
  if (!algomanager::bridge::LoadAlgorithmPipelineWrapperSpecFromLocation(
        root_package_location,
        &wrapper_spec,
        &wrapper_error_message)) {
    set_error(wrapper_error_message.empty()
      ? "Failed to load pipeline wrapper spec."
      : std::move(wrapper_error_message));
    return false;
  }
  if (!wrapper_spec.declared) {
    set_error("Pipeline algorithm must declare both wrapper stages. Legacy pipelines must be upgraded with wrappers.");
    return false;
  }

  std::vector<pipeline_scheduler_detail::BuiltAlgorithmMount> built_body_stages{};
  built_body_stages.reserve(stage_submissions.size());
  std::unordered_set<std::string> seen_stage_names{};
  bool pipeline_supports_circular_submission = false;
  for (size_t stage_index = 0u; stage_index < stage_submissions.size(); ++stage_index) {
    const AlgorithmPipelineStageSubmission& stage_submission = stage_submissions[stage_index];
    if (stage_submission.stage_name.empty()) {
      set_error("Pipeline stage name must not be empty.");
      return false;
    }
    if (!seen_stage_names.insert(stage_submission.stage_name).second) {
      set_error("Pipeline stage name is duplicated inside the submission: " + stage_submission.stage_name);
      return false;
    }

    pipeline_scheduler_detail::BuiltAlgorithmMount built_mount =
      pipeline_scheduler_detail::BuildAlgorithmMount(
        stage_submission.stage_name,
        stage_submission.resource_bindings,
        stage_submission.descriptor_values,
        ::algomanager::bridge::AlgorithmMountMode::Pipeline,
        execution_preference,
        standard_shared_container_sets);
    if (!built_mount.ok) {
      set_error(built_mount.error_message);
      return false;
    }
    if (!built_mount.container_set || !built_mount.container_set->standard_layout.enabled()) {
      set_error("Pipeline stage standard container layout is unavailable: " + stage_submission.stage_name);
      return false;
    }
    if (!algorithm::HasMandatoryPipelineStageBuffer(*built_mount.container_set)) {
      set_error(
        "Pipeline stage must expose the mandatory implicit stage buffer in its standard container: " +
        stage_submission.stage_name);
      return false;
    }

    if (stage_index == 0u &&
        built_mount.object.runtime_transfer_map &&
        built_mount.object.runtime_transfer_map->SupportsCircularTick()) {
      pipeline_supports_circular_submission = true;
    }
    built_body_stages.push_back(std::move(built_mount));
  }

  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap> shared_pipeline_transfer_map{};
  std::string transfer_map_error_message;
  if (!pipeline_scheduler_detail::TryBuildMountedPipelineTransferMap(
        built_body_stages,
        &shared_pipeline_transfer_map,
        &transfer_map_error_message)) {
    set_error(transfer_map_error_message.empty()
      ? "Failed to build the mounted pipeline runtime transfer map."
      : std::move(transfer_map_error_message));
    return false;
  }
  ALGORITHM_SCHEDULER_ASSERT(
    shared_pipeline_transfer_map && shared_pipeline_transfer_map->valid,
    "Mounted pipeline runtime transfer map must be valid.");
  for (pipeline_scheduler_detail::BuiltAlgorithmMount& built_stage : built_body_stages) {
    built_stage.object.runtime_transfer_map = shared_pipeline_transfer_map;
  }

  if (topology == ::algomanager::bridge::AlgorithmPipelineTopology::Circular &&
      !pipeline_supports_circular_submission) {
    set_error("This pipeline algorithm does not support circular pipeline submission.");
    return false;
  }

  std::vector<pipeline_scheduler_detail::BuiltAlgorithmMount> built_stages{};
  const bool has_wrapper_nodes = wrapper_spec.declared;
  built_stages.reserve(stage_submissions.size() + (has_wrapper_nodes ? 2u : 0u));
  if (has_wrapper_nodes) {
    const ::algomanager::bridge::AlgorithmExecutionPreference wrapper_begin_preference =
      built_body_stages.front().object.execution_preference;
    pipeline_scheduler_detail::BuiltAlgorithmMount wrapper_begin_mount =
      pipeline_scheduler_detail::BuildPipelineWrapperMount(
        root_package_location,
        built_body_stages.front().object,
        wrapper_spec.stage_begin,
        ::algomanager::bridge::AlgorithmPipelineWrapperRole::Begin,
        wrapper_begin_preference,
        standard_shared_container_sets);
    if (!wrapper_begin_mount.ok) {
      set_error(wrapper_begin_mount.error_message.empty()
        ? "Failed to build pipeline wrapper begin mount."
        : std::move(wrapper_begin_mount.error_message));
      return false;
    }
    if (wrapper_begin_mount.object.pipeline_wrapper_empty) {
      set_error("Pipeline wrapper begin stage must resolve to a concrete algorithm package.");
      return false;
    }
    built_stages.push_back(std::move(wrapper_begin_mount));
  }
  for (pipeline_scheduler_detail::BuiltAlgorithmMount& built_body_stage : built_body_stages) {
    built_stages.push_back(std::move(built_body_stage));
  }
  if (has_wrapper_nodes) {
    const ::algomanager::bridge::AlgorithmExecutionPreference wrapper_end_preference =
      built_body_stages.back().object.execution_preference;
    pipeline_scheduler_detail::BuiltAlgorithmMount wrapper_end_mount =
      pipeline_scheduler_detail::BuildPipelineWrapperMount(
        root_package_location,
        built_stages[has_wrapper_nodes ? 1u : 0u].object,
        wrapper_spec.stage_end,
        ::algomanager::bridge::AlgorithmPipelineWrapperRole::End,
        wrapper_end_preference,
        standard_shared_container_sets);
    if (!wrapper_end_mount.ok) {
      set_error(wrapper_end_mount.error_message.empty()
        ? "Failed to build pipeline wrapper end mount."
        : std::move(wrapper_end_mount.error_message));
      return false;
    }
    if (wrapper_end_mount.object.pipeline_wrapper_empty) {
      set_error("Pipeline wrapper end stage must resolve to a concrete algorithm package.");
      return false;
    }
    built_stages.push_back(std::move(wrapper_end_mount));
  }
  const size_t standard_stage_index = has_wrapper_nodes ? 1u : 0u;
  std::shared_ptr<algorithm::AlgorithmContainerSet> pipeline_standard_container_set =
    built_stages[standard_stage_index].object.shared_container_set;
  std::string standard_container_mapping_error;
  if (!pipeline_scheduler_detail::NormalizePipelineStandardContainerSet(
        pipeline_standard_container_set.get(),
        &standard_container_mapping_error)) {
    set_error(standard_container_mapping_error.empty()
      ? "Failed to normalize the pipeline standard container set."
      : std::move(standard_container_mapping_error));
    return false;
  }
  for (const pipeline_scheduler_detail::BuiltAlgorithmMount& built_stage : built_stages) {
    if (!pipeline_scheduler_detail::BindPipelineStageContainerSetToStandardContainerSet(
          *built_stage.object.container_set(),
          pipeline_standard_container_set.get(),
          &standard_container_mapping_error)) {
      set_error(standard_container_mapping_error.empty()
        ? "Failed to bind a pipeline stage to the standard container set."
        : std::move(standard_container_mapping_error));
      return false;
    }
  }
  for (pipeline_scheduler_detail::BuiltAlgorithmMount& built_stage : built_stages) {
    built_stage.object.SetContainerSet(pipeline_standard_container_set);
    built_stage.container_set = pipeline_standard_container_set;
  }

  const size_t pipeline_total_stage_count = built_stages.size();
  const size_t pipeline_body_begin_stage_index = has_wrapper_nodes ? 1u : 0u;
  const size_t pipeline_body_stage_count = built_body_stages.size();
  const size_t pipeline_effective_tail_stage_index =
    has_wrapper_nodes ? (pipeline_total_stage_count - 1u) : (pipeline_total_stage_count - 1u);

  const size_t pipeline_begin_index = mounted_objects->size();
  const size_t previous_runtime_state_count = inout_runtime_states->size();
  const size_t previous_assembly_state_count = inout_assembly_states->size();
  auto pipeline_child_storage =
    std::make_shared<::algomanager::algoscheduler::AlgorithmObjectChildStorage>();
  pipeline_child_storage->children.reserve(built_stages.size());
  for (size_t stage_index = 0u; stage_index < built_stages.size(); ++stage_index) {
    pipeline_child_storage->children.push_back(std::move(built_stages[stage_index].object));
  }

  ::algomanager::bridge::AlgorithmObject pipeline_node{};
  pipeline_node.algorithm_profile.algorithm_name = pipeline_name;
  pipeline_node.runtime_package_root_path = root_package_location.runtime_package_root.string();
  pipeline_node.shared_container_set = pipeline_standard_container_set;
  pipeline_node.mount_mode = ::algomanager::bridge::AlgorithmMountMode::Pipeline;
  pipeline_node.pipeline_stage = true;
  pipeline_node.pipeline_name = pipeline_name;
  pipeline_node.pipeline_stage_index = 0u;
  pipeline_node.pipeline_stage_count = static_cast<uint32_t>(pipeline_total_stage_count);
  pipeline_node.pipeline_topology = topology;
  pipeline_node.pipeline_sync_mode = sync_mode;
  pipeline_node.child_algorithm_object_storage = std::move(pipeline_child_storage);
  pipeline_node.child_algorithm_objects.reserve(
    pipeline_node.child_algorithm_object_storage->children.size());
  for (::algomanager::bridge::AlgorithmObject& child :
       pipeline_node.child_algorithm_object_storage->children) {
    pipeline_node.child_algorithm_objects.emplace_back(
      pipeline_node.child_algorithm_object_storage,
      &child);
  }

  for (size_t stage_index = 0u; stage_index < pipeline_node.child_algorithm_object_storage->children.size(); ++stage_index) {
    ::algomanager::bridge::AlgorithmObject& child =
      pipeline_node.child_algorithm_object_storage->children[stage_index];
    child.mount_mode = ::algomanager::bridge::AlgorithmMountMode::Pipeline;
    child.pipeline_stage = true;
    child.pipeline_name = pipeline_name;
    child.pipeline_stage_index = static_cast<uint32_t>(stage_index);
    child.pipeline_stage_count = static_cast<uint32_t>(pipeline_total_stage_count);
    child.pipeline_topology = topology;
    child.pipeline_sync_mode = sync_mode;
  }
  const ::algomanager::bridge::AlgorithmObject& pipeline_result_node =
    *pipeline_node.child_algorithm_objects[pipeline_effective_tail_stage_index];
  pipeline_node.intervention = pipeline_result_node.intervention;
  pipeline_node.runtime_package_root_path = pipeline_result_node.runtime_package_root_path;
  pipeline_node.resource_bindings = pipeline_result_node.resource_bindings;
  pipeline_node.descriptor_values = pipeline_result_node.descriptor_values;
  mounted_objects->push_back(std::move(pipeline_node));
  inout_runtime_states->push_back(::algomanager::bridge::AgentAlgorithmRuntimeState{
    .algorithm_name = pipeline_name,
  });
  inout_runtime_states->back().child_runtime_states.resize(pipeline_total_stage_count);
  for (size_t stage_index = 0u; stage_index < pipeline_total_stage_count; ++stage_index) {
    inout_runtime_states->back().child_runtime_states[stage_index].algorithm_name =
      mounted_objects->back().child_algorithm_objects[stage_index]->algorithm_profile.algorithm_name;
    AlgorithmReflectionSnapshot stage_reflection_snapshot{};
    if (pipeline_scheduler_detail::CollectReflectionSnapshot(
          *mounted_objects->back().child_algorithm_objects[stage_index],
          *mounted_objects->back().child_algorithm_objects[stage_index]->container_set(),
          &stage_reflection_snapshot)) {
      inout_runtime_states->back().child_runtime_states[stage_index].reflection_snapshot =
        std::move(stage_reflection_snapshot);
      inout_runtime_states->back().child_runtime_states[stage_index].reflection_snapshot_cached = true;
    }
  }
  inout_assembly_states->push_back(::algomanager::bridge::AlgorithmAssemblyState::Ready);

  const auto rollback_mount_vectors = [&]() {
    mounted_objects->resize(pipeline_begin_index);
    inout_runtime_states->resize(previous_runtime_state_count);
    inout_assembly_states->resize(previous_assembly_state_count);
  };


  ::algomanager::bridge::JobsPipelineRuntimeState pipeline_runtime_state{};
  pipeline_runtime_state.owner_agent_name = owner_agent_name;
  pipeline_runtime_state.topology = topology;
  pipeline_runtime_state.sync_mode = sync_mode;
  pipeline_runtime_state.max_concurrent_stage0_submissions =
    common_data::DefaultPipelineMaxConcurrentStage0Submissions();
  pipeline_runtime_state.mandatory_stage_buffer_slot_name =
    shared_pipeline_transfer_map->pipeline_shared_stage_buffer_slot_name;
  pipeline_runtime_state.stage_has_data.assign(pipeline_total_stage_count, false);
  if (topology == ::algomanager::bridge::AlgorithmPipelineTopology::NonCircular &&
      pipeline_body_begin_stage_index < pipeline_runtime_state.stage_has_data.size()) {
    pipeline_runtime_state.stage_has_data[pipeline_body_begin_stage_index] = true;
  }

  pipeline_scheduler_detail::PipelineLaneRuntimeState initial_lane_state{};
  std::string lane_error_message;
  if (!pipeline_scheduler_detail::TryBuildInitialPipelineLaneRuntimeState(
        *mounted_objects->at(pipeline_begin_index).child_algorithm_objects[pipeline_body_begin_stage_index],
        pipeline_total_stage_count,
        owner_agent_name,
        topology == ::algomanager::bridge::AlgorithmPipelineTopology::Circular,
        pipeline_runtime_state.next_lane_id,
        pipeline_runtime_state.mandatory_stage_buffer_slot_name,
        &initial_lane_state,
        &lane_error_message)) {
    rollback_mount_vectors();
    set_error(lane_error_message.empty()
      ? "Failed to initialize pipeline lane runtime state."
      : std::move(lane_error_message));
    return false;
  }
  if (topology == ::algomanager::bridge::AlgorithmPipelineTopology::NonCircular &&
      pipeline_body_begin_stage_index < initial_lane_state.stage_has_data.size()) {
    initial_lane_state.stage_has_data[pipeline_body_begin_stage_index] = true;
  }
  pipeline_runtime_state.current_lane_id = initial_lane_state.lane_id;
  pipeline_runtime_state.lanes.push_back(std::move(initial_lane_state));
  ++pipeline_runtime_state.next_lane_id;

  const bool registered = RegisterPipeline(
    ::algomanager::bridge::JobsPipelineRegistration{
      .pipeline_name = pipeline_name,
      .root_stage_name = mounted_objects->at(pipeline_begin_index)
        .child_algorithm_objects[pipeline_body_begin_stage_index]->algorithm_profile.algorithm_name,
      .stage_count = static_cast<uint32_t>(pipeline_total_stage_count),
      .body_begin_stage_index = static_cast<uint32_t>(pipeline_body_begin_stage_index),
      .body_stage_count = static_cast<uint32_t>(pipeline_body_stage_count),
      .effective_tail_stage_index = static_cast<uint32_t>(pipeline_effective_tail_stage_index),
      .topology = topology,
      .sync_mode = sync_mode,
      .max_concurrent_stage0_submissions =
        common_data::DefaultPipelineMaxConcurrentStage0Submissions(),
      .mandatory_stage_buffer_slot_name =
        shared_pipeline_transfer_map->pipeline_shared_stage_buffer_slot_name,
    },
    out_error_message);
  if (!registered) {
    rollback_mount_vectors();
    return false;
  }
  const bool runtime_registered = RegisterPipelineRuntime(
    pipeline_name,
    owner_agent_name,
    pipeline_runtime_state,
    out_error_message);
  if (!runtime_registered) {
    rollback_mount_vectors();
    return false;
  }
  if (out_begin_index) {
    *out_begin_index = pipeline_begin_index;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}
bool AlgorithmScheduler::ReplayMountedPipelineDebug(
  std::vector<::algomanager::bridge::AlgorithmObject>* mounted_objects,
  size_t index,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  std::vector<::algomanager::bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::string* out_error_message) {
  if (!mounted_objects || !inout_runtime_states) {
    if (out_error_message) {
      *out_error_message = "Pipeline bridge replay received a null output pointer.";
    }
    return false;
  }
  if (index >= mounted_objects->size() || index >= inout_runtime_states->size()) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm runtime state is unavailable.";
    }
    return false;
  }

  ::algomanager::bridge::AlgorithmObject& object = (*mounted_objects)[index];
  ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state = (*inout_runtime_states)[index];
  if (!object.pipeline_stage) {
    if (out_error_message) {
      *out_error_message = "Selected algorithm is not a pipeline stage.";
    }
    return false;
  }
  if (!runtime_state.bridge_debug_set.valid || !runtime_state.bridge_debug_set.has_stage_input_container_set) {
    if (out_error_message) {
      *out_error_message = "Pipeline bridge debug input is unavailable for the selected stage.";
    }
    return false;
  }

  runtime_state.bridge_debug_set.replay_output_container_set = {};
  runtime_state.bridge_debug_set.replay_debug_state = {};
  runtime_state.bridge_debug_set.replay_reflection_snapshot.Clear();
  runtime_state.bridge_debug_set.replay_algorithm_to_agent_signal = {};
  runtime_state.bridge_debug_set.has_replay_output_container_set = false;
  runtime_state.bridge_debug_set.replay_valid = false;

  algorithm::AlgorithmContainerSet replay_container_set{};
  algorithm::CopyAlgorithmContainerSet(
    runtime_state.bridge_debug_set.stage_input_container_set,
    &replay_container_set);

  std::string submit_error_message;
  if (!this->SubmitAlgorithmObject(
        object,
        context,
        runtime_state.agent_to_algorithm_signal,
        &replay_container_set,
        &runtime_state.bridge_debug_set.replay_algorithm_to_agent_signal,
        &runtime_state.bridge_debug_set.replay_debug_state,
        &submit_error_message)) {
    if (out_error_message) {
      *out_error_message = submit_error_message.empty()
        ? "Pipeline bridge debug replay execution failed."
        : std::move(submit_error_message);
    }
    ALGORITHM_SCHEDULER_ASSERT(
      false,
      out_error_message ? out_error_message->c_str() : "Pipeline bridge debug replay execution failed.");
    return false;
  }

  ::algomanager::bridge::AlgorithmPackageDebugState collected_debug_state{};
  pipeline_scheduler_detail::CollectDebugState(object, &collected_debug_state);
  runtime_state.bridge_debug_set.replay_debug_state.signals.insert(
    runtime_state.bridge_debug_set.replay_debug_state.signals.end(),
    collected_debug_state.signals.begin(),
    collected_debug_state.signals.end());

  if (object.algorithm_reflector && !object.algorithm_reflector->empty()) {
    if (!pipeline_scheduler_detail::CollectReflectionSnapshot(
          object,
          replay_container_set,
          &runtime_state.bridge_debug_set.replay_reflection_snapshot)) {
      if (out_error_message) {
        *out_error_message = "Pipeline bridge debug replay reflection collection failed.";
      }
      ALGORITHM_SCHEDULER_ASSERT(false, "Pipeline bridge debug replay reflection collection failed.");
      return false;
    }
  }

  algorithm::CopyAlgorithmContainerSet(
    replay_container_set,
    &runtime_state.bridge_debug_set.replay_output_container_set);
  runtime_state.bridge_debug_set.has_replay_output_container_set = true;
  runtime_state.bridge_debug_set.replay_valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

void ClearSchedulerState() {
  AlgorithmScheduler::Instance().Clear();
}

void BeginSchedulerDebugToolRecording() {
  AlgorithmScheduler::Instance().BeginDebugToolRecording();
}

void EndSchedulerDebugToolRecording() {
  AlgorithmScheduler::Instance().EndDebugToolRecording();
}

bool SchedulerDebugToolRecording() {
  return AlgorithmScheduler::Instance().DebugToolRecording();
}

uint64_t SchedulerDebugToolRecordingTickCount() {
  return AlgorithmScheduler::Instance().DebugToolRecordingTickCount();
}

void RecordSchedulerDebugToolTick() {
  AlgorithmScheduler::Instance().RecordDebugToolTick();
}

bool EnqueueMountedPipelineStage0SubmissionForManager(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  const std::string& pipeline_name,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::vector<bridge::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::string* out_error_message,
  bool load_reflector) {
  return AlgorithmScheduler::Instance().EnqueueMountedPipelineStage0Submission(
    algorithm_objects,
    pipeline_name,
    agent_name,
    resource_bindings,
    descriptor_values,
    algorithm_assembly_states,
    out_error_message,
    load_reflector);
}

bool EnqueueMountedPipelineStage0SubmissionNodeForManager(
  bridge::AlgorithmObject* pipeline_node,
  std::vector<bridge::AlgorithmAssemblyState>* inout_assembly_states,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message,
  bool load_reflector) {
  return AlgorithmScheduler::Instance().EnqueueMountedPipelineStage0SubmissionNode(
    pipeline_node,
    inout_assembly_states,
    agent_name,
    resource_bindings,
    descriptor_values,
    out_error_message,
    load_reflector);
}

bool MountPipelineAlgorithmObjectsForManager(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  std::vector<bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::vector<bridge::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
  const std::string& agent_name,
  const std::string& pipeline_name,
  const std::vector<bridge::AlgorithmPipelineStageSubmission>& stage_submissions,
  bridge::AlgorithmExecutionPreference execution_preference,
  bridge::AlgorithmPipelineTopology topology,
  bridge::AlgorithmPipelineSyncMode sync_mode,
  size_t* out_index,
  std::string* out_error_message,
  bool load_reflector) {
  return AlgorithmScheduler::Instance().MountPipelineAlgorithmObjects(
    algorithm_objects,
    inout_runtime_states,
    algorithm_assembly_states,
    standard_shared_container_sets,
    agent_name,
    pipeline_name,
    stage_submissions,
    execution_preference,
    topology,
    sync_mode,
    out_index,
    out_error_message,
    load_reflector);
}

bool ReplayMountedPipelineDebugForManager(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t index,
  const bridge::AgentTickContext& context,
  std::vector<bridge::AgentAlgorithmRuntimeState>* algorithm_runtime_states,
  std::string* out_error_message) {
  return AlgorithmScheduler::Instance().ReplayMountedPipelineDebug(
    algorithm_objects,
    index,
    context,
    algorithm_runtime_states,
    out_error_message);
}

bool SubmitAlgorithmObjectForManager(
  const bridge::AlgorithmObject& object,
  const bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  return AlgorithmScheduler::Instance().SubmitAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

bool TickMountedPipelineForManager(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& agent_name,
  const bridge::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<bridge::AlgorithmAssemblyState>& assembly_states,
  bool collect_timing_log,
  std::vector<bridge::AgentAlgorithmRuntimeState>* out_updated_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message) {
  return AlgorithmScheduler::Instance().TickMountedPipeline(
    algorithm_objects,
    begin_index,
    end_index,
    agent_name,
    context,
    allow_tick_mask,
    assembly_states,
    collect_timing_log,
    out_updated_runtime_states,
    out_pipeline_signal,
    out_mounted_pipeline_processing_failed,
    out_error_message);
}

bool ReplayMountedPipelineDebugNodeForManager(
  bridge::AlgorithmObject* pipeline_node,
  bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  size_t child_index,
  const bridge::AgentTickContext& context,
  std::string* out_error_message) {
  return AlgorithmScheduler::Instance().ReplayMountedPipelineDebugNode(
    pipeline_node,
    inout_runtime_state,
    child_index,
    context,
    out_error_message);
}

bool TickMountedPipelineNodeForManager(
  bridge::AlgorithmObject* pipeline_node,
  bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  const std::string& agent_name,
  const bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message) {
  return AlgorithmScheduler::Instance().TickMountedPipelineNode(
    pipeline_node,
    inout_runtime_state,
    agent_name,
    context,
    allow_tick,
    assembly_state,
    collect_timing_log,
    out_pipeline_signal,
    out_mounted_pipeline_processing_failed,
    out_error_message);
}

bool TryGetMountedPipelineRuntimeForManager(
  const std::string& pipeline_name,
  const std::string& agent_name,
  bridge::JobsPipelineRuntimeState* out_runtime_state) {
  return AlgorithmScheduler::Instance().TryGetPipelineRuntime(
    pipeline_name,
    agent_name,
    out_runtime_state);
}

bool TryGetMountedPipelineRegistrationForManager(
  const std::string& pipeline_name,
  bridge::JobsPipelineRegistration* out_registration) {
  return AlgorithmScheduler::Instance().TryGetPipelineRegistration(
    pipeline_name,
    out_registration);
}

bool TickAlgorithmObjectForManager(
  bridge::AlgorithmObject& object,
  bridge::AgentAlgorithmRuntimeState& runtime_state,
  const std::string& agent_name,
  const bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  std::string* out_error_message) {
  return TickAlgorithmObject(
    object,
    runtime_state,
    agent_name,
    context,
    allow_tick,
    assembly_state,
    collect_timing_log,
    out_error_message);
}

void RefreshAlgorithmObjectSignalsForManager(
  bridge::AlgorithmObject& object,
  bridge::AgentAlgorithmRuntimeState& runtime_state,
  const bridge::AgentTickContext& context) {
  RefreshAlgorithmObjectSignals(object, runtime_state, context);
}

bool LoadAlgorithmPackageDefaultBindingsForManager(
  const std::string& algorithm_name,
  std::vector<bridge::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<bridge::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) {
  return LoadAlgorithmPackageDefaultBindings(
    algorithm_name,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

bool QueryAlgorithmRequestedBindingsForManager(
  const std::string& algorithm_name,
  bridge::AlgorithmRequestedResources* out_requested_resources,
  bridge::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message) {
  return QueryAlgorithmRequestedBindings(
    algorithm_name,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

void UnregisterMountedPipelineObjectsForManager(
  const std::vector<bridge::AlgorithmObject>& objects,
  const std::string& agent_name) {
  UnregisterMountedPipelineObjects(objects, agent_name);
}

void UnregisterMountedPipelineObjectForManager(
  const bridge::AlgorithmObject& object,
  const std::string& agent_name) {
  UnregisterMountedPipelineObject(object, agent_name);
}
}  // namespace scheduler
}  // namespace algomanager

namespace algomanager {

void ClearAlgorithmExecutionCaches() {
  algoscheduler::ClearAlgorithmExecutionCaches();
}

void ClearAlgorithmScheduler() {
  algoscheduler::ClearSchedulerState();
}

void BeginDebugToolRecording() {
  algoscheduler::BeginSchedulerDebugToolRecording();
}

void EndDebugToolRecording() {
  algoscheduler::EndSchedulerDebugToolRecording();
}

bool DebugToolRecording() {
  return algoscheduler::SchedulerDebugToolRecording();
}

uint64_t DebugToolRecordingTickCount() {
  return algoscheduler::SchedulerDebugToolRecordingTickCount();
}

void RecordDebugToolTick() {
  algoscheduler::RecordSchedulerDebugToolTick();
}

bool EnqueueMountedPipelineStage0Submission(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  const std::string& pipeline_name,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::vector<bridge::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::string* out_error_message,
  bool load_reflector) {
  return algoscheduler::EnqueueMountedPipelineStage0SubmissionForManager(
    algorithm_objects,
    pipeline_name,
    agent_name,
    resource_bindings,
    descriptor_values,
    algorithm_assembly_states,
    out_error_message,
    load_reflector);
}

bool EnqueueMountedPipelineStage0SubmissionNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  std::vector<bridge::AlgorithmAssemblyState>* inout_assembly_states,
  const std::string& agent_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  std::string* out_error_message,
  bool load_reflector) {
  return algoscheduler::EnqueueMountedPipelineStage0SubmissionNodeForManager(
    pipeline_node,
    inout_assembly_states,
    agent_name,
    resource_bindings,
    descriptor_values,
    out_error_message,
    load_reflector);
}

void UnregisterMountedPipelineObjects(
  const std::vector<::algomanager::bridge::AlgorithmObject>& objects,
  const std::string& agent_name) {
  algoscheduler::UnregisterMountedPipelineObjectsForManager(objects, agent_name);
}

void UnregisterMountedPipelineObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const std::string& agent_name) {
  algoscheduler::UnregisterMountedPipelineObjectForManager(object, agent_name);
}

void RefreshAlgorithmObjectSignals(
  ::algomanager::bridge::AlgorithmObject& object,
  ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state,
  const ::algomanager::bridge::AgentTickContext& context) {
  algoscheduler::RefreshAlgorithmObjectSignalsForManager(object, runtime_state, context);
}

bool TickAlgorithmObject(
  ::algomanager::bridge::AlgorithmObject& object,
  ::algomanager::bridge::AgentAlgorithmRuntimeState& runtime_state,
  const std::string& agent_name,
  const ::algomanager::bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  std::string* out_error_message) {
  return algoscheduler::TickAlgorithmObjectForManager(
    object,
    runtime_state,
    agent_name,
    context,
    allow_tick,
    assembly_state,
    collect_timing_log,
    out_error_message);
}

bool ExecuteJobsAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  return algoscheduler::ExecuteJobsAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

bool ExecuteVkAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::bridge::AgentTickContext& context,
  std::string* out_error_message) {
  return algoscheduler::ExecuteVkAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

bool ExecuteCudaAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  return algoscheduler::ExecuteCudaAlgorithmObject(
    object,
    container_set,
    context,
    agent_to_algorithm_signal,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

bool FinalizeAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return algoscheduler::FinalizeAlgorithmObject(object, container_set, out_error_message);
}

bool HasExecutableVkAlgorithmStage(const ::algomanager::bridge::AlgorithmObject& object) {
  return algoscheduler::HasExecutableVkAlgorithmStage(object);
}

bool HasExecutableCudaAlgorithmStage(const ::algomanager::bridge::AlgorithmObject& object) {
  return algoscheduler::HasExecutableCudaAlgorithmStage(object);
}

bool LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<bridge::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<bridge::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) {
  return algoscheduler::LoadAlgorithmPackageDefaultBindingsForManager(
    algorithm_name,
    out_resource_bindings,
    out_descriptor_values,
    out_has_default_file,
    out_error_message);
}

bool MountPipelineAlgorithmObjects(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  std::vector<bridge::AgentAlgorithmRuntimeState>* inout_runtime_states,
  std::vector<bridge::AlgorithmAssemblyState>* algorithm_assembly_states,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* standard_shared_container_sets,
  const std::string& agent_name,
  const std::string& pipeline_name,
  const std::vector<bridge::AlgorithmPipelineStageSubmission>& stage_submissions,
  bridge::AlgorithmExecutionPreference execution_preference,
  bridge::AlgorithmPipelineTopology topology,
  bridge::AlgorithmPipelineSyncMode sync_mode,
  size_t* out_index,
  std::string* out_error_message,
  bool load_reflector) {
  return algoscheduler::MountPipelineAlgorithmObjectsForManager(
    algorithm_objects,
    inout_runtime_states,
    algorithm_assembly_states,
    standard_shared_container_sets,
    agent_name,
    pipeline_name,
    stage_submissions,
    execution_preference,
    topology,
    sync_mode,
    out_index,
    out_error_message,
    load_reflector);
}

bool PrepareAlgorithmObjectByName(
  const std::string& algorithm_name,
  const std::vector<bridge::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<bridge::AlgorithmDescriptorValue>& descriptor_values,
  ::algomanager::bridge::AlgorithmObject* out_group,
  std::string* out_error_message,
  bool load_reflector) {
  return algoscheduler::PrepareAlgorithmObjectByName(
    algorithm_name,
    resource_bindings,
    descriptor_values,
    out_group,
    out_error_message,
    load_reflector);
}

bool QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  bridge::AlgorithmRequestedResources* out_requested_resources,
  bridge::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message) {
  return algoscheduler::QueryAlgorithmRequestedBindingsForManager(
    algorithm_name,
    out_requested_resources,
    out_requested_descriptor_bindings,
    out_error_message);
}

bool ReplayMountedPipelineDebug(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t index,
  const ::algomanager::bridge::AgentTickContext& context,
  std::vector<bridge::AgentAlgorithmRuntimeState>* algorithm_runtime_states,
  std::string* out_error_message) {
  return algoscheduler::ReplayMountedPipelineDebugForManager(
    algorithm_objects,
    index,
    context,
    algorithm_runtime_states,
    out_error_message);
}

void SetAlgorithmRuntimeShutdownHook() {
  algoscheduler::SetAlgorithmRuntimeShutdownHook();
}

bool SubmitAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  return algoscheduler::SubmitAlgorithmObjectForManager(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

bool SynchronizeVkAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return algoscheduler::SynchronizeVkAlgorithmObject(object, container_set, out_error_message);
}

bool SynchronizeCudaAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return algoscheduler::SynchronizeCudaAlgorithmObject(object, container_set, out_error_message);
}

bool TickMountedPipeline(
  std::vector<bridge::AlgorithmObject>* algorithm_objects,
  size_t begin_index,
  size_t end_index,
  const std::string& agent_name,
  const ::algomanager::bridge::AgentTickContext& context,
  const std::vector<bool>& allow_tick_mask,
  const std::vector<bridge::AlgorithmAssemblyState>& assembly_states,
  bool collect_timing_log,
  std::vector<bridge::AgentAlgorithmRuntimeState>* out_updated_runtime_states,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message) {
  return algoscheduler::TickMountedPipelineForManager(
    algorithm_objects,
    begin_index,
    end_index,
    agent_name,
    context,
    allow_tick_mask,
    assembly_states,
    collect_timing_log,
    out_updated_runtime_states,
    out_pipeline_signal,
    out_mounted_pipeline_processing_failed,
    out_error_message);
}

bool ExecuteCompatibilityAlgorithmObject(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::bridge::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  return algoscheduler::ExecuteCompatibilityAlgorithmObject(
    object,
    context,
    agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state,
    out_error_message);
}

bool ReplayMountedPipelineDebugNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  size_t child_index,
  const ::algomanager::bridge::AgentTickContext& context,
  std::string* out_error_message) {
  return algoscheduler::ReplayMountedPipelineDebugNodeForManager(
    pipeline_node,
    inout_runtime_state,
    child_index,
    context,
    out_error_message);
}

bool TickMountedPipelineNode(
  ::algomanager::bridge::AlgorithmObject* pipeline_node,
  ::algomanager::bridge::AgentAlgorithmRuntimeState* inout_runtime_state,
  const std::string& agent_name,
  const ::algomanager::bridge::AgentTickContext& context,
  bool allow_tick,
  const bridge::AlgorithmAssemblyState& assembly_state,
  bool collect_timing_log,
  common_data::AlgorithmToAgentSignal* out_pipeline_signal,
  bool* out_mounted_pipeline_processing_failed,
  std::string* out_error_message) {
  return algoscheduler::TickMountedPipelineNodeForManager(
    pipeline_node,
    inout_runtime_state,
    agent_name,
    context,
    allow_tick,
    assembly_state,
    collect_timing_log,
    out_pipeline_signal,
    out_mounted_pipeline_processing_failed,
    out_error_message);
}

bool TryGetMountedPipelineRuntime(
  const std::string& pipeline_name,
  const std::string& agent_name,
  bridge::JobsPipelineRuntimeState* out_runtime_state) {
  return algoscheduler::TryGetMountedPipelineRuntimeForManager(pipeline_name, agent_name, out_runtime_state);
}

bool TryGetMountedPipelineRegistration(
  const std::string& pipeline_name,
  bridge::JobsPipelineRegistration* out_registration) {
  return algoscheduler::TryGetMountedPipelineRegistrationForManager(
    pipeline_name,
    out_registration);
}

void UnregisterMountedPipeline(
  const std::string& pipeline_name,
  const std::string& agent_name) {
  algoscheduler::UnregisterMountedPipeline(pipeline_name, agent_name);
}

}  // namespace algomanager



