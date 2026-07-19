#pragma once

#include "algomanager/bridge/algorithm_abi.h"
#include "algomanager/bridge/algorithm_package_location.h"

#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtimesys/runtime_environment.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace algomanager { namespace algoscheduler {

inline bool ExecuteJobsAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::algomanager::algoscheduler::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message);
inline bool ExecuteVkAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::algomanager::algoscheduler::AgentTickContext& context,
  std::string* out_error_message);
inline bool HasExecutableVkAlgorithmStage(const ::algomanager::algoscheduler::AlgorithmObject& object);
inline bool SynchronizeVkAlgorithmObject(
  const ::algomanager::algoscheduler::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message);

inline void SetAlgorithmRuntimeShutdownHook() {
  runtimesys::SetRuntimeShutdownCallback(&ClearAlgorithmScheduler);
  runtimesys::SetRuntimeVkCacheClearCallback(&ClearAlgorithmExecutionCaches);
}

inline void ClearAlgorithmExecutionCaches() {
  runtimesys::ClearRuntimeVkJobCaches();
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

inline bool ExecuteJobsAlgorithmObject(
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

inline bool ExecuteVkAlgorithmObject(
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

inline bool HasExecutableVkAlgorithmStage(const ::algomanager::algoscheduler::AlgorithmObject& object) {
  if (!object.vk_executor) {
    return false;
  }
  ::algomanager::algoscheduler::AlgorithmVkExecSpec vk_exec_spec{};
  return object.vk_executor->GetVkExecSpec(&vk_exec_spec) &&
    !vk_exec_spec.shader.vertex_shader_path.empty() &&
    !vk_exec_spec.shader.fragment_shader_path.empty() &&
    !vk_exec_spec.used_algorithm_containers.empty();
}

inline bool SynchronizeVkAlgorithmObject(
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
  return ::algomanager::bridge::runtime_bridge_support::ResolveAlgorithmVkShaderPath(
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
  return ::algomanager::bridge::runtime_bridge_support::TryBuildAlgorithmInterventionVkPhaseSubJob(
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
  return ::algomanager::bridge::runtime_bridge_support::TryBuildAlgorithmVkExecStageSubJob(
    object,
    vk_exec_spec,
    container_set,
    out_stage_job,
    out_error_message);
}

inline bool ExecuteAlgorithmObjectStagePlan(
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
           stage_entries[bundle_end].execution_preference == bundle_preference) {
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

}  // namespace scheduler
}  // namespace algomanager

