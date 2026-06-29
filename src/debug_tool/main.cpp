#include "debug_tool/debug_tool_backend_runtime.h"
#include "debug_tool/debug_tool_frontend_panel.h"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct PipelineRunnerOptions {
  bool enabled{false};
  std::string algorithm_name{"v2a0_pipeline_square_vertex_demo"};
  std::string pipeline_name{};
  uint32_t ticks{24u};
  uint32_t preview_width{640u};
  uint32_t preview_height{480u};
  std::string preview_output_path{
    "D:/gptsandbox/artifacts/pipeline_runner/render_preview.ppm"};
  debug_tool::AlgorithmExecutionPreference execution_preference{
    debug_tool::AlgorithmExecutionPreference::Gpu};
};

struct PositionSample {
  bool valid{false};
  float x{0.0f};
  float y{0.0f};
};

const char* _AssemblyStateName(debug_tool::AlgorithmAssemblyState state) {
  switch (state) {
    case debug_tool::AlgorithmAssemblyState::Pending: return "pending";
    case debug_tool::AlgorithmAssemblyState::Assembling: return "assembling";
    case debug_tool::AlgorithmAssemblyState::Ready: return "ready";
    case debug_tool::AlgorithmAssemblyState::Failed: return "failed";
  }
  return "unknown";
}

const char* _ExecutionPreferenceName(debug_tool::AlgorithmExecutionPreference preference) {
  switch (preference) {
    case debug_tool::AlgorithmExecutionPreference::Cpu: return "cpu";
    case debug_tool::AlgorithmExecutionPreference::Gpu: return "gpu";
  }
  return "unknown";
}

