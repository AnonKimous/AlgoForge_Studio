#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cuda_runtime.h>

#include <cstring>

namespace {

constexpr float kLeftStep = -0.10f;
constexpr float kUpStep = 0.03f;

__global__ void AdvanceScalarKernel(float* value, float delta) {
  if (blockIdx.x == 0u && threadIdx.x == 0u) {
    *value += delta;
  }
}

bool _AdvanceScalarCuda(algorithm::AlgorithmContainer* container, float delta) {
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

  float* device_value = nullptr;
  if (cudaMalloc(reinterpret_cast<void**>(&device_value), sizeof(float)) != cudaSuccess) {
    return false;
  }

  const bool copied_to_device =
    cudaMemcpy(device_value, &value, sizeof(value), cudaMemcpyHostToDevice) == cudaSuccess;
  if (copied_to_device) {
    AdvanceScalarKernel<<<1u, 1u>>>(device_value, delta);
    if (cudaGetLastError() == cudaSuccess &&
        cudaDeviceSynchronize() == cudaSuccess &&
        cudaMemcpy(&value, device_value, sizeof(value), cudaMemcpyDeviceToHost) == cudaSuccess) {
      cudaFree(device_value);
      std::memcpy(container->bytes.data(), &value, sizeof(value));
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

    if (!algorithm_container_set) {
      return false;
    }

    algorithm::AlgorithmContainer* point_x =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_x");
    algorithm::AlgorithmContainer* point_y =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_y");
    algorithm::AlgorithmContainer* point_z =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_z");
    if (!_AdvanceScalarCuda(point_x, kLeftStep) ||
        !_AdvanceScalarCuda(point_y, kUpStep) ||
        !_AdvanceScalarCuda(point_z, 0.0f)) {
      return false;
    }

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = "temporary_test_line_motion.cuda",
        .payload = "Moved point_x/point_y using CUDA.",
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
