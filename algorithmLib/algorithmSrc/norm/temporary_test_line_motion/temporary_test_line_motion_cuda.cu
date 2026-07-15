#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cuda_runtime.h>

#include <cstring>
#include <fstream>

namespace {

constexpr float kLeftStep = -0.10f;
constexpr float kUpStep = 0.03f;

__global__ void AdvanceScalarKernel(float* value, float delta) {
  if (blockIdx.x == 0u && threadIdx.x == 0u) {
    *value += delta;
  }
}

bool _AdvanceScalarCuda(float* value, float delta) {
  float* device_value = nullptr;
  if (cudaMalloc(reinterpret_cast<void**>(&device_value), sizeof(float)) != cudaSuccess) {
    return false;
  }

  const bool copied_to_device =
    cudaMemcpy(device_value, value, sizeof(*value), cudaMemcpyHostToDevice) == cudaSuccess;
  if (copied_to_device) {
    AdvanceScalarKernel<<<1u, 1u>>>(device_value, delta);
    if (cudaGetLastError() == cudaSuccess &&
        cudaDeviceSynchronize() == cudaSuccess &&
        cudaMemcpy(value, device_value, sizeof(*value), cudaMemcpyDeviceToHost) == cudaSuccess) {
      cudaFree(device_value);
      return true;
    }
  }

  cudaFree(device_value);
  return false;
}

class LineMotionCudaExecutor final : public agent::IAlgorithmCudaExecutor {
 public:
  bool ExecuteCudaAlgorithm(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;

    (void)algorithm_container_set;
    std::ofstream trace("testData\\cuda_algorithm_probe.log", std::ios::app);
    trace << "cuda_executor.begin\n";
    float value = 0.0f;
    if (!_AdvanceScalarCuda(&value, kLeftStep)) {
      return false;
    }
    trace << "cuda_executor.kernel_done value=" << value << "\n";

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = "temporary_test_line_motion.cuda",
        .payload = "CUDA kernel advanced its private scalar.",
      });
    }
    return true;
  }
};

}  // namespace

agent::IAlgorithmCudaExecutor* CreateTemporaryTestLineMotionCudaExecutor() {
  return new LineMotionCudaExecutor();
}

void DestroyTemporaryTestLineMotionCudaExecutor(agent::IAlgorithmCudaExecutor* executor) {
  delete executor;
}