bool _ReadFloatBytes(const std::vector<std::byte>& bytes, float* out_value) {
  if (!out_value || bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(out_value, bytes.data(), sizeof(float));
  return true;
}

std::optional<float> _FindScalarValue(
  const debug_tool::AlgorithmReflectionSnapshot& snapshot,
  const char* container_name) {
  if (!container_name) {
    return std::nullopt;
  }

  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variables) {
    if (value.container_name != container_name) {
      continue;
    }
    float scalar = 0.0f;
    if (_ReadFloatBytes(value.bytes, &scalar)) {
      return scalar;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

const debug_tool::AlgorithmReflectionValue* _FindReflectionValue(
  const debug_tool::AlgorithmReflectionSnapshot& snapshot,
  const char* container_name) {
  if (!container_name) {
    return nullptr;
  }
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variables) {
    if (value.container_name == container_name) {
      return &value;
    }
  }
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    if (value.container_name == container_name) {
      return &value;
    }
  }
  return nullptr;
}

std::optional<float> _ReadArrayFloatValue(
  const debug_tool::AlgorithmReflectionSnapshot& snapshot,
  const char* container_name,
  size_t index) {
  const debug_tool::AlgorithmReflectionValue* value = _FindReflectionValue(snapshot, container_name);
  if (!value || value->bytes.size() < (index + 1u) * sizeof(float)) {
    return std::nullopt;
  }
  float scalar = 0.0f;
  std::memcpy(&scalar, value->bytes.data() + index * sizeof(float), sizeof(float));
  return scalar;
}

PositionSample _ExtractPositionSample(const debug_tool::AlgorithmReflectionSnapshot& snapshot) {
  const std::optional<float> x = _FindScalarValue(snapshot, "position_x");
  const std::optional<float> y = _FindScalarValue(snapshot, "position_y");
  if (!x.has_value() || !y.has_value()) {
    return {};
  }
  return PositionSample{
    .valid = true,
    .x = *x,
    .y = *y,
  };
}

std::string _BuildRunnerPipelineName(const PipelineRunnerOptions& options) {
  if (!options.pipeline_name.empty()) {
    return options.pipeline_name;
  }
  return options.algorithm_name + "::runner_mount";
}

std::string _BuildRunnerSubmissionName(const PipelineRunnerOptions& options) {
  return _BuildRunnerPipelineName(options) + "::testsubmit_0";
}

bool _ParseUInt32(const char* text, uint32_t* out_value) {
  if (!text || !*text || !out_value) {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out_value = static_cast<uint32_t>(parsed);
  return true;
}

bool _ParseExecutionPreference(
  const char* text,
  debug_tool::AlgorithmExecutionPreference* out_preference) {
  if (!text || !out_preference) {
    return false;
  }
  const std::string value(text);
  if (value == "cpu") {
    *out_preference = debug_tool::AlgorithmExecutionPreference::Cpu;
    return true;
  }
  if (value == "gpu") {
    *out_preference = debug_tool::AlgorithmExecutionPreference::Gpu;
    return true;
  }
  return false;
}

bool _WritePpmImage(
  const std::filesystem::path& output_path,
  const std::vector<std::byte>& rgba_bytes,
  uint32_t width,
  uint32_t height) {
  if (width == 0u || height == 0u) {
    return false;
  }

  const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
  const size_t required_size = pixel_count * 4u;
  if (rgba_bytes.size() < required_size) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(output_path.parent_path(), ec);
  if (ec) {
    return false;
  }

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output << "P6\n" << width << ' ' << height << "\n255\n";
  for (uint32_t y = 0u; y < height; ++y) {
    const uint32_t source_y = height - 1u - y;
    for (uint32_t x = 0u; x < width; ++x) {
      const size_t pixel_index =
        (static_cast<size_t>(source_y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
      const char rgb[3]{
        static_cast<char>(rgba_bytes[pixel_index + 0u]),
        static_cast<char>(rgba_bytes[pixel_index + 1u]),
        static_cast<char>(rgba_bytes[pixel_index + 2u]),
      };
      output.write(rgb, sizeof(rgb));
      if (!output) {
        return false;
      }
    }
  }

  return true;
}

bool _ParsePipelineRunnerOptions(
  int argc,
  char** argv,
  PipelineRunnerOptions* out_options,
  std::string* out_error_message) {
  if (!out_options) {
    if (out_error_message) {
      *out_error_message = "Pipeline runner option output pointer is null.";
    }
    return false;
  }

  PipelineRunnerOptions options{};
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i] ? argv[i] : "";
    if (argument == "--pipeline-runner") {
      options.enabled = true;
      continue;
    }
    if (argument == "--algorithm") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--algorithm requires a non-empty value.";
        }
        return false;
      }
      options.algorithm_name = argv[++i];
      continue;
    }
    if (argument == "--pipeline-name") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--pipeline-name requires a non-empty value.";
        }
        return false;
      }
      options.pipeline_name = argv[++i];
      continue;
    }
    if (argument == "--ticks") {
      if (i + 1 >= argc || !_ParseUInt32(argv[i + 1], &options.ticks) || options.ticks == 0u) {
        if (out_error_message) {
          *out_error_message = "--ticks requires a positive integer value.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--preview-width") {
      if (i + 1 >= argc || !_ParseUInt32(argv[i + 1], &options.preview_width) || options.preview_width == 0u) {
        if (out_error_message) {
          *out_error_message = "--preview-width requires a positive integer value.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--preview-height") {
      if (i + 1 >= argc || !_ParseUInt32(argv[i + 1], &options.preview_height) || options.preview_height == 0u) {
        if (out_error_message) {
          *out_error_message = "--preview-height requires a positive integer value.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--preview-output") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--preview-output requires a non-empty value.";
        }
        return false;
      }
      options.preview_output_path = argv[++i];
      continue;
    }
    if (argument == "--execution") {
      if (i + 1 >= argc || !_ParseExecutionPreference(argv[i + 1], &options.execution_preference)) {
        if (out_error_message) {
          *out_error_message = "--execution requires 'cpu' or 'gpu'.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--help" || argument == "-h") {
      std::cout
        << "Usage:\n"
        << "  debugTool.exe --pipeline-runner "
        << "[--algorithm <name>] [--pipeline-name <name>] [--ticks <count>] "
        << "[--preview-width <px>] [--preview-height <px>] [--preview-output <path>] "
        << "[--execution cpu|gpu]\n";
      return false;
    }
  }

  *out_options = std::move(options);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

void _PrintReflectionSnapshot(const debug_tool::AlgorithmReflectionSnapshot& snapshot) {
  std::cout << "    reflection.valid=" << (snapshot.valid ? "true" : "false") << '\n';
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variables) {
    std::cout << "      var " << value.container_name;
    float scalar = 0.0f;
    if (_ReadFloatBytes(value.bytes, &scalar)) {
      std::cout << '=' << scalar;
    } else {
      std::cout << " bytes=" << value.bytes.size();
    }
    std::cout << '\n';
  }
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    std::cout << "      array " << value.container_name << " bytes=" << value.bytes.size() << '\n';
  }
  for (size_t index = 0u; index < 8u; ++index) {
    const std::optional<float> spark_state = _ReadArrayFloatValue(snapshot, "spark_state", index);
    if (!spark_state.has_value() || *spark_state <= 0.5f) {
      continue;
    }
    const std::optional<float> spark_x = _ReadArrayFloatValue(snapshot, "spark_pos_x", index);
    const std::optional<float> spark_y = _ReadArrayFloatValue(snapshot, "spark_pos_y", index);
    const std::optional<float> spark_life = _ReadArrayFloatValue(snapshot, "spark_life", index);
    if (spark_x.has_value() && spark_y.has_value()) {
      std::cout
        << "      sample.spark[" << index << "] state=" << *spark_state
        << " pos=(" << *spark_x << ", " << *spark_y << ')';
      if (spark_life.has_value()) {
        std::cout << " life=" << *spark_life;
      }
      std::cout << '\n';
    }
    break;
  }
}

void _PrintBridgeDebugSummary(const debug_tool::PipelineStageBridgeDebugSummary& bridge_summary) {
  if (!bridge_summary.valid) {
    std::cout << "    bridge.valid=false\n";
    return;
  }

  std::cout
    << "    bridge.valid=true prev=" << bridge_summary.previous_stage_name
    << " next=" << bridge_summary.next_stage_name << '\n';
  std::cout << "      ingress_bindings=" << bridge_summary.ingress_bindings.size() << '\n';
  for (const debug_tool::PipelineStageBridgeDebugBinding& binding : bridge_summary.ingress_bindings) {
    std::cout
      << "        " << binding.source_stage_name << ':' << binding.source_container_name
      << " -> " << binding.target_stage_name << ':' << binding.target_container_name << '\n';
  }
  std::cout << "      egress_bindings=" << bridge_summary.egress_bindings.size() << '\n';
  for (const debug_tool::PipelineStageBridgeDebugBinding& binding : bridge_summary.egress_bindings) {
    std::cout
      << "        " << binding.source_stage_name << ':' << binding.source_container_name
      << " -> " << binding.target_stage_name << ':' << binding.target_container_name << '\n';
  }
  if (bridge_summary.has_stage_output_reflection_snapshot) {
    for (size_t index = 0u; index < 8u; ++index) {
      const std::optional<float> spark_state =
        _ReadArrayFloatValue(bridge_summary.stage_output_reflection_snapshot, "spark_state", index);
      if (!spark_state.has_value() || *spark_state <= 0.5f) {
        continue;
      }
      const std::optional<float> spark_x =
        _ReadArrayFloatValue(bridge_summary.stage_output_reflection_snapshot, "spark_pos_x", index);
      const std::optional<float> spark_y =
        _ReadArrayFloatValue(bridge_summary.stage_output_reflection_snapshot, "spark_pos_y", index);
      if (spark_x.has_value() && spark_y.has_value()) {
        std::cout
          << "      bridge.sample.spark[" << index << "] stage_out=("
          << *spark_x << ", " << *spark_y << ")\n";
      }
      break;
    }
  }
}

bool _RunPipelineRunner(const PipelineRunnerOptions& options) {
  const std::filesystem::path log_directory =
    std::filesystem::path("D:/gptsandbox/artifacts/pipeline_runner");
  std::error_code ec;
  std::filesystem::create_directories(log_directory, ec);
  if (ec) {
    throw std::runtime_error(
      "Failed to create pipeline runner log directory: " + log_directory.string());
  }
  const std::filesystem::path package_loader_probe_path = log_directory / "package_loader_probe.log";
  const std::filesystem::path backend_attach_probe_path = log_directory / "backend_attach_probe.log";
  const std::filesystem::path agent_mount_probe_path = log_directory / "agent_mount_probe.log";
  const std::filesystem::path progress_path = log_directory / "progress_probe.log";
  const std::filesystem::path log_path = log_directory / "last_run.log";
  const std::filesystem::path preview_output_path(options.preview_output_path);
  std::filesystem::remove(package_loader_probe_path, ec);
  ec.clear();
  std::filesystem::remove(backend_attach_probe_path, ec);
  ec.clear();
  std::filesystem::remove(agent_mount_probe_path, ec);
  ec.clear();
  std::filesystem::remove(progress_path, ec);
  ec.clear();
  std::filesystem::remove(log_path, ec);
  ec.clear();
  std::filesystem::remove(preview_output_path, ec);
  ec.clear();
  const auto append_progress = [&](const std::string& line) {
    std::ofstream progress_file(progress_path, std::ios::binary | std::ios::app);
    if (progress_file) {
      progress_file << line << '\n';
    }
  };
  append_progress("runner.begin");
  std::ofstream log_file(log_path, std::ios::binary | std::ios::trunc);
  if (!log_file) {
    throw std::runtime_error("Failed to open pipeline runner log file: " + log_path.string());
  }
  std::streambuf* const original_cout_buffer = std::cout.rdbuf(log_file.rdbuf());

  DebugToolBackendRuntime runtime;
  append_progress("runtime.created");
  if (!runtime.Init("debugToolRunner", 1280, 720)) {
    throw std::runtime_error("DebugToolBackendRuntime init failed in pipeline runner mode.");
  }
  append_progress("runtime.initialized");

  runtime.runtime_environment().SetDrawCallback([]() {});
  append_progress("draw_callback_set");

  bool is_pipeline = false;
  std::string error_message;
  if (!runtime.IsPipelineAlgorithm(options.algorithm_name, &is_pipeline, &error_message)) {
    throw std::runtime_error(
      error_message.empty()
        ? ("Failed to query algorithm type for '" + options.algorithm_name + "'.")
        : error_message);
  }
  append_progress("algorithm_type_checked");
  if (!is_pipeline) {
    throw std::runtime_error(
      "Pipeline runner requires a pipeline algorithm, but '" + options.algorithm_name + "' is not a pipeline.");
  }

  std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings;
  std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values;
  bool has_default_file = false;
  if (!runtime.LoadAlgorithmPackageDefaultBindings(
        options.algorithm_name,
        &resource_bindings,
        &descriptor_values,
        &has_default_file,
        &error_message)) {
    throw std::runtime_error(
      error_message.empty()
        ? ("Failed to load default bindings for '" + options.algorithm_name + "'.")
        : error_message);
  }
  append_progress("default_bindings_loaded");

  const std::string mounted_pipeline_name = _BuildRunnerPipelineName(options);
  const std::string submission_name = _BuildRunnerSubmissionName(options);

  size_t mounted_pipeline_index = 0u;
  if (!runtime.AttachPipelinePackageToAgent(
        0u,
        mounted_pipeline_name,
        options.algorithm_name,
        resource_bindings,
        descriptor_values,
        &mounted_pipeline_index,
        &error_message,
        options.execution_preference)) {
    throw std::runtime_error(
      error_message.empty()
        ? ("Failed to mount pipeline '" + mounted_pipeline_name + "'.")
        : error_message);
  }
  append_progress("pipeline_mounted");

  if (!runtime.AttachPipelinePackageToAgent(
        0u,
        submission_name,
        options.algorithm_name,
        resource_bindings,
        descriptor_values,
        nullptr,
        &error_message,
        options.execution_preference)) {
    throw std::runtime_error(
      error_message.empty()
        ? ("Failed to submit pipeline resource batch '" + submission_name + "'.")
        : error_message);
  }
  append_progress("resource_batch_submitted");

  runtime.StartTicking();
  append_progress("ticking_started");
  runtime.SetRenderPreviewExtent(
    ImVec2(static_cast<float>(options.preview_width), static_cast<float>(options.preview_height)));
  append_progress("preview_extent_set");

  std::cout
    << "pipeline_runner.begin algorithm=" << options.algorithm_name
    << " pipeline=" << mounted_pipeline_name
    << " execution=" << _ExecutionPreferenceName(options.execution_preference)
    << " ticks=" << options.ticks
    << " preview=" << options.preview_width << 'x' << options.preview_height
    << " defaults=" << (has_default_file ? "true" : "false") << '\n';

  std::optional<PositionSample> first_valid_position{};
  PositionSample last_valid_position{};
  bool observed_motion = false;

  for (uint32_t tick_index = 0u; tick_index < options.ticks; ++tick_index) {
    append_progress("tick_loop_begin_" + std::to_string(tick_index + 1u));
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    if (!runtime.Tick()) {
      throw std::runtime_error(
        runtime.ui_status_message().empty()
          ? "Pipeline runner tick failed."
          : runtime.ui_status_message());
    }
    append_progress("tick_complete_" + std::to_string(tick_index + 1u));

    debug_tool::AgentRuntimeSummary agent_summary{};
    if (!runtime.GetAgentSummary(0u, &agent_summary)) {
      throw std::runtime_error("Failed to collect agent summary after pipeline tick.");
    }

    std::vector<debug_tool::AlgorithmRuntimeSummary> pipeline_stages;
    for (const debug_tool::AlgorithmRuntimeSummary& summary : agent_summary.algorithms) {
      if (summary.pipeline_name == mounted_pipeline_name && summary.pipeline_stage) {
        pipeline_stages.push_back(summary);
      }
    }
    std::sort(
      pipeline_stages.begin(),
      pipeline_stages.end(),
      [](const debug_tool::AlgorithmRuntimeSummary& lhs, const debug_tool::AlgorithmRuntimeSummary& rhs) {
        return lhs.pipeline_stage_index < rhs.pipeline_stage_index;
      });

    if (pipeline_stages.empty()) {
      throw std::runtime_error("Mounted pipeline stages disappeared during runner execution.");
    }

    std::cout << "[tick " << (tick_index + 1u) << "] stage_count=" << pipeline_stages.size() << '\n';
    for (const debug_tool::AlgorithmRuntimeSummary& summary : pipeline_stages) {
      std::cout
        << "  stage[" << summary.pipeline_stage_index << "] "
        << summary.algorithm_name
        << " state=" << _AssemblyStateName(summary.assembly_state)
        << " exec=" << _ExecutionPreferenceName(summary.execution_preference)
        << " cpu_symbol=" << (summary.cpu_symbol ? "true" : "false")
        << " gpu_symbol=" << (summary.gpu_symbol ? "true" : "false");
      if (summary.pipeline_active_stage_index_valid) {
        std::cout << " active_stage=" << summary.pipeline_active_stage_index;
      }
      std::cout << '\n';

      if (summary.pipeline_stage_index == 0u) {
        _PrintReflectionSnapshot(summary.reflection_snapshot);
        const PositionSample sample = _ExtractPositionSample(summary.reflection_snapshot);
        if (sample.valid) {
          std::cout << "    position=(" << sample.x << ", " << sample.y << ")\n";
          if (!first_valid_position.has_value()) {
            first_valid_position = sample;
          } else if (std::fabs(sample.x - first_valid_position->x) > 1.0e-4f ||
                     std::fabs(sample.y - first_valid_position->y) > 1.0e-4f) {
            observed_motion = true;
          }
          last_valid_position = sample;
        }
      }
      _PrintBridgeDebugSummary(summary.bridge_debug_set);
    }
  }

  runtime.PauseTicking();
  append_progress("ticking_paused");

  runtime_systems::RenderPreviewRequest preview_request{};
  if (!runtime.BuildRenderPreviewRequest(0u, mounted_pipeline_index, &preview_request, &error_message)) {
    throw std::runtime_error(
      error_message.empty()
        ? "Failed to build render preview request for mounted pipeline."
        : error_message);
  }
  if (!preview_request.valid) {
    throw std::runtime_error("Render preview request is invalid after pipeline execution.");
  }
  runtime.SetRenderPreviewRequest(std::move(preview_request));
  append_progress("preview_request_set");

  if (!runtime.runtime_environment().Tick()) {
    throw std::runtime_error("Runtime environment failed while rendering the preview frame.");
  }
  append_progress("preview_frame_rendered");

  if (!runtime.has_render_preview_texture()) {
    throw std::runtime_error(
      "Render preview texture was not created. summary=" + runtime.render_preview_debug_summary());
  }

  std::vector<std::byte> preview_rgba{};
  ImVec2 preview_size{};
  if (!runtime.runtime_environment().ReadbackRenderPreviewTexture(&preview_rgba, &preview_size)) {
    throw std::runtime_error(
      "Failed to read back render preview texture. summary=" + runtime.render_preview_debug_summary());
  }
  append_progress("preview_readback_complete");

  const uint32_t preview_width = static_cast<uint32_t>(preview_size.x);
  const uint32_t preview_height = static_cast<uint32_t>(preview_size.y);
  if (preview_width == 0u || preview_height == 0u) {
    throw std::runtime_error("Render preview readback returned an empty extent.");
  }
  const size_t preview_pixel_count =
    static_cast<size_t>(preview_width) * static_cast<size_t>(preview_height);
  if (preview_rgba.size() < preview_pixel_count * 4u) {
    throw std::runtime_error("Render preview readback returned fewer bytes than expected.");
  }

  size_t non_empty_pixel_count = 0u;
  for (size_t pixel_index = 0u; pixel_index < preview_pixel_count; ++pixel_index) {
    const size_t byte_index = pixel_index * 4u;
    if (preview_rgba[byte_index + 0u] != std::byte{0} ||
        preview_rgba[byte_index + 1u] != std::byte{0} ||
        preview_rgba[byte_index + 2u] != std::byte{0} ||
        preview_rgba[byte_index + 3u] != std::byte{0}) {
      ++non_empty_pixel_count;
    }
  }
  if (non_empty_pixel_count == 0u) {
    throw std::runtime_error(
      "Render preview frame is empty. summary=" + runtime.render_preview_debug_summary());
  }
  if (!_WritePpmImage(preview_output_path, preview_rgba, preview_width, preview_height)) {
    throw std::runtime_error(
      "Failed to write render preview image: " + preview_output_path.string());
  }
  append_progress("preview_image_written");

  if (first_valid_position.has_value() && last_valid_position.valid && !observed_motion) {
    throw std::runtime_error(
      "Pipeline runner observed stable reflected position across all ticks. The demo did not move.");
  }

  std::cout << "pipeline_runner.end";
  if (first_valid_position.has_value() && last_valid_position.valid) {
    std::cout
      << " first=(" << first_valid_position->x << ", " << first_valid_position->y << ')'
      << " last=(" << last_valid_position.x << ", " << last_valid_position.y << ')';
  }
  std::cout
    << " preview_pixels=" << non_empty_pixel_count
    << " preview_path=" << preview_output_path.string()
    << " preview_summary=" << runtime.render_preview_debug_summary();
  std::cout << '\n';
  std::cout.flush();
  std::cout.rdbuf(original_cout_buffer);
  append_progress("runner.completed");
  std::cerr << "pipeline_runner.log=" << log_path.string() << '\n';
  runtime.Destroy();
  append_progress("runtime.destroyed");
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    {
      std::filesystem::create_directories("D:/gptsandbox/artifacts/pipeline_runner");
      std::ofstream probe_file(
        "D:/gptsandbox/artifacts/pipeline_runner/argv_probe.log",
        std::ios::binary | std::ios::app);
      if (probe_file) {
        probe_file << "argc=" << argc << '\n';
        for (int i = 0; i < argc; ++i) {
          probe_file << "argv[" << i << "]=" << (argv[i] ? argv[i] : "<null>") << '\n';
        }
      }
    }

    PipelineRunnerOptions runner_options{};
    std::string runner_parse_error;
    if (!_ParsePipelineRunnerOptions(argc, argv, &runner_options, &runner_parse_error)) {
      if (!runner_parse_error.empty()) {
        throw std::runtime_error(runner_parse_error);
      }
      return 0;
    }
    if (runner_options.enabled) {
      return _RunPipelineRunner(runner_options) ? 0 : 1;
    }

    DebugToolBackendRuntime runtime;
    DebugToolFrontendPanel ui_panel;
    if (!runtime.Init("debugTool", 1280, 720)) {
      throw std::runtime_error("DebugToolBackendRuntime init failed");
    }
    runtime.runtime_environment().SetDrawCallback([&]() {
      ui_panel.Draw(runtime);
    });

    while (runtime.Tick()) {
    }

    ui_panel.Destroy();
    runtime.Destroy();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "debugTool error: " << e.what() << '\n';
    return 1;
  }
}
