#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "../algorithm_plugin_api.h"

namespace v3a16_fireworks_pipeline_demo {

namespace {

inline algorithm::AlgorithmContainer* _FindMutableContainer(
  algorithm::AlgorithmContainerSet* container_set,
  const char* container_name) {
  return algorithm::FindAlgorithmContainer(container_set, container_name);
}

inline const algorithm::AlgorithmContainer* _FindConstContainer(
  const algorithm::AlgorithmContainerSet& container_set,
  const char* container_name) {
  return algorithm::FindAlgorithmContainer(container_set, container_name);
}

inline float _ReadScalar(const algorithm::AlgorithmContainer* container) {
  float value = 0.0f;
  std::memcpy(&value, container->bytes.data(), sizeof(value));
  return value;
}

inline void _WriteScalar(algorithm::AlgorithmContainer* container, float value) {
  std::memcpy(container->bytes.data(), &value, sizeof(value));
}

inline size_t _ArrayCount(const algorithm::AlgorithmContainer* container) {
  return container->bytes.size() / container->element_stride;
}

inline float _Random01(uint32_t seed) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return static_cast<float>(seed & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

inline Vec2 _PixelToLogical(Vec2 pixel, Vec2 extent) {
  return Vec2{
    (pixel.x / extent.x) * 2.0f - 1.0f,
    (pixel.y / extent.y) * 2.0f - 1.0f,
  };
}

inline Vec2 _LogicalToPixel(Vec2 logical, Vec2 extent) {
  return Vec2{
    (logical.x * 0.5f + 0.5f) * extent.x,
    (logical.y * 0.5f + 0.5f) * extent.y,
  };
}

inline Vec2 _PixelVelocityToLogical(Vec2 pixel_velocity, Vec2 extent) {
  return Vec2{
    (pixel_velocity.x / extent.x) * 2.0f,
    (pixel_velocity.y / extent.y) * 2.0f,
  };
}

inline Vec2 _LogicalVelocityToPixel(Vec2 logical_velocity, Vec2 extent) {
  return Vec2{
    logical_velocity.x * extent.x * 0.5f,
    logical_velocity.y * extent.y * 0.5f,
  };
}

class FireworksCpuExecutor final : public agent::IAlgorithmCpuExecutor {
 public:
  explicit FireworksCpuExecutor(std::string algorithm_name)
    : algorithm_name_(std::move(algorithm_name)) {}

  bool ExecuteCpuAlgorithm(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)agent_to_algorithm_signal;

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = algorithm_profile.algorithm_name + ".cpu",
        .payload = "v3a16 cpu executor tick",
      });
    }
    if (!algorithm_container_set) {
      return false;
    }

    algorithm::AlgorithmContainer* v1 = _FindMutableContainer(algorithm_container_set, "v1");
    algorithm::AlgorithmContainer* v2 = _FindMutableContainer(algorithm_container_set, "v2");
    algorithm::AlgorithmContainer* v3 = _FindMutableContainer(algorithm_container_set, "v3");
    algorithm::AlgorithmContainer* shell_pos_x = _FindMutableContainer(algorithm_container_set, "shell_pos_x");
    algorithm::AlgorithmContainer* shell_pos_y = _FindMutableContainer(algorithm_container_set, "shell_pos_y");
    algorithm::AlgorithmContainer* shell_vel_x = _FindMutableContainer(algorithm_container_set, "shell_vel_x");
    algorithm::AlgorithmContainer* shell_vel_y = _FindMutableContainer(algorithm_container_set, "shell_vel_y");
    algorithm::AlgorithmContainer* shell_state = _FindMutableContainer(algorithm_container_set, "shell_state");
    algorithm::AlgorithmContainer* shell_age = _FindMutableContainer(algorithm_container_set, "shell_age");
    algorithm::AlgorithmContainer* shell_life = _FindMutableContainer(algorithm_container_set, "shell_life");
    algorithm::AlgorithmContainer* shell_seed = _FindMutableContainer(algorithm_container_set, "shell_seed");
    algorithm::AlgorithmContainer* spark_pos_x = _FindMutableContainer(algorithm_container_set, "spark_pos_x");
    algorithm::AlgorithmContainer* spark_pos_y = _FindMutableContainer(algorithm_container_set, "spark_pos_y");
    algorithm::AlgorithmContainer* spark_vel_x = _FindMutableContainer(algorithm_container_set, "spark_vel_x");
    algorithm::AlgorithmContainer* spark_vel_y = _FindMutableContainer(algorithm_container_set, "spark_vel_y");
    algorithm::AlgorithmContainer* spark_state = _FindMutableContainer(algorithm_container_set, "spark_state");
    algorithm::AlgorithmContainer* spark_age = _FindMutableContainer(algorithm_container_set, "spark_age");
    algorithm::AlgorithmContainer* spark_life = _FindMutableContainer(algorithm_container_set, "spark_life");
    algorithm::AlgorithmContainer* spark_seed = _FindMutableContainer(algorithm_container_set, "spark_seed");

