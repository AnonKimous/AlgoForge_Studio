#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRenderDt = 1.0f / 60.0f;
constexpr float kLanceThickness = 14.0f;
constexpr uint32_t kSceneDataCount = 12u;

float* _SceneData(algorithm::AlgorithmContainerSet* container_set) {
  static std::ofstream trace("testData\\knight_lance_coordinate_trace.log", std::ios::app);
  trace << "scene.lookup.begin container_set=" << container_set << std::endl;
  trace << "scene.lookup.container_read.begin";
  trace << " arrays=" << container_set->arrays.size();
  for (const algorithm::AlgorithmContainer& container : container_set->arrays) {
    trace << " array=" << container.name << ":" << container.bytes.size();
  }
  trace << " registers=" << container_set->temporary_registers.size();
  for (const algorithm::AlgorithmContainer& container : container_set->temporary_registers) {
    trace << " register=" << container.name << ":" << container.bytes.size();
  }
  trace << std::endl;
  algorithm::AlgorithmContainer* scene =
    algorithm::FindAlgorithmContainer(container_set, "scene_data");
  assert(scene);
  assert(scene->bytes.size() >= sizeof(float) * kSceneDataCount);
  return reinterpret_cast<float*>(scene->bytes.data());
}

void _WriteDrawCommand(algorithm::AlgorithmContainerSet* container_set) {
  algorithm::AlgorithmContainer* draw =
    algorithm::FindAlgorithmContainer(container_set, "render_draw");
  assert(draw);
  assert(draw->bytes.size() >= sizeof(uint32_t) * 4u);
  const uint32_t command[4] = {4u, 2u, 0u, 0u};
  std::memcpy(draw->bytes.data(), command, sizeof(command));
}

class KnightLanceCoordinateJobsExecutor final : public algomanager::bridge::IAlgorithmJobsExecutor {
 public:
  bool ExecuteJobsAlgorithm(
    const algomanager::bridge::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    algomanager::bridge::AlgorithmPackageDebugState* debug_state) override {
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;

    static std::ofstream trace("testData\\knight_lance_coordinate_trace.log", std::ios::app);
    trace << "exec.begin" << std::endl;

    float* scene = _SceneData(algorithm_container_set);
    trace << "scene.loaded" << std::endl;
    const float dt = context.dt_seconds > 0.0f ? context.dt_seconds : kRenderDt;
    const float angle_radians = scene[2] * kPi / 180.0f;
    const float direction_x = std::cos(angle_radians);
    const float direction_y = std::sin(angle_radians);
    const float half_length = scene[4] * 0.5f;
    const float tip_x = scene[0] + direction_x * half_length;
    const float tip_y = scene[1] + direction_y * half_length;

    if (scene[9] < 0.5f) {
      scene[0] += direction_x * scene[3] * dt;
      scene[1] += direction_y * scene[3] * dt;
      const float moved_tip_x = scene[0] + direction_x * half_length;
      const float moved_tip_y = scene[1] + direction_y * half_length;
      const float solid_left = scene[5];
      const float solid_top = scene[6] + scene[8];
      if ((direction_x > 0.0f && moved_tip_x >= solid_left &&
           std::abs(moved_tip_y - scene[6] - scene[8] * 0.5f) <= scene[8] * 0.5f) ||
          (direction_x < 0.0f && moved_tip_x <= solid_left + scene[7] &&
           std::abs(moved_tip_y - scene[6] - scene[8] * 0.5f) <= scene[8] * 0.5f)) {
        scene[9] = 1.0f;
        scene[10] = moved_tip_x;
        scene[11] = moved_tip_y;
      }
      (void)tip_x;
      (void)tip_y;
      (void)solid_top;
    }

    _WriteDrawCommand(algorithm_container_set);
    trace << "draw.command_written" << std::endl;
    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algomanager::bridge::AdvancedAlgorithmDebugSignal{
        .name = "knight_lance_coordinate_demo.state",
        .payload = "lance_x=" + std::to_string(scene[0]) +
          " lance_y=" + std::to_string(scene[1]) +
          " impact=" + std::to_string(scene[9]),
      });
    }
    trace << "exec.end" << std::endl;
    return true;
  }
};

void _DestroyJobsExecutor(algomanager::bridge::IAlgorithmJobsExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algomanager::algocatalog::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }
  out_bundle->Clear();
  out_bundle->jobs_symbol = true;
  out_bundle->vk_symbol = true;
  out_bundle->cuda_symbol = false;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->jobs_executor = new KnightLanceCoordinateJobsExecutor();
  out_bundle->destroy_jobs_executor = &_DestroyJobsExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }
  std::shared_ptr<algorithm::AlgorithmReflector> reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algomanager::algocatalog::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &reflector,
        nullptr)) {
    return false;
  }
  *out_reflector = *reflector;
  return true;
}
