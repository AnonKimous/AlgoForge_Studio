#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr float kWaveStep = 0.08f;

algorithm::AlgorithmContainer* RequireFloatRegister(
  algorithm::AlgorithmContainerSet* container_set,
  const char* container_name) {
  assert(container_set && "Algorithm container set must be valid.");
  assert(container_name && "Container name must be valid.");
  algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(container_set, container_name);
  assert(container && "Required algorithm container is missing.");
  assert(container->storage_kind == algorithm::AlgorithmContainerStorageKind::TemporaryRegister);
  assert(container->element_stride >= sizeof(float));
  assert(container->bytes.size() >= sizeof(float));
  return container;
}

float ReadFloat(const algorithm::AlgorithmContainer& container) {
  float value = 0.0f;
  std::memcpy(&value, container.bytes.data(), sizeof(value));
  return value;
}

void WriteFloat(algorithm::AlgorithmContainer* container, float value) {
  std::memcpy(container->bytes.data(), &value, sizeof(value));
}

class TideFastPathJobsExecutor final : public agent::IAlgorithmJobsExecutor {
 public:
  bool ExecuteJobsAlgorithm(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    assert(algorithm_container_set && "Algorithm container set must be valid.");

    algorithm::AlgorithmContainer* wave_input = RequireFloatRegister(algorithm_container_set, "v1");
    algorithm::AlgorithmContainer* tide_output = RequireFloatRegister(algorithm_container_set, "v2");

    const float next_wave = ReadFloat(*wave_input) + kWaveStep;
    WriteFloat(wave_input, next_wave);
    WriteFloat(tide_output, std::sinf(next_wave));

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algomanager::algoscheduler::AdvancedAlgorithmDebugSignal{
        .name = "v2a0minimal_tide_fastpath.jobs",
        .payload = "v1=" + std::to_string(next_wave) + ", v2=" + std::to_string(std::sinf(next_wave)),
      });
    }
    return true;
  }
};

void DestroyJobsExecutor(agent::IAlgorithmJobsExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algomanager::support::AlgorithmPluginRequest* request,
  algomanager::support::AlgorithmPluginBundle* out_bundle) {
  (void)request;
  assert(out_bundle && "Algorithm plugin bundle output must be valid.");

  out_bundle->Clear();
  out_bundle->jobs_symbol = true;
  out_bundle->vk_symbol = false;
  out_bundle->cuda_symbol = false;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->jobs_executor = new TideFastPathJobsExecutor();
  out_bundle->destroy_jobs_executor = &DestroyJobsExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algomanager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  assert(request && "Algorithm plugin request must be valid.");
  assert(out_reflector && "Runtime reflector output must be valid.");

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algomanager::support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}
