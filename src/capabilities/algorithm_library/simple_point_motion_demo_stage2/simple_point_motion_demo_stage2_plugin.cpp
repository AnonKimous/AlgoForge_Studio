#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "capabilities/algorithm_library/algorithm_plugin_api.h"

#include "cJSON.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr float kFallbackDtSeconds = 1.0f / 60.0f;
constexpr float kDefaultLeft = 50.0f;
constexpr float kDefaultRight = 150.0f;
constexpr float kDefaultBottom = 50.0f;
constexpr float kDefaultTop = 150.0f;
constexpr float kDefaultPhaseSpeed = 0.50f;

enum class DemoStageMode {
  Generator = 0,
  TopEdge = 1,
  RightEdge = 2,
  BottomEdge = 3,
};

struct DemoStageConfig {
  DemoStageMode mode{DemoStageMode::Generator};
  float left{kDefaultLeft};
  float right{kDefaultRight};
  float bottom{kDefaultBottom};
  float top{kDefaultTop};
  float phase_speed{kDefaultPhaseSpeed};
  bool valid{false};
  std::string error_message;
};

std::string _ReadTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GetStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
}

float _GetFloatField(const cJSON* object, const char* key, float fallback) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsNumber(item)) {
    return fallback;
  }
  return static_cast<float>(item->valuedouble);
}

std::string _BuildPath(
  const algorithm_library_plugin::AlgorithmPluginRequest& request,
  const std::string& suffix) {
  const std::string root = request.algorithm_library_root ? request.algorithm_library_root : "";
  const std::string folder = request.algorithm_folder ? request.algorithm_folder : "";
  return root + "/" + folder + "/" + folder + suffix;
}

