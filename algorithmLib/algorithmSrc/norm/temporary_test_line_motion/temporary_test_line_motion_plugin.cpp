#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cstring>

#if defined(ALGORITHM_LIBRARY_PLUGIN_HAS_CUDA)
agent::IAlgorithmCudaExecutor* CreateTemporaryTestLineMotionCudaExecutor();
void DestroyTemporaryTestLineMotionCudaExecutor(agent::IAlgorithmCudaExecutor* executor);
#endif

namespace {

constexpr float kLeftStep = -0.10f;
constexpr float kUpStep = 0.03f;

bool AdvanceScalar(algorithm::AlgorithmContainer* container, float delta) {
  if (!container) {
    return false;
  }
  if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister) {
    return false;
  }
  if (container->element_stride < sizeof(float)) {
    return false;
  }
  if (container->bytes.size() < sizeof(float)) {
    return false;
  }
  float value = 0.0f;
  std::memcpy(&value, container->bytes.data(), sizeof(value));
  value += delta;
  std::memcpy(container->bytes.data(), &value, sizeof(value));
  return true;
}

class LineMotionJobsExecutor final : public agent::IAlgorithmJobsExecutor {
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
    if (!algorithm_container_set) {
      return false;
    }

    algorithm::AlgorithmContainer* point_x =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_x");
    algorithm::AlgorithmContainer* point_y =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_y");
    algorithm::AlgorithmContainer* point_z =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_z");
    if (!AdvanceScalar(point_x, kLeftStep) ||
        !AdvanceScalar(point_y, kUpStep) ||
        !AdvanceScalar(point_z, 0.0f)) {
      return false;
    }

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algomanager::algoscheduler::AdvancedAlgorithmDebugSignal{
        .name = "temporary_test_line_motion.body",
        .payload = "Moved point_x/point_y toward the upper-left corner.",
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
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->jobs_symbol = true;
  out_bundle->vk_symbol = false;
  out_bundle->cuda_symbol = false;
  out_bundle->reflector = false;
  out_bundle->intervention = true;
  out_bundle->jobs_executor = new LineMotionJobsExecutor();
  out_bundle->destroy_jobs_executor = &DestroyJobsExecutor;
#if defined(ALGORITHM_LIBRARY_PLUGIN_HAS_CUDA)
  out_bundle->cuda_symbol = true;
  out_bundle->cuda_executor = CreateTemporaryTestLineMotionCudaExecutor();
  out_bundle->destroy_cuda_executor = &DestroyTemporaryTestLineMotionCudaExecutor;
#endif
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algomanager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

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