    const float tick_value = _ReadScalar(v1);
    const float launch_budget_value = _ReadScalar(v2);
    const float live_spark_value = _ReadScalar(v3);
    const uint32_t tick_seed = static_cast<uint32_t>(tick_value) + 1u;
    const Vec2 preview_extent = context.render_preview_extent;

    const auto convert_shell_coordinates = [&](bool to_logical) {
      if (!(shell_state && shell_pos_x && shell_pos_y && shell_vel_x && shell_vel_y && shell_age && shell_life && shell_seed)) {
        return;
      }
      const size_t shell_count = _ArrayCount(shell_state);
      for (size_t i = 0u; i < shell_count; ++i) {
        float state = 0.0f;
        std::memcpy(&state, shell_state->bytes.data() + i * shell_state->element_stride, sizeof(state));
        if (state <= 0.5f) {
          const float zero = 0.0f;
          std::memcpy(shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, &zero, sizeof(zero));
          std::memcpy(shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, &zero, sizeof(zero));
          std::memcpy(shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, &zero, sizeof(zero));
          std::memcpy(shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, &zero, sizeof(zero));
          std::memcpy(shell_age->bytes.data() + i * shell_age->element_stride, &zero, sizeof(zero));
          std::memcpy(shell_life->bytes.data() + i * shell_life->element_stride, &zero, sizeof(zero));
          std::memcpy(shell_seed->bytes.data() + i * shell_seed->element_stride, &zero, sizeof(zero));
          std::memcpy(shell_state->bytes.data() + i * shell_state->element_stride, &zero, sizeof(zero));
          continue;
        }

        Vec2 pos{};
        Vec2 vel{};
        float age = 0.0f;
        float life = 0.0f;
        float seed = 0.0f;
        std::memcpy(&pos.x, shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, sizeof(pos.x));
        std::memcpy(&pos.y, shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, sizeof(pos.y));
        std::memcpy(&vel.x, shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, sizeof(vel.x));
        std::memcpy(&vel.y, shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, sizeof(vel.y));
        std::memcpy(&age, shell_age->bytes.data() + i * shell_age->element_stride, sizeof(age));
        std::memcpy(&life, shell_life->bytes.data() + i * shell_life->element_stride, sizeof(life));
        std::memcpy(&seed, shell_seed->bytes.data() + i * shell_seed->element_stride, sizeof(seed));

        if (to_logical) {
          pos = _PixelToLogical(pos, preview_extent);
          vel = _PixelVelocityToLogical(vel, preview_extent);
        } else {
          pos = _LogicalToPixel(pos, preview_extent);
          vel = _LogicalVelocityToPixel(vel, preview_extent);
        }

        std::memcpy(shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, &pos.x, sizeof(pos.x));
        std::memcpy(shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, &pos.y, sizeof(pos.y));
        std::memcpy(shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, &vel.x, sizeof(vel.x));
        std::memcpy(shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, &vel.y, sizeof(vel.y));
        std::memcpy(shell_age->bytes.data() + i * shell_age->element_stride, &age, sizeof(age));
        std::memcpy(shell_life->bytes.data() + i * shell_life->element_stride, &life, sizeof(life));
        std::memcpy(shell_seed->bytes.data() + i * shell_seed->element_stride, &seed, sizeof(seed));
        std::memcpy(shell_state->bytes.data() + i * shell_state->element_stride, &state, sizeof(state));
      }
    };