bool _ReadScalar(
  const algorithm::AlgorithmContainerSet* container_set,
  const char* container_name,
  float* out_value) {
  if (!container_set || !container_name || !out_value) {
    return false;
  }
  const algorithm::AlgorithmContainer* container =
    algorithm::FindAlgorithmContainer(*container_set, container_name);
  if (!container || container->bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(out_value, container->bytes.data(), sizeof(float));
  return true;
}

bool _WriteScalar(
  algorithm::AlgorithmContainerSet* container_set,
  const char* container_name,
  float value) {
  if (!container_set || !container_name) {
    return false;
  }
  algorithm::AlgorithmContainer* container =
    algorithm::FindAlgorithmContainer(container_set, container_name);
  if (!container || container->bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data(), &value, sizeof(float));
  return true;
}

float _Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float _Lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

DemoStageConfig _LoadDemoStageConfig(const algorithm_library_plugin::AlgorithmPluginRequest& request) {
  DemoStageConfig config{};
  const std::string path = _BuildPath(request, "_package.json");
  const std::string json_text = _ReadTextFile(path);
  if (json_text.empty()) {
    config.error_message = "Failed to read demo package JSON file: " + path;
    return config;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    config.error_message = "Failed to parse demo package JSON file: " + path;
    return config;
  }

  const cJSON* demo = cJSON_GetObjectItemCaseSensitive(root, "demo");
  if (!demo || !cJSON_IsObject(demo)) {
    config.error_message = "Package JSON is missing a demo section: " + path;
    cJSON_Delete(root);
    return config;
  }

  const std::string mode_text = _GetStringField(demo, "mode");
  if (mode_text == "generator") {
    config.mode = DemoStageMode::Generator;
  } else if (mode_text == "top_edge") {
    config.mode = DemoStageMode::TopEdge;
  } else if (mode_text == "right_edge") {
    config.mode = DemoStageMode::RightEdge;
  } else if (mode_text == "bottom_edge") {
    config.mode = DemoStageMode::BottomEdge;
  } else {
    config.error_message = "Package demo mode is invalid: " + path;
    cJSON_Delete(root);
    return config;
  }

  config.left = _GetFloatField(demo, "left", kDefaultLeft);
  config.right = _GetFloatField(demo, "right", kDefaultRight);
  config.bottom = _GetFloatField(demo, "bottom", kDefaultBottom);
  config.top = _GetFloatField(demo, "top", kDefaultTop);
  config.phase_speed = _GetFloatField(demo, "phase_speed", kDefaultPhaseSpeed);
  if (!(config.right > config.left) || !(config.top > config.bottom) || !(config.phase_speed > 0.0f)) {
    config.error_message = "Package demo bounds or phase speed are invalid: " + path;
    cJSON_Delete(root);
    return config;
  }

  cJSON_Delete(root);
  config.valid = true;
  return config;
}

class DemoStageExecutor final : public agent::IAlgorithmtemporaryTestMainThreadExecutor {
 public:
  explicit DemoStageExecutor(DemoStageConfig config)
    : config_(std::move(config)) {}

  bool temporaryTestExecuteOnMainThread(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    if (!algorithm_container_set || !config_.valid) {
      return false;
    }

    const float dt_seconds = context.dt_seconds > 0.0f ? context.dt_seconds : kFallbackDtSeconds;
    float x = config_.left;
    float y = config_.bottom;

    switch (config_.mode) {
      case DemoStageMode::Generator: {
        const float phase = phase_;
        phase_ += config_.phase_speed * dt_seconds;
        phase_ -= std::floor(phase_);
        x = config_.left;
        y = _Lerp(config_.bottom, config_.top, phase);
        break;
      }
      case DemoStageMode::TopEdge: {
        float incoming_y = config_.bottom;
        if (!_ReadScalar(algorithm_container_set, "v2", &incoming_y)) {
          return false;
        }
        const float t = _Clamp01((incoming_y - config_.bottom) / (config_.top - config_.bottom));
        x = _Lerp(config_.left, config_.right, t);
        y = config_.top;
        break;
      }
      case DemoStageMode::RightEdge: {
        float incoming_x = config_.left;
        if (!_ReadScalar(algorithm_container_set, "v1", &incoming_x)) {
          return false;
        }
        const float t = _Clamp01((incoming_x - config_.left) / (config_.right - config_.left));
        x = config_.right;
        y = _Lerp(config_.top, config_.bottom, t);
        break;
      }
      case DemoStageMode::BottomEdge: {
        float incoming_y = config_.top;
        if (!_ReadScalar(algorithm_container_set, "v2", &incoming_y)) {
          return false;
        }
        const float t = _Clamp01((config_.top - incoming_y) / (config_.top - config_.bottom));
        x = _Lerp(config_.right, config_.left, t);
        y = config_.bottom;
        break;
      }
    }

    if (!_WriteScalar(algorithm_container_set, "v1", x) ||
        !_WriteScalar(algorithm_container_set, "v2", y)) {
      return false;
    }

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = algorithm_profile.algorithm_name + ".xy",
        .payload = std::to_string(x) + "," + std::to_string(y),
      });
    }
    return true;
  }

 private:
  DemoStageConfig config_{};
  float phase_{0.0f};
};

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

void _DestroyIntervention(agent::IAlgorithmIntervention* intervention) {
  delete intervention;
}

void _DestroyTemporaryTestExecutor(agent::IAlgorithmtemporaryTestMainThreadExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm_library_plugin::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->cpu_symbol = true;
  out_bundle->gpu_symbol = true;

  const DemoStageConfig config = _LoadDemoStageConfig(*request);
  if (!config.valid) {
    return false;
  }

  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }

  std::shared_ptr<agent::IAlgorithmIntervention> inner_intervention{};
  if (!algorithm_support::LoadAlgorithmInterventionFromLocation(
        package_location,
        &inner_intervention,
        nullptr)) {
    return false;
  }

  out_bundle->intervention = new DelegatingIntervention(std::move(inner_intervention));
  out_bundle->destroy_intervention = &_DestroyIntervention;
  out_bundle->temporary_test_executor = new DemoStageExecutor(config);
  out_bundle->destroy_temporary_test_executor = &_DestroyTemporaryTestExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
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
        nullptr)) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}
