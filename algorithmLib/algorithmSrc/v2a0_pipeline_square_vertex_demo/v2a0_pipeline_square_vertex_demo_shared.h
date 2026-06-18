#pragma once

#include "../algorithm_plugin_api.h"

#include <memory>

namespace v2a0_pipeline_square_vertex_demo {

class DelegatingIntervention final : public agent::IAlgorithmIntervention {
 public:
  explicit DelegatingIntervention(std::shared_ptr<agent::IAlgorithmIntervention> inner)
    : inner_(std::move(inner)) {}

  bool SupportsIntervention() const override {
    return inner_ && inner_->SupportsIntervention();
  }

  void FillAgentToAlgorithmSignal(
    const agent::AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const override {
    if (!inner_) {
      if (out_signal) {
        *out_signal = {};
      }
      return;
    }
    inner_->FillAgentToAlgorithmSignal(context, out_signal);
  }

  bool GetInterventionStageSpecs(
    std::vector<agent::AlgorithmInterventionStageSpec>* out_stage_specs) const override {
    if (!inner_) {
      if (out_stage_specs) {
        out_stage_specs->clear();
      }
      return false;
    }
    return inner_->GetInterventionStageSpecs(out_stage_specs);
  }

 private:
  std::shared_ptr<agent::IAlgorithmIntervention> inner_{};
};

inline void DestroyIntervention(agent::IAlgorithmIntervention* intervention) {
  delete intervention;
}

inline bool CreateBundle(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm_library_plugin::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->cpu_symbol = false;
  out_bundle->gpu_symbol = true;

  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }

  std::shared_ptr<agent::IAlgorithmIntervention> intervention{};
  if (!algorithm_support::LoadAlgorithmInterventionFromLocation(
        package_location,
        &intervention,
        nullptr) || !intervention) {
    return false;
  }

  out_bundle->intervention = new DelegatingIntervention(std::move(intervention));
  out_bundle->destroy_intervention = &DestroyIntervention;
  return true;
}

inline bool CreateRuntimeReflector(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algorithm_support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}

}  // namespace v2a0_pipeline_square_vertex_demo