    const auto convert_spark_coordinates = [&](bool to_logical) {
      if (!(spark_state && spark_pos_x && spark_pos_y && spark_vel_x && spark_vel_y && spark_age && spark_life && spark_seed)) {
        return;
      }
      const size_t spark_count = _ArrayCount(spark_state);
      for (size_t i = 0u; i < spark_count; ++i) {
        float state = 0.0f;
        std::memcpy(&state, spark_state->bytes.data() + i * spark_state->element_stride, sizeof(state));
        if (state <= 0.5f) {
          const float zero = 0.0f;
          std::memcpy(spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, &zero, sizeof(zero));
          std::memcpy(spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, &zero, sizeof(zero));
          std::memcpy(spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, &zero, sizeof(zero));
          std::memcpy(spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, &zero, sizeof(zero));
          std::memcpy(spark_age->bytes.data() + i * spark_age->element_stride, &zero, sizeof(zero));
          std::memcpy(spark_life->bytes.data() + i * spark_life->element_stride, &zero, sizeof(zero));
          std::memcpy(spark_seed->bytes.data() + i * spark_seed->element_stride, &zero, sizeof(zero));
          std::memcpy(spark_state->bytes.data() + i * spark_state->element_stride, &zero, sizeof(zero));
          continue;
        }

        Vec2 pos{};
        Vec2 vel{};
        float age = 0.0f;
        float life = 0.0f;
        float seed = 0.0f;
        std::memcpy(&pos.x, spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, sizeof(pos.x));
        std::memcpy(&pos.y, spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, sizeof(pos.y));
        std::memcpy(&vel.x, spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, sizeof(vel.x));
        std::memcpy(&vel.y, spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, sizeof(vel.y));
        std::memcpy(&age, spark_age->bytes.data() + i * spark_age->element_stride, sizeof(age));
        std::memcpy(&life, spark_life->bytes.data() + i * spark_life->element_stride, sizeof(life));
        std::memcpy(&seed, spark_seed->bytes.data() + i * spark_seed->element_stride, sizeof(seed));

        if (to_logical) {
          pos = _PixelToLogical(pos, preview_extent);
          vel = _PixelVelocityToLogical(vel, preview_extent);
        } else {
          pos = _LogicalToPixel(pos, preview_extent);
          vel = _LogicalVelocityToPixel(vel, preview_extent);
        }

        std::memcpy(spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, &pos.x, sizeof(pos.x));
        std::memcpy(spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, &pos.y, sizeof(pos.y));
        std::memcpy(spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, &vel.x, sizeof(vel.x));
        std::memcpy(spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, &vel.y, sizeof(vel.y));
        std::memcpy(spark_age->bytes.data() + i * spark_age->element_stride, &age, sizeof(age));
        std::memcpy(spark_life->bytes.data() + i * spark_life->element_stride, &life, sizeof(life));
        std::memcpy(spark_seed->bytes.data() + i * spark_seed->element_stride, &seed, sizeof(seed));
        std::memcpy(spark_state->bytes.data() + i * spark_state->element_stride, &state, sizeof(state));
      }
    };

