#include "common_data/kernel_cfg.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace common_data {
namespace {

std::uint32_t _ReadDefaultAgentLimitFpsFlag() {
  const std::filesystem::path path = std::filesystem::path(PROJECT_ROOT_DIR) / "config" / "kernelCfg.json";
  std::ifstream file(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::string key = "\"limit_fps_flag\"";
  const size_t key_pos = text.find(key);
  if (key_pos == std::string::npos) {
    throw std::runtime_error("Missing limit_fps_flag in config/kernelCfg.json");
  }

  const size_t value_pos = text.find_first_of("0123456789", key_pos + key.size());
  if (value_pos == std::string::npos) {
    throw std::runtime_error("Missing limit_fps_flag value in config/kernelCfg.json");
  }

  const size_t value_end = text.find_first_not_of("0123456789", value_pos);
  return static_cast<std::uint32_t>(std::stoul(text.substr(value_pos, value_end - value_pos)));
}

std::uint32_t _ReadDefaultPipelineMaxConcurrentStage0Submissions() {
  const std::filesystem::path path = std::filesystem::path(PROJECT_ROOT_DIR) / "config" / "kernelCfg.json";
  std::ifstream file(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::string key = "\"max_concurrent_stage0_submissions\"";
  const size_t key_pos = text.find(key);
  if (key_pos == std::string::npos) {
    throw std::runtime_error("Missing max_concurrent_stage0_submissions in config/kernelCfg.json");
  }

  const size_t value_pos = text.find_first_of("0123456789", key_pos + key.size());
  if (value_pos == std::string::npos) {
    throw std::runtime_error("Missing max_concurrent_stage0_submissions value in config/kernelCfg.json");
  }

  const size_t value_end = text.find_first_not_of("0123456789", value_pos);
  return static_cast<std::uint32_t>(std::stoul(text.substr(value_pos, value_end - value_pos)));
}

}  // namespace

std::uint32_t DefaultAgentLimitFpsFlag() {
  static const std::uint32_t value = _ReadDefaultAgentLimitFpsFlag();
  return value;
}

std::uint32_t DefaultPipelineMaxConcurrentStage0Submissions() {
  static const std::uint32_t value = _ReadDefaultPipelineMaxConcurrentStage0Submissions();
  return value;
}

}  // namespace common_data