    if (algorithm_name_ == "v3a16_fireworks_pipeline_demo_stageBegin") {
      convert_shell_coordinates(true);
      convert_spark_coordinates(true);
      _WriteScalar(v1, tick_value);
      _WriteScalar(v2, launch_budget_value);
      _WriteScalar(v3, live_spark_value);
    } else if (algorithm_name_ == "v3a16_fireworks_pipeline_demo") {
      const uint32_t window_frame = static_cast<uint32_t>(tick_value) % 30u;
      const bool window_reset = window_frame == 0u;
      const float launch_budget = window_reset ? 12.0f : launch_budget_value;
      const uint32_t launch_budget_count = static_cast<uint32_t>(std::floor(launch_budget));
      const uint32_t launch_quota = std::min<uint32_t>(
        launch_budget_count,
        1u + static_cast<uint32_t>(std::floor(_Random01(tick_seed + 23u) * 2.0f)));
      uint32_t inactive_count = 0u;
      for (size_t i = 0u; i < _ArrayCount(shell_state); ++i) {
        float state = 0.0f;
        std::memcpy(&state, shell_state->bytes.data() + i * shell_state->element_stride, sizeof(state));
        if (state < 0.5f) {
          ++inactive_count;
        }
      }
      const uint32_t spawned_count = std::min<uint32_t>(launch_quota, inactive_count);
      _WriteScalar(v1, tick_value + 1.0f);
      _WriteScalar(v2, std::max(0.0f, launch_budget - static_cast<float>(spawned_count)));
      _WriteScalar(v3, live_spark_value);
      if (shell_state && shell_pos_x && shell_pos_y && shell_vel_x && shell_vel_y && shell_age && shell_life && shell_seed) {
        const size_t shell_count = _ArrayCount(shell_state);
        for (size_t i = 0; i < shell_count; ++i) {
          float state = 0.0f;
          std::memcpy(&state, shell_state->bytes.data() + i * shell_state->element_stride, sizeof(state));
          if (state != 0.0f) {
            continue;
          }
          float slot_score = _Random01(tick_seed + static_cast<uint32_t>(i) * 17u);
          uint32_t rank = 0u;
          for (size_t other = 0u; other < shell_count; ++other) {
            float other_state = 0.0f;
            std::memcpy(&other_state, shell_state->bytes.data() + other * shell_state->element_stride, sizeof(other_state));
            if (other_state >= 0.5f) {
              continue;
            }
            float other_score = _Random01(tick_seed + static_cast<uint32_t>(other) * 17u);
            if (other_score < slot_score || (other_score == slot_score && other < i)) {
              ++rank;
            }
          }
          if (rank >= launch_quota) {
            continue;
          }
          const float x = -0.88f + _Random01(tick_seed + static_cast<uint32_t>(i) * 17u) * 1.76f;
          const float y = -0.96f + _Random01(tick_seed + static_cast<uint32_t>(i) * 29u) * 0.06f;
          const float vx = -0.018f + _Random01(tick_seed + static_cast<uint32_t>(i) * 31u) * 0.036f;
          const float vy = 0.050f + _Random01(tick_seed + static_cast<uint32_t>(i) * 47u) * 0.030f;
          const float life = 100.0f + _Random01(tick_seed + static_cast<uint32_t>(i) * 59u) * 60.0f;
          std::memcpy(shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, &x, sizeof(x));
          std::memcpy(shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, &y, sizeof(y));
          std::memcpy(shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, &vx, sizeof(vx));
          std::memcpy(shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, &vy, sizeof(vy));
          const float age = 0.0f;
          std::memcpy(shell_age->bytes.data() + i * shell_age->element_stride, &age, sizeof(age));
          std::memcpy(shell_life->bytes.data() + i * shell_life->element_stride, &life, sizeof(life));
          state = 1.0f;
          std::memcpy(shell_state->bytes.data() + i * shell_state->element_stride, &state, sizeof(state));
        }
      }
    } else if (algorithm_name_ == "v3a16_fireworks_pipeline_demo_stage1") {
      if (shell_state && shell_pos_x && shell_pos_y && shell_vel_x && shell_vel_y && shell_age && shell_life) {
        const size_t shell_count = _ArrayCount(shell_state);
        for (size_t i = 0; i < shell_count; ++i) {
          float state = 0.0f;
          std::memcpy(&state, shell_state->bytes.data() + i * shell_state->element_stride, sizeof(state));
          if (state != 1.0f) {
            continue;
          }
          float x = 0.0f;
          float y = 0.0f;
          float vx = 0.0f;
          float vy = 0.0f;
          float age = 0.0f;
          float life = 0.0f;
          std::memcpy(&x, shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, sizeof(x));
          std::memcpy(&y, shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, sizeof(y));
          std::memcpy(&vx, shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, sizeof(vx));
          std::memcpy(&vy, shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, sizeof(vy));
          std::memcpy(&age, shell_age->bytes.data() + i * shell_age->element_stride, sizeof(age));
          std::memcpy(&life, shell_life->bytes.data() + i * shell_life->element_stride, sizeof(life));
          float seed = 0.0f;
          std::memcpy(&seed, shell_seed->bytes.data() + i * shell_seed->element_stride, sizeof(seed));
          const uint32_t seed_bits = static_cast<uint32_t>(seed * 65536.0f);
          x += vx;
          y += vy;
          vx *= 0.9985f;
          vy -= 0.0025f;
          age += 1.0f;
          const float explode_height = 0.42f + _Random01(seed_bits + static_cast<uint32_t>(i) * 7u + 13u) * 0.38f;
          const float explode_bias = _Random01(seed_bits + tick_seed + static_cast<uint32_t>(i) * 3u + 19u);
          if (y > explode_height || age >= life || explode_bias > 0.90f) {
            state = 2.0f;
          }
          std::memcpy(shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, &x, sizeof(x));
          std::memcpy(shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, &y, sizeof(y));
          std::memcpy(shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, &vx, sizeof(vx));
          std::memcpy(shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, &vy, sizeof(vy));
          std::memcpy(shell_age->bytes.data() + i * shell_age->element_stride, &age, sizeof(age));
          std::memcpy(shell_state->bytes.data() + i * shell_state->element_stride, &state, sizeof(state));
        }
      }
      _WriteScalar(v1, tick_value);
      _WriteScalar(v2, launch_budget_value);
      _WriteScalar(v3, live_spark_value);
    } else if (algorithm_name_ == "v3a16_fireworks_pipeline_demo_stage2") {
      if (shell_state && shell_pos_x && shell_pos_y && spark_state && spark_pos_x && spark_pos_y &&
          spark_vel_x && spark_vel_y && spark_age && spark_life && spark_seed) {
        const size_t shell_count = _ArrayCount(shell_state);
        const size_t spark_count = _ArrayCount(spark_state);
        for (size_t i = 0; i < shell_count; ++i) {
          float state = 0.0f;
          std::memcpy(&state, shell_state->bytes.data() + i * shell_state->element_stride, sizeof(state));
          if (state != 2.0f) {
            continue;
          }
          float x = 0.0f;
          float y = 0.0f;
          std::memcpy(&x, shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, sizeof(x));
          std::memcpy(&y, shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, sizeof(y));
          float shell_seed_value = 0.0f;
          std::memcpy(&shell_seed_value, shell_seed->bytes.data() + i * shell_seed->element_stride, sizeof(shell_seed_value));
          const uint32_t shell_seed_bits = static_cast<uint32_t>(shell_seed_value * 65536.0f);
          const size_t spark_total = 8u + static_cast<size_t>(
            _Random01(shell_seed_bits + static_cast<uint32_t>(i) * 11u + 11u) * 10.0f);
          const size_t spark_base = (i * 19u) % spark_count;
          for (size_t j = 0; j < spark_total; ++j) {
            const size_t spark_index = (spark_base + j) % spark_count;
            const float angle = 6.2831853f * _Random01(tick_seed + static_cast<uint32_t>(i) * 43u + static_cast<uint32_t>(j) * 7u);
            const float radius = 0.008f + _Random01(tick_seed + static_cast<uint32_t>(i) * 61u + static_cast<uint32_t>(j) * 11u) * 0.038f;
            const float vx = std::cos(angle) * (0.010f + _Random01(tick_seed + static_cast<uint32_t>(i) * 71u + static_cast<uint32_t>(j) * 13u) * 0.028f);
            const float vy = std::sin(angle) * (0.010f + _Random01(tick_seed + static_cast<uint32_t>(i) * 83u + static_cast<uint32_t>(j) * 17u) * 0.028f);
            const float life = 42.0f + _Random01(tick_seed + static_cast<uint32_t>(i) * 97u + static_cast<uint32_t>(j) * 19u) * 64.0f;
            const float spark_x = x + std::cos(angle) * radius;
            const float spark_y = y + std::sin(angle) * radius;
            const float age = 0.0f;
            const float seed = static_cast<float>(spark_index);
            std::memcpy(spark_pos_x->bytes.data() + spark_index * spark_pos_x->element_stride, &spark_x, sizeof(spark_x));
            std::memcpy(spark_pos_y->bytes.data() + spark_index * spark_pos_y->element_stride, &spark_y, sizeof(spark_y));
            std::memcpy(spark_vel_x->bytes.data() + spark_index * spark_vel_x->element_stride, &vx, sizeof(vx));
            std::memcpy(spark_vel_y->bytes.data() + spark_index * spark_vel_y->element_stride, &vy, sizeof(vy));
            std::memcpy(spark_age->bytes.data() + spark_index * spark_age->element_stride, &age, sizeof(age));
            std::memcpy(spark_life->bytes.data() + spark_index * spark_life->element_stride, &life, sizeof(life));
            std::memcpy(spark_seed->bytes.data() + spark_index * spark_seed->element_stride, &seed, sizeof(seed));
            const float spark_state_value = 1.0f;
            std::memcpy(spark_state->bytes.data() + spark_index * spark_state->element_stride, &spark_state_value, sizeof(spark_state_value));
          }
          const float shell_state_value = 3.0f;
          std::memcpy(shell_state->bytes.data() + i * shell_state->element_stride, &shell_state_value, sizeof(shell_state_value));
        }
      }
      _WriteScalar(v1, tick_value);
      _WriteScalar(v2, launch_budget_value);
      _WriteScalar(v3, live_spark_value);
    } else if (algorithm_name_ == "v3a16_fireworks_pipeline_demo_stage3" ||
               algorithm_name_ == "v3a16_fireworks_pipeline_demo_stage4") {
      _WriteScalar(v1, tick_value);
      _WriteScalar(v2, launch_budget_value);
      _WriteScalar(v3, live_spark_value);
    } else if (algorithm_name_ == "v3a16_fireworks_pipeline_demo_stage5") {
      convert_shell_coordinates(false);
      convert_spark_coordinates(false);
      if (shell_state && shell_pos_x && shell_pos_y && shell_vel_x && shell_vel_y && shell_age && shell_life && shell_seed) {
        const size_t shell_count = _ArrayCount(shell_state);
        for (size_t i = 0u; i < shell_count; ++i) {
          float state = 0.0f;
          std::memcpy(&state, shell_state->bytes.data() + i * shell_state->element_stride, sizeof(state));
          if (state <= 0.5f) {
            continue;
          }
          float x = 0.0f;
          float y = 0.0f;
          float vx = 0.0f;
          float vy = 0.0f;
          float life = 0.0f;
          std::memcpy(&x, shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, sizeof(x));
          std::memcpy(&y, shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, sizeof(y));
          std::memcpy(&vx, shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, sizeof(vx));
          std::memcpy(&vy, shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, sizeof(vy));
          std::memcpy(&life, shell_life->bytes.data() + i * shell_life->element_stride, sizeof(life));
          if (life <= 0.01f) {
            const float zero = 0.0f;
            std::memcpy(shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, &zero, sizeof(zero));
            std::memcpy(shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, &zero, sizeof(zero));
            std::memcpy(shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, &zero, sizeof(zero));
            std::memcpy(shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, &zero, sizeof(zero));
            std::memcpy(shell_state->bytes.data() + i * shell_state->element_stride, &zero, sizeof(zero));
            std::memcpy(shell_age->bytes.data() + i * shell_age->element_stride, &zero, sizeof(zero));
            std::memcpy(shell_life->bytes.data() + i * shell_life->element_stride, &zero, sizeof(zero));
            std::memcpy(shell_seed->bytes.data() + i * shell_seed->element_stride, &zero, sizeof(zero));
          } else {
            std::memcpy(shell_pos_x->bytes.data() + i * shell_pos_x->element_stride, &x, sizeof(x));
            std::memcpy(shell_pos_y->bytes.data() + i * shell_pos_y->element_stride, &y, sizeof(y));
            std::memcpy(shell_vel_x->bytes.data() + i * shell_vel_x->element_stride, &vx, sizeof(vx));
            std::memcpy(shell_vel_y->bytes.data() + i * shell_vel_y->element_stride, &vy, sizeof(vy));
            std::memcpy(shell_state->bytes.data() + i * shell_state->element_stride, &state, sizeof(state));
            std::memcpy(shell_life->bytes.data() + i * shell_life->element_stride, &life, sizeof(life));
          }
        }
      }
      if (spark_state && spark_pos_x && spark_pos_y && spark_vel_x && spark_vel_y && spark_age && spark_life && spark_seed) {
        const size_t spark_count = _ArrayCount(spark_state);
        for (size_t i = 0u; i < spark_count; ++i) {
          float state = 0.0f;
          std::memcpy(&state, spark_state->bytes.data() + i * spark_state->element_stride, sizeof(state));
          if (state <= 0.5f) {
            continue;
          }
          float x = 0.0f;
          float y = 0.0f;
          float vx = 0.0f;
          float vy = 0.0f;
          float life = 0.0f;
          std::memcpy(&x, spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, sizeof(x));
          std::memcpy(&y, spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, sizeof(y));
          std::memcpy(&vx, spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, sizeof(vx));
          std::memcpy(&vy, spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, sizeof(vy));
          std::memcpy(&life, spark_life->bytes.data() + i * spark_life->element_stride, sizeof(life));
          if (life <= 0.01f) {
            const float zero = 0.0f;
            std::memcpy(spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, &zero, sizeof(zero));
            std::memcpy(spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, &zero, sizeof(zero));
            std::memcpy(spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, &zero, sizeof(zero));
            std::memcpy(spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, &zero, sizeof(zero));
            std::memcpy(spark_state->bytes.data() + i * spark_state->element_stride, &zero, sizeof(zero));
            std::memcpy(spark_age->bytes.data() + i * spark_age->element_stride, &zero, sizeof(zero));
            std::memcpy(spark_life->bytes.data() + i * spark_life->element_stride, &zero, sizeof(zero));
            std::memcpy(spark_seed->bytes.data() + i * spark_seed->element_stride, &zero, sizeof(zero));
          } else {
            std::memcpy(spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, &x, sizeof(x));
            std::memcpy(spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, &y, sizeof(y));
            std::memcpy(spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, &vx, sizeof(vx));
            std::memcpy(spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, &vy, sizeof(vy));
            std::memcpy(spark_state->bytes.data() + i * spark_state->element_stride, &state, sizeof(state));
            std::memcpy(spark_life->bytes.data() + i * spark_life->element_stride, &life, sizeof(life));
          }
        }
      }
      _WriteScalar(v1, tick_value);
      _WriteScalar(v2, launch_budget_value);
      _WriteScalar(v3, live_spark_value);
    } else if (algorithm_name_ == "v3a16_fireworks_pipeline_demo_stage6") {
      if (spark_state && spark_pos_x && spark_pos_y && spark_vel_x && spark_vel_y && spark_age && spark_life) {
        const size_t spark_count = _ArrayCount(spark_state);
        size_t live_count = 0u;
        for (size_t i = 0; i < spark_count; ++i) {
          float state = 0.0f;
          std::memcpy(&state, spark_state->bytes.data() + i * spark_state->element_stride, sizeof(state));
          if (state != 1.0f) {
            continue;
          }
          float x = 0.0f;
          float y = 0.0f;
          float vx = 0.0f;
          float vy = 0.0f;
          float age = 0.0f;
          float life = 0.0f;
          std::memcpy(&x, spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, sizeof(x));
          std::memcpy(&y, spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, sizeof(y));
          std::memcpy(&vx, spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, sizeof(vx));
          std::memcpy(&vy, spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, sizeof(vy));
          std::memcpy(&age, spark_age->bytes.data() + i * spark_age->element_stride, sizeof(age));
          std::memcpy(&life, spark_life->bytes.data() + i * spark_life->element_stride, sizeof(life));
          x += vx;
          y += vy;
          vx *= 0.985f;
          vy *= 0.985f;
          age += 1.0f;
          life -= 1.0f;
          if (life <= 0.01f || (std::fabs(vx) + std::fabs(vy)) <= 0.01f) {
            state = 0.0f;
          } else {
            ++live_count;
          }
          std::memcpy(spark_pos_x->bytes.data() + i * spark_pos_x->element_stride, &x, sizeof(x));
          std::memcpy(spark_pos_y->bytes.data() + i * spark_pos_y->element_stride, &y, sizeof(y));
          std::memcpy(spark_vel_x->bytes.data() + i * spark_vel_x->element_stride, &vx, sizeof(vx));
          std::memcpy(spark_vel_y->bytes.data() + i * spark_vel_y->element_stride, &vy, sizeof(vy));
          std::memcpy(spark_age->bytes.data() + i * spark_age->element_stride, &age, sizeof(age));
          std::memcpy(spark_life->bytes.data() + i * spark_life->element_stride, &life, sizeof(life));
          std::memcpy(spark_state->bytes.data() + i * spark_state->element_stride, &state, sizeof(state));
        }
        _WriteScalar(v3, static_cast<float>(live_count));
      }
      _WriteScalar(v1, tick_value);
      _WriteScalar(v2, launch_budget_value);
    }

    return true;
  }

 private:
  std::string algorithm_name_;
};

void DestroyCpuExecutor(agent::IAlgorithmCpuExecutor* executor) {
  delete executor;
}

}  // namespace

inline bool CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle) {
  out_bundle->Clear();
  out_bundle->cpu_symbol = true;
  out_bundle->gpu_symbol = true;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->cpu_executor = new FireworksCpuExecutor(request && request->algorithm_name ? request->algorithm_name : "");
  out_bundle->destroy_cpu_executor = &DestroyCpuExecutor;
  return true;
}

inline bool CreateRuntimeReflector(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algorithmManager::support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}

}  // namespace v3a16_fireworks_pipeline_demo
