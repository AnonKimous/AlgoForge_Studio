#include "debug_tool/runner_control_socket.h"
#include "debug_tool/debug_tool_backend_runtime.h"
#include "debug_tool/debug_cmd.h"
#include "debug_tool/debug_tool_frontend_panel.h"
#include "debug_tool/gif_writer.h"
#include "common_data/kernel_cfg.h"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

namespace {

#ifndef ALGOFORGE_VERBOSE_RUNTIME_LOGGING
#define ALGOFORGE_VERBOSE_RUNTIME_LOGGING 0
#endif

struct PipelineRunnerOptions {
  bool enabled{false};
  bool display_window{false};
  std::string algorithm_name{"v4a16_fireworks_pipeline_demo"};
  std::string pipeline_name{};
  uint32_t ticks{24u};
  uint32_t preview_width{640u};
  uint32_t preview_height{480u};
  std::string runner_endpoint{"127.0.0.1:0"};
  std::string render_preview_output_path{
    algomanager::ResolvePipelineRunnerArtifactRoot().string() + "/render_preview.png"};
  debug_tool::AlgorithmExecutionPreference execution_preference{
    debug_tool::AlgorithmExecutionPreference::Vk};
  bool export_swapchain_gif{false};
  std::string swapchain_gif_output_path{};
  double swapchain_gif_duration_seconds{20.0};
};

struct AlgorithmRunnerOptions {
  bool enabled{false};
  bool display_window{false};
  std::string algorithm_name{"v6a6_pbd_ball_collision_demo"};
  uint32_t ticks{24u};
  uint32_t preview_width{640u};
  uint32_t preview_height{480u};
  std::string runner_endpoint{"127.0.0.1:0"};
  std::string render_preview_output_path{
    algomanager::ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot().string() +
      "/render_preview.png"};
  debug_tool::AlgorithmExecutionPreference execution_preference{
    debug_tool::AlgorithmExecutionPreference::Vk};
  bool export_swapchain_gif{false};
  std::string swapchain_gif_output_path{};
  double swapchain_gif_duration_seconds{20.0};
};

struct PreviewRenderServerOptions {
  bool enabled{false};
  bool display_window{false};
  std::string algorithm_name{"v6a6_pbd_ball_collision_demo"};
  uint32_t ticks{24u};
  uint32_t preview_width{640u};
  uint32_t preview_height{480u};
  std::string runner_endpoint{"127.0.0.1:0"};
  std::string render_preview_output_path{
    algomanager::ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot().string() +
      "/render_preview.png"};
  debug_tool::AlgorithmExecutionPreference execution_preference{
    debug_tool::AlgorithmExecutionPreference::Vk};
};

struct RunnerServerOptions {
  bool enabled{false};
  bool once{false};
  std::string runner_endpoint{"127.0.0.1:0"};
};

struct PositionSample {
  bool valid{false};
  float x{0.0f};
  float y{0.0f};
};

double _MeasureGifSamplingFrequency(debug_tool_backend::DebugToolBackendRuntime& runtime) {
  constexpr uint32_t probe_tick_count = 8u;
  const auto probe_begin = std::chrono::steady_clock::now();
  for (uint32_t probe_index = 0u; probe_index < probe_tick_count; ++probe_index) {
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    if (!runtime.Tick()) {
      throw std::runtime_error("Runtime environment failed during GIF sampling frequency measurement.");
    }
  }
  const double elapsed_seconds = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - probe_begin).count();
  return static_cast<double>(probe_tick_count) / elapsed_seconds;
}

#ifdef _WIN32
LONG WINAPI _WriteCrashDump(EXCEPTION_POINTERS* exception_pointers) {
  const std::filesystem::path dump_dir = std::filesystem::path("testData") / "dumps";
  std::error_code ec;
  std::filesystem::create_directories(dump_dir, ec);

  const std::filesystem::path dump_path =
    dump_dir /
    (std::string("debugTool_") +
      std::to_string(static_cast<unsigned long>(GetCurrentProcessId())) +
      "_" +
      std::to_string(static_cast<unsigned long long>(GetTickCount64())) +
      ".dmp");

  HANDLE dump_file = CreateFileW(
    dump_path.wstring().c_str(),
    GENERIC_WRITE,
    0,
    nullptr,
    CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    nullptr);
  if (dump_file == INVALID_HANDLE_VALUE) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  MINIDUMP_EXCEPTION_INFORMATION info{};
  info.ThreadId = GetCurrentThreadId();
  info.ExceptionPointers = exception_pointers;
  info.ClientPointers = FALSE;

  const BOOL dumped = MiniDumpWriteDump(
    GetCurrentProcess(),
    GetCurrentProcessId(),
    dump_file,
    static_cast<MINIDUMP_TYPE>(
      MiniDumpNormal |
      MiniDumpWithThreadInfo |
      MiniDumpWithUnloadedModules |
      MiniDumpWithIndirectlyReferencedMemory),
    &info,
    nullptr,
    nullptr);
  CloseHandle(dump_file);

  std::ofstream crash_log(dump_dir / "last_crash.txt", std::ios::binary | std::ios::trunc);
  crash_log
    << "dump_path=" << dump_path.string() << '\n'
    << "dumped=" << (dumped ? "true" : "false") << '\n';
  return EXCEPTION_EXECUTE_HANDLER;
}

void _InstallCrashDumpHandler() {
  SetErrorMode(
    SEM_FAILCRITICALERRORS |
    SEM_NOGPFAULTERRORBOX |
    SEM_NOOPENFILEERRORBOX);
  SetUnhandledExceptionFilter(&_WriteCrashDump);
}
#endif

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
    case debug_tool::AlgorithmExecutionPreference::Jobs: return "jobs";
    case debug_tool::AlgorithmExecutionPreference::Vk: return "vk";
    case debug_tool::AlgorithmExecutionPreference::Cuda: return "cuda";
    case debug_tool::AlgorithmExecutionPreference::Compatibility: return "compatibility";
  }
  return "unknown";
}

const char* _ExecutionPreferenceName(algomanager::bridge::AlgorithmExecutionPreference preference) {
  switch (preference) {
    case algomanager::bridge::AlgorithmExecutionPreference::Jobs: return "jobs";
    case algomanager::bridge::AlgorithmExecutionPreference::Vk: return "vk";
    case algomanager::bridge::AlgorithmExecutionPreference::Cuda: return "cuda";
    case algomanager::bridge::AlgorithmExecutionPreference::Compatibility: return "compatibility";
  }
  return "unknown";
}

const char* _PhaseKindName(algomanager::bridge::AlgorithmPhaseKind phase_kind) {
  switch (phase_kind) {
    case algomanager::bridge::AlgorithmPhaseKind::Pretick: return "pretick";
    case algomanager::bridge::AlgorithmPhaseKind::Exec: return "exec";
    case algomanager::bridge::AlgorithmPhaseKind::AfterTick: return "aftertick";
    case algomanager::bridge::AlgorithmPhaseKind::RenderResult: return "renderresult";
    case algomanager::bridge::AlgorithmPhaseKind::Reflect: return "reflect";
    case algomanager::bridge::AlgorithmPhaseKind::Custom: return "custom";
  }
  return "custom";
}

bool _ReadFloatBytes(const std::vector<std::byte>& bytes, float* out_value) {
  if (!out_value || bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(out_value, bytes.data(), sizeof(float));
  return true;
}

const debug_tool::AlgorithmReflectionValue* _FindReflectionValue(
  const debug_tool::AlgorithmReflectionSnapshot& snapshot,
  const char* container_name);

std::optional<float> _ReadArrayFloatValue(
  const debug_tool::AlgorithmReflectionSnapshot& snapshot,
  const char* container_name,
  size_t index);

bool _ReadUint32Bytes(const std::vector<std::byte>& bytes, uint32_t* out_value) {
  if (!out_value || bytes.size() < sizeof(uint32_t)) {
    return false;
  }
  std::memcpy(out_value, bytes.data(), sizeof(uint32_t));
  return true;
}

bool _ReadFloatBytes(const std::vector<std::byte>& bytes, size_t offset, float* out_value) {
  if (!out_value || bytes.size() < offset + sizeof(float)) {
    return false;
  }
  std::memcpy(out_value, bytes.data() + offset, sizeof(float));
  return true;
}

void _PrintVec3(const char* label, const std::vector<std::byte>& bytes, size_t offset) {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!_ReadFloatBytes(bytes, offset + 0u, &x) ||
      !_ReadFloatBytes(bytes, offset + sizeof(float), &y) ||
      !_ReadFloatBytes(bytes, offset + sizeof(float) * 2u, &z)) {
    return;
  }
  std::cout << label << "=(" << x << ", " << y << ", " << z << ')';
}

void _PrintVec4(const char* label, const std::vector<std::byte>& bytes, size_t offset) {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;
  if (!_ReadFloatBytes(bytes, offset + 0u, &x) ||
      !_ReadFloatBytes(bytes, offset + sizeof(float), &y) ||
      !_ReadFloatBytes(bytes, offset + sizeof(float) * 2u, &z) ||
      !_ReadFloatBytes(bytes, offset + sizeof(float) * 3u, &w)) {
    return;
  }
  std::cout << label << "=(" << x << ", " << y << ", " << z << ", " << w << ')';
}

uint64_t _HashBytes(const std::vector<std::byte>& bytes) {
  uint64_t hash = 1469598103934665603ull;
  for (const std::byte value : bytes) {
    hash ^= static_cast<uint64_t>(std::to_integer<unsigned int>(value));
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string _FormatBytePreview(const std::vector<std::byte>& bytes, size_t max_count) {
  static constexpr char kHexDigits[] = "0123456789abcdef";
  const size_t count = std::min(max_count, bytes.size());
  std::string result;
  result.reserve(count * 2u + (bytes.size() > count ? 3u : 0u));
  for (size_t i = 0u; i < count; ++i) {
    const unsigned int value = std::to_integer<unsigned int>(bytes[i]);
    result.push_back(kHexDigits[(value >> 4u) & 0x0fu]);
    result.push_back(kHexDigits[value & 0x0fu]);
  }
  if (bytes.size() > count) {
    result += "...";
  }
  return result;
}

void _PrintReflectionValueHeader(
  const char* value_kind,
  const debug_tool::AlgorithmReflectionValue& value) {
  std::cout << "      " << value_kind << ' ';
  if (!value.reflection_object_name.empty()) {
    std::cout << "object=" << value.reflection_object_name << ' ';
  }
  std::cout
    << "container=" << value.container_name
    << " filter=" << value.filter_name
    << " storage=" << value.storage_kind
    << " bytes=" << value.bytes.size();
  if (!value.bytes.empty()) {
    uint64_t hash = _HashBytes(value.bytes);
    static constexpr char kHexDigits[] = "0123456789abcdef";
    char hex_buffer[17]{};
    for (int i = 15; i >= 0; --i) {
      hex_buffer[static_cast<size_t>(i)] = kHexDigits[static_cast<size_t>(hash & 0x0fu)];
      hash >>= 4u;
    }
    std::cout
      << " hash=0x" << hex_buffer
      << " preview=" << _FormatBytePreview(value.bytes, value.bytes.size() <= 64u ? value.bytes.size() : 32u);
  }
}

void _PrintReflectionValueDetails(
  const char* value_kind,
  const debug_tool::AlgorithmReflectionValue& value) {
  _PrintReflectionValueHeader(value_kind, value);
  if (value.bytes.size() == sizeof(uint32_t)) {
    uint32_t scalar_u32 = 0u;
    float scalar_f32 = 0.0f;
    if (_ReadUint32Bytes(value.bytes, &scalar_u32) && _ReadFloatBytes(value.bytes, &scalar_f32)) {
      std::cout << " u32=" << scalar_u32 << " f32=" << scalar_f32;
    }
  }
  std::cout << '\n';
}

void _PrintTeapotArrayReflection(const debug_tool::AlgorithmReflectionSnapshot& snapshot) {
  const debug_tool::AlgorithmReflectionValue* scene_info = _FindReflectionValue(snapshot, "scene_info");
  if (scene_info && scene_info->bytes.size() >= sizeof(float) * 4u) {
    std::cout << "      scene_info ";
    _PrintVec3("center", scene_info->bytes, 0u);
    float radius = 0.0f;
    if (_ReadFloatBytes(scene_info->bytes, sizeof(float) * 3u, &radius)) {
      std::cout << " radius=" << radius;
    }
    std::cout << '\n';
  }

  const debug_tool::AlgorithmReflectionValue* material_buffer =
    _FindReflectionValue(snapshot, "material_buffer");
  if (material_buffer && material_buffer->bytes.size() >= sizeof(float) * 12u) {
    std::cout << "      material_buffer ";
    _PrintVec3("Ka", material_buffer->bytes, 0u);
    float roughness = 0.0f;
    if (_ReadFloatBytes(material_buffer->bytes, sizeof(float) * 3u, &roughness)) {
      std::cout << " roughness=" << roughness;
    }
    std::cout << ' ';
    _PrintVec3("Kd", material_buffer->bytes, sizeof(float) * 4u);
    float metallic = 0.0f;
    if (_ReadFloatBytes(material_buffer->bytes, sizeof(float) * 7u, &metallic)) {
      std::cout << " metallic=" << metallic;
    }
    std::cout << ' ';
    _PrintVec3("Ks", material_buffer->bytes, sizeof(float) * 8u);
    float opacity = 0.0f;
    if (_ReadFloatBytes(material_buffer->bytes, sizeof(float) * 11u, &opacity)) {
      std::cout << " opacity=" << opacity;
    }
    std::cout << '\n';
  }

  const debug_tool::AlgorithmReflectionValue* triangle_buffer =
    _FindReflectionValue(snapshot, "triangle_buffer");
  if (triangle_buffer && triangle_buffer->bytes.size() >= sizeof(float) * 24u) {
    std::cout << "      triangle_buffer[0] ";
    _PrintVec4("p0", triangle_buffer->bytes, 0u);
    std::cout << ' ';
    _PrintVec4("p1", triangle_buffer->bytes, sizeof(float) * 4u);
    std::cout << ' ';
    _PrintVec4("p2", triangle_buffer->bytes, sizeof(float) * 8u);
    std::cout << '\n';
    std::cout << "      triangle_buffer[0] ";
    _PrintVec4("n0", triangle_buffer->bytes, sizeof(float) * 12u);
    std::cout << ' ';
    _PrintVec4("n1", triangle_buffer->bytes, sizeof(float) * 16u);
    std::cout << ' ';
    _PrintVec4("n2", triangle_buffer->bytes, sizeof(float) * 20u);
    std::cout << '\n';
  }

  const debug_tool::AlgorithmReflectionValue* bvh_buffer =
    _FindReflectionValue(snapshot, "bvh_buffer");
  if (bvh_buffer && bvh_buffer->bytes.size() >= sizeof(float) * 8u) {
    std::cout << "      bvh_buffer[0] ";
    _PrintVec4("min", bvh_buffer->bytes, 0u);
    std::cout << ' ';
    _PrintVec4("max", bvh_buffer->bytes, sizeof(float) * 4u);
    std::cout << '\n';
  }
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
  const char* container_name);

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

bool _ParsePositiveDouble(const char* text, double* out_value) {
  if (!text || !*text || !out_value) {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(text, &end);
  if (!end || *end != '\0' || !std::isfinite(parsed) || parsed <= 0.0) {
    return false;
  }
  *out_value = parsed;
  return true;
}

bool _ParseExecutionPreference(
  const char* text,
  debug_tool::AlgorithmExecutionPreference* out_preference) {
  if (!text || !out_preference) {
    return false;
  }
  const std::string value(text);
  if (value == "jobs") {
    *out_preference = debug_tool::AlgorithmExecutionPreference::Jobs;
    return true;
  }
  if (value == "vk") {
    *out_preference = debug_tool::AlgorithmExecutionPreference::Vk;
    return true;
  }
  if (value == "cuda") {
    *out_preference = debug_tool::AlgorithmExecutionPreference::Cuda;
    return true;
  }
  if (value == "compat" || value == "compatibility") {
    *out_preference = debug_tool::AlgorithmExecutionPreference::Compatibility;
    return true;
  }
  return false;
}

bool _IsPipelineRunnerInvocation(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == "--pipeline-runner") {
      return true;
    }
  }
  return false;
}

bool _IsAlgorithmRunnerInvocation(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == "--algorithm-runner") {
      return true;
    }
  }
  return false;
}

bool _IsPreviewRenderServerInvocation(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == "--preview-render-server") {
      return true;
    }
  }
  return false;
}

bool _IsPreviewWindowInvocation(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && std::string(argv[i]) == "--preview-window") {
      return true;
    }
  }
  return false;
}

bool _IsRunnerServerInvocation(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && (std::string(argv[i]) == "--runner-server" || std::string(argv[i]) == "--runner-server-once")) {
      return true;
    }
  }
  return false;
}

void _WritePngBigEndianU32(std::ofstream& output, uint32_t value) {
  output.put(static_cast<char>((value >> 24u) & 0xFFu));
  output.put(static_cast<char>((value >> 16u) & 0xFFu));
  output.put(static_cast<char>((value >> 8u) & 0xFFu));
  output.put(static_cast<char>(value & 0xFFu));
}

uint32_t _PngCrc32(const std::vector<uint8_t>& bytes) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const uint8_t byte : bytes) {
    crc ^= byte;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
      crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xEDB88320u : (crc >> 1u);
    }
  }
  return ~crc;
}

uint32_t _PngAdler32(const std::vector<uint8_t>& bytes) {
  constexpr uint32_t modulo = 65521u;
  uint32_t first = 1u;
  uint32_t second = 0u;
  for (const uint8_t byte : bytes) {
    first = (first + byte) % modulo;
    second = (second + first) % modulo;
  }
  return (second << 16u) | first;
}

bool _WritePngChunk(
  std::ofstream& output,
  const char type[4],
  const std::vector<uint8_t>& data) {
  _WritePngBigEndianU32(output, static_cast<uint32_t>(data.size()));
  output.write(type, 4);
  if (!data.empty()) {
    output.write(
      reinterpret_cast<const char*>(data.data()),
      static_cast<std::streamsize>(data.size()));
  }
  std::vector<uint8_t> crc_input;
  crc_input.reserve(4u + data.size());
  for (size_t index = 0u; index < 4u; ++index) {
    crc_input.push_back(static_cast<uint8_t>(type[index]));
  }
  crc_input.insert(crc_input.end(), data.begin(), data.end());
  _WritePngBigEndianU32(output, _PngCrc32(crc_input));
  return static_cast<bool>(output);
}

bool _WritePngImage(
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

  std::vector<uint8_t> raw_scanlines;
  raw_scanlines.reserve(static_cast<size_t>(height) * (1u + static_cast<size_t>(width) * 4u));
  for (uint32_t y = 0u; y < height; ++y) {
    const uint32_t source_y = height - 1u - y;
    raw_scanlines.push_back(0u);
    for (uint32_t x = 0u; x < width; ++x) {
      const size_t pixel_index =
        (static_cast<size_t>(source_y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
      raw_scanlines.push_back(std::to_integer<uint8_t>(rgba_bytes[pixel_index + 0u]));
      raw_scanlines.push_back(std::to_integer<uint8_t>(rgba_bytes[pixel_index + 1u]));
      raw_scanlines.push_back(std::to_integer<uint8_t>(rgba_bytes[pixel_index + 2u]));
      raw_scanlines.push_back(std::to_integer<uint8_t>(rgba_bytes[pixel_index + 3u]));
    }
  }

  std::vector<uint8_t> compressed;
  compressed.reserve(raw_scanlines.size() + (raw_scanlines.size() / 65535u + 1u) * 5u + 6u);
  compressed.push_back(0x78u);
  compressed.push_back(0x01u);
  size_t raw_offset = 0u;
  while (raw_offset < raw_scanlines.size()) {
    const size_t block_size = std::min<size_t>(65535u, raw_scanlines.size() - raw_offset);
    const bool final_block = raw_offset + block_size == raw_scanlines.size();
    compressed.push_back(final_block ? 0x01u : 0x00u);
    const uint16_t length = static_cast<uint16_t>(block_size);
    compressed.push_back(static_cast<uint8_t>(length & 0xFFu));
    compressed.push_back(static_cast<uint8_t>((length >> 8u) & 0xFFu));
    const uint16_t inverse_length = static_cast<uint16_t>(~length);
    compressed.push_back(static_cast<uint8_t>(inverse_length & 0xFFu));
    compressed.push_back(static_cast<uint8_t>((inverse_length >> 8u) & 0xFFu));
    compressed.insert(
      compressed.end(),
      raw_scanlines.begin() + static_cast<std::ptrdiff_t>(raw_offset),
      raw_scanlines.begin() + static_cast<std::ptrdiff_t>(raw_offset + block_size));
    raw_offset += block_size;
  }
  const uint32_t adler = _PngAdler32(raw_scanlines);
  compressed.push_back(static_cast<uint8_t>((adler >> 24u) & 0xFFu));
  compressed.push_back(static_cast<uint8_t>((adler >> 16u) & 0xFFu));
  compressed.push_back(static_cast<uint8_t>((adler >> 8u) & 0xFFu));
  compressed.push_back(static_cast<uint8_t>(adler & 0xFFu));

  output.write("\x89PNG\r\n\x1A\n", 8);
  const std::vector<uint8_t> header{
    static_cast<uint8_t>((width >> 24u) & 0xFFu),
    static_cast<uint8_t>((width >> 16u) & 0xFFu),
    static_cast<uint8_t>((width >> 8u) & 0xFFu),
    static_cast<uint8_t>(width & 0xFFu),
    static_cast<uint8_t>((height >> 24u) & 0xFFu),
    static_cast<uint8_t>((height >> 16u) & 0xFFu),
    static_cast<uint8_t>((height >> 8u) & 0xFFu),
    static_cast<uint8_t>(height & 0xFFu),
    8u,
    6u,
    0u,
    0u,
    0u,
  };
  if (!output ||
      !_WritePngChunk(output, "IHDR", header) ||
      !_WritePngChunk(output, "IDAT", compressed) ||
      !_WritePngChunk(output, "IEND", {})) {
    return false;
  }
  output.flush();
  return static_cast<bool>(output);
}

std::filesystem::path _BuildPipelineFramePreviewPath(
  const std::filesystem::path& output_path,
  uint32_t frame_index) {
  return output_path.parent_path() /
    (output_path.stem().string() + "_frame" + std::to_string(frame_index) + output_path.extension().string());
}

std::vector<std::byte> _ReadBinaryFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }

  const std::streamsize size = file.tellg();
  if (size <= 0) {
    return {};
  }

  std::vector<std::byte> bytes(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!file) {
    return {};
  }
  return bytes;
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
    if (argument == "--preview-window") {
      options.display_window = true;
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
      options.render_preview_output_path = argv[++i];
      continue;
    }
    if (argument == "--preview-gif") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--preview-gif requires a non-empty output path.";
        }
        return false;
      }
      options.export_swapchain_gif = true;
      options.swapchain_gif_output_path = argv[++i];
      continue;
    }
    if (argument == "--gif-duration") {
      if (i + 1 >= argc || !_ParsePositiveDouble(argv[i + 1], &options.swapchain_gif_duration_seconds)) {
        if (out_error_message) {
          *out_error_message = "--gif-duration requires a positive number of seconds.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--execution") {
      if (i + 1 >= argc || !_ParseExecutionPreference(argv[i + 1], &options.execution_preference)) {
        if (out_error_message) {
          *out_error_message = "--execution requires 'jobs', 'vk', 'cuda', or 'compatibility'.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--runner-endpoint") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--runner-endpoint requires a non-empty value.";
        }
        return false;
      }
      options.runner_endpoint = argv[++i];
      continue;
    }
    if (argument == "--help" || argument == "-h") {
      std::cout
        << "Usage:\n"
        << "  debugTool.exe --algorithm-runner "
        << "[--algorithm <name>] [--ticks <count>] "
        << "[--preview-width <px>] [--preview-height <px>] [--preview-output <path>] "
        << "[--execution jobs|vk|cuda|compatibility] "
        << "[--runner-endpoint <host:port>]\n"
        << "  debugTool.exe --pipeline-runner "
        << "[--algorithm <name>] [--pipeline-name <name>] [--ticks <count>] "
        << "[--preview-width <px>] [--preview-height <px>] [--preview-output <path>] "
        << "[--preview-gif <path>] [--gif-duration <seconds>] "
        << "[--execution jobs|vk|cuda|compatibility] "
        << "[--runner-endpoint <host:port>]\n"
        << "  debugTool.exe --runner-server [--runner-endpoint <host:port>] [--runner-server-once]\n";
      return false;
    }
  }

  *out_options = std::move(options);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _ParseAlgorithmRunnerOptions(
  int argc,
  char** argv,
  AlgorithmRunnerOptions* out_options,
  std::string* out_error_message) {
  if (!out_options) {
    if (out_error_message) {
      *out_error_message = "Algorithm runner option output pointer is null.";
    }
    return false;
  }

  AlgorithmRunnerOptions options{};
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i] ? argv[i] : "";
    if (argument == "--algorithm-runner") {
      options.enabled = true;
      continue;
    }
    if (argument == "--preview-window") {
      options.display_window = true;
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
      options.render_preview_output_path = argv[++i];
      continue;
    }
    if (argument == "--preview-gif") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--preview-gif requires a non-empty output path.";
        }
        return false;
      }
      options.export_swapchain_gif = true;
      options.swapchain_gif_output_path = argv[++i];
      continue;
    }
    if (argument == "--gif-duration") {
      if (i + 1 >= argc || !_ParsePositiveDouble(argv[i + 1], &options.swapchain_gif_duration_seconds)) {
        if (out_error_message) {
          *out_error_message = "--gif-duration requires a positive number of seconds.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--execution") {
      if (i + 1 >= argc || !_ParseExecutionPreference(argv[i + 1], &options.execution_preference)) {
        if (out_error_message) {
          *out_error_message = "--execution requires 'jobs', 'vk', 'cuda', or 'compatibility'.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--runner-endpoint") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--runner-endpoint requires a non-empty value.";
        }
        return false;
      }
      options.runner_endpoint = argv[++i];
      continue;
    }
    if (argument == "--help" || argument == "-h") {
      std::cout
        << "Usage:\n"
        << "  debugTool.exe --algorithm-runner "
        << "[--algorithm <name>] [--ticks <count>] "
        << "[--preview-width <px>] [--preview-height <px>] [--preview-output <path>] "
        << "[--preview-gif <path>] [--gif-duration <seconds>] "
        << "[--execution jobs|vk|cuda|compatibility] "
        << "[--runner-endpoint <host:port>]\n";
      return false;
    }
  }

  *out_options = std::move(options);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _ParsePreviewRenderServerOptions(
  int argc,
  char** argv,
  PreviewRenderServerOptions* out_options,
  std::string* out_error_message) {
  if (!out_options) {
    if (out_error_message) {
      *out_error_message = "Preview render server option output pointer is null.";
    }
    return false;
  }

  PreviewRenderServerOptions options{};
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i] ? argv[i] : "";
    if (argument == "--preview-render-server") {
      options.enabled = true;
      continue;
    }
    if (argument == "--preview-window") {
      options.enabled = true;
      options.display_window = true;
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
      options.render_preview_output_path = argv[++i];
      continue;
    }
    if (argument == "--execution") {
      if (i + 1 >= argc || !_ParseExecutionPreference(argv[i + 1], &options.execution_preference)) {
        if (out_error_message) {
          *out_error_message = "--execution requires 'jobs', 'vk', 'cuda', or 'compatibility'.";
        }
        return false;
      }
      ++i;
      continue;
    }
    if (argument == "--runner-endpoint") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--runner-endpoint requires a non-empty value.";
        }
        return false;
      }
      options.runner_endpoint = argv[++i];
      continue;
    }
    if (argument == "--help" || argument == "-h") {
      std::cout
        << "Usage:\n"
        << "  debugTool.exe --preview-window "
        << "[--algorithm <name>] [--ticks <count>] "
        << "[--preview-width <px>] [--preview-height <px>] [--preview-output <path>] "
        << "[--execution jobs|vk|cuda|compatibility]\n";
      return false;
    }
  }

  *out_options = std::move(options);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _ParseRunnerServerOptions(
  int argc,
  char** argv,
  RunnerServerOptions* out_options,
  std::string* out_error_message) {
  if (!out_options) {
    if (out_error_message) {
      *out_error_message = "Runner server option output pointer is null.";
    }
    return false;
  }

  RunnerServerOptions options{};
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i] ? argv[i] : "";
    if (argument == "--runner-server") {
      options.enabled = true;
      continue;
    }
    if (argument == "--runner-server-once") {
      options.enabled = true;
      options.once = true;
      continue;
    }
    if (argument == "--runner-endpoint") {
      if (i + 1 >= argc || !argv[i + 1] || !*argv[i + 1]) {
        if (out_error_message) {
          *out_error_message = "--runner-endpoint requires a non-empty value.";
        }
        return false;
      }
      options.runner_endpoint = argv[++i];
      continue;
    }
    if (argument == "--help" || argument == "-h") {
      std::cout
        << "Usage:\n"
        << "  debugTool.exe --runner-server [--runner-endpoint <host:port>] [--runner-server-once]\n";
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
  std::cout << "    reflection.algorithm=" << snapshot.algorithm_name << '\n';
  std::cout << "    reflection.valid=" << (snapshot.valid ? "true" : "false") << '\n';
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variables) {
    _PrintReflectionValueDetails("var", value);
  }
  for (const debug_tool::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    _PrintReflectionValueDetails("array", value);
  }
  _PrintTeapotArrayReflection(snapshot);
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

void _PrintReflectionSnapshotPresence(
  const char* label,
  const debug_tool::AlgorithmReflectionSnapshot& snapshot) {
  std::cout
    << "    " << label
    << ".valid=" << (snapshot.valid ? "true" : "false")
    << " vars=" << snapshot.variables.size()
    << " arrays=" << snapshot.variable_arrays.size()
    << '\n';
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
  _PrintReflectionSnapshotPresence("bridge.stage_input", bridge_summary.stage_input_reflection_snapshot);
  _PrintReflectionSnapshotPresence("bridge.stage_output", bridge_summary.stage_output_reflection_snapshot);
  _PrintReflectionSnapshotPresence("bridge.next_stage_input", bridge_summary.next_stage_input_reflection_snapshot);
  _PrintReflectionSnapshotPresence("bridge.replay_output", bridge_summary.replay_output_reflection_snapshot);
  _PrintReflectionSnapshotPresence("bridge.replay_runtime", bridge_summary.replay_reflection_snapshot);
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
    algomanager::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot();
  std::error_code ec;
  std::filesystem::create_directories(log_directory, ec);
  if (ec) {
    throw std::runtime_error(
      "Failed to create pipeline runner log directory: " + log_directory.string());
  }
  const std::filesystem::path cache_loader_probe_path = log_directory / "cache_loader_probe.log";
  const std::filesystem::path agent_mount_probe_path = log_directory / "agent_mount_probe.log";
  const std::filesystem::path progress_path = log_directory / "progress_probe.log";
  const std::filesystem::path reflection_probe_path = log_directory / "pipeline_reflection_probe.log";
  const std::filesystem::path log_path = log_directory / "last_run.log";
  const std::filesystem::path render_preview_output_path(options.render_preview_output_path);
  std::filesystem::remove(cache_loader_probe_path, ec);
  ec.clear();
  std::filesystem::remove(agent_mount_probe_path, ec);
  ec.clear();
  std::filesystem::remove(progress_path, ec);
  ec.clear();
  std::filesystem::remove(reflection_probe_path, ec);
  ec.clear();
  std::filesystem::remove(log_path, ec);
  ec.clear();
  std::filesystem::remove(render_preview_output_path, ec);
  ec.clear();
  const auto append_progress = [&](const std::string& line) {
    std::ofstream progress_file(progress_path, std::ios::binary | std::ios::app);
    if (progress_file) {
      progress_file << line << '\n';
    }
  };
  const auto append_reflection_probe = [&](const std::string& line) {
    std::ofstream probe_file(reflection_probe_path, std::ios::binary | std::ios::app);
    if (probe_file) {
      probe_file << line << '\n';
    }
  };
  append_progress("runner.begin");
  std::ofstream log_file(log_path, std::ios::binary | std::ios::trunc);
  if (!log_file) {
    throw std::runtime_error("Failed to open pipeline runner log file: " + log_path.string());
  }
  std::streambuf* const original_cout_buffer = std::cout.rdbuf(log_file.rdbuf());
  std::streambuf* const original_cerr_buffer = std::cerr.rdbuf(log_file.rdbuf());

  DebugToolBackendRuntime runtime;
  append_progress("runtime.created");
  append_progress("runtime_init_begin");
  if (!runtime.Init("debugToolRunner", 1280, 720)) {
    throw std::runtime_error("DebugToolBackendRuntime init failed in pipeline runner mode.");
  }
  append_progress("runtime_init_end");
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
  append_progress("default_bindings_begin");
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
  append_progress("default_bindings_end");
  append_progress("default_bindings_loaded");

  const std::string mounted_pipeline_name = _BuildRunnerPipelineName(options);
  const std::string submission_name = _BuildRunnerSubmissionName(options);

  size_t mounted_pipeline_index = 0u;
  debug_tool::DebugCommandResult mount_result{};
  if (!debug_tool::DebugCmd::Execute(
        runtime,
        debug_tool::DebugCommand{
          .id = debug_tool::DebugCommandId::AttachPipelinePackage,
          .algorithm_name = options.algorithm_name,
          .pipeline_name = mounted_pipeline_name,
          .execution_preference = options.execution_preference,
          .resource_bindings = resource_bindings,
          .descriptor_values = descriptor_values,
        },
        &mount_result)) {
    throw std::runtime_error(
      mount_result.message.empty()
        ? ("Failed to mount pipeline '" + mounted_pipeline_name + "'.")
        : mount_result.message);
  }
  mounted_pipeline_index = mount_result.algorithm_index;
  append_progress("pipeline_mounted");

  debug_tool::DebugCommandResult submission_result{};
  if (!debug_tool::DebugCmd::Execute(
        runtime,
        debug_tool::DebugCommand{
          .id = debug_tool::DebugCommandId::AttachPipelinePackage,
          .algorithm_name = options.algorithm_name,
          .pipeline_name = submission_name,
          .execution_preference = options.execution_preference,
          .resource_bindings = resource_bindings,
          .descriptor_values = descriptor_values,
        },
        &submission_result)) {
    throw std::runtime_error(
      submission_result.message.empty()
        ? ("Failed to submit pipeline resource batch '" + submission_name + "'.")
        : submission_result.message);
  }
  append_progress("resource_batch_submitted");

  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::SetRenderPreviewExtent,
        .preview_extent = ImVec2(
          static_cast<float>(options.preview_width),
          static_cast<float>(options.preview_height)),
      }, nullptr)) {
    throw std::runtime_error("Failed to set pipeline preview extent.");
  }
  append_progress("preview_extent_set");

  runtimesys::RenderPreviewRequest preview_request{};
  if (!runtime.BuildRenderPreviewRequest(
        0u,
        mounted_pipeline_index,
        &preview_request,
        &error_message) ||
      !preview_request.valid) {
    throw std::runtime_error(
      error_message.empty()
        ? "Render preview request is invalid after pipeline mount."
        : error_message);
  }
  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::SetRenderPreviewRequest,
        .preview_request = std::move(preview_request),
      }, nullptr)) {
    throw std::runtime_error("Failed to set pipeline preview request before ticking.");
  }
  append_progress("preview_request_set_before_ticks");

  if (!runtime.runtime_environment().Tick()) {
    throw std::runtime_error("Runtime environment failed before pipeline ticking.");
  }
  append_progress("preview_frame_rendered_before_ticks");

  append_progress("start_ticking_begin");
  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::StartTick,
      }, nullptr)) {
    throw std::runtime_error("Failed to start pipeline ticking.");
  }
  append_progress("start_ticking_end");
  append_progress("ticking_started");

  double gif_engine_frequency_hz = static_cast<double>(common_data::DefaultAgentLimitFpsFlag());
  if (options.export_swapchain_gif) {
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    if (!runtime.Tick()) {
      throw std::runtime_error("Runtime environment failed during pipeline GIF sampling warm-up tick.");
    }
    append_progress("gif_sampling_warmup_complete");
    gif_engine_frequency_hz = _MeasureGifSamplingFrequency(runtime);
    append_progress("gif_sampling_frequency_measured");
    runtimesys::RenderPreviewRequest gif_preview_request{};
    if (!runtime.BuildRenderPreviewRequest(
          0u,
          mounted_pipeline_index,
          &gif_preview_request,
          &error_message) ||
        !gif_preview_request.valid) {
      throw std::runtime_error(
        error_message.empty()
          ? "Render preview request is invalid after pipeline GIF sampling warm-up."
          : error_message);
    }
    if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
          .id = debug_tool::DebugCommandId::SetRenderPreviewRequest,
          .preview_request = std::move(gif_preview_request),
        }, nullptr)) {
      throw std::runtime_error("Failed to update pipeline render preview request before GIF recording.");
    }
    runtime.BeginDebugToolRecording();
    append_progress("debug_tool_recording_started");
  }

  if (options.display_window) {
    DebugToolFrontendPanel ui_panel;
    runtime.runtime_environment().SetDrawCallback([&]() {
      ui_panel.DrawRenderPreviewOnly(runtime);
    });
    while (runtime.Tick()) {
    }
    ui_panel.Destroy();
    std::cout.rdbuf(original_cout_buffer);
    std::cerr.rdbuf(original_cerr_buffer);
    append_progress("runner.completed");
    runtime.Destroy();
    append_progress("runtime.destroyed");
    return true;
  }

  std::cout
    << "pipeline_runner.begin algorithm=" << options.algorithm_name
    << " pipeline=" << mounted_pipeline_name
    << " execution=" << _ExecutionPreferenceName(options.execution_preference)
    << " ticks=" << (options.export_swapchain_gif ? 0u : options.ticks)
    << " preview=" << options.preview_width << 'x' << options.preview_height
    << " defaults=" << (has_default_file ? "true" : "false")
    << " swapchain_gif=" << (options.export_swapchain_gif ? "true" : "false")
    << " gif_duration_seconds=" << (options.export_swapchain_gif ? options.swapchain_gif_duration_seconds : 0.0)
    << '\n';

  std::optional<PositionSample> first_valid_position{};
  PositionSample last_valid_position{};
  bool observed_motion = false;
  std::vector<std::byte> first_preview_rgba{};
  std::vector<std::byte> last_preview_rgba{};
  ImVec2 last_preview_size{};
  uint64_t first_preview_hash = 0u;
  uint64_t last_preview_hash = 0u;

  const uint32_t runner_tick_count = options.ticks;
  uint32_t gif_total_tick_count = static_cast<uint32_t>(std::ceil(
    options.swapchain_gif_duration_seconds * gif_engine_frequency_hz));
  if (gif_total_tick_count == 0u) {
    gif_total_tick_count = 1u;
  }

  for (uint32_t tick_index = 0u;
       options.export_swapchain_gif
         ? tick_index < gif_total_tick_count
         : tick_index < runner_tick_count;
       ++tick_index) {
    debug_tool::DebugCommandResult timing_request_result{};
    if (!debug_tool::DebugCmd::Execute(
          runtime,
          debug_tool::DebugCommand{
            .id = debug_tool::DebugCommandId::RequestTimingLog,
            .agent_index = 0u,
          },
          &timing_request_result)) {
      throw std::runtime_error(
        timing_request_result.message.empty()
          ? "Failed to request pipeline timing log."
          : timing_request_result.message);
    }
    append_progress("tick_loop_begin_" + std::to_string(tick_index + 1u));
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    if (!runtime.Tick()) {
      throw std::runtime_error(
        runtime.ui_status_message().empty()
          ? "Pipeline runner tick failed."
          : runtime.ui_status_message());
    }
    append_progress("tick_complete_" + std::to_string(tick_index + 1u));

    if (options.export_swapchain_gif) {
      runtimesys::RenderPreviewRequest gif_preview_request{};
      if (!runtime.BuildRenderPreviewRequest(
            0u,
            mounted_pipeline_index,
            &gif_preview_request,
            &error_message) ||
          !gif_preview_request.valid) {
        throw std::runtime_error(
          error_message.empty()
            ? "Render preview request is invalid during pipeline GIF recording."
            : error_message);
      }
      if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
            .id = debug_tool::DebugCommandId::SetRenderPreviewRequest,
            .preview_request = std::move(gif_preview_request),
          }, nullptr)) {
        throw std::runtime_error("Failed to update pipeline render preview request during GIF recording.");
      }
    }

    std::vector<std::byte> frame_rgba{};
    ImVec2 frame_size{};
    if (!options.export_swapchain_gif &&
        !runtime.runtime_environment().ReadbackRenderPreviewTexture(&frame_rgba, &frame_size)) {
      throw std::runtime_error(
        "Failed to read back pipeline preview frame " + std::to_string(tick_index + 1u) + ".");
    }
    if (options.export_swapchain_gif) {
      continue;
    }
    const uint32_t frame_width = static_cast<uint32_t>(frame_size.x);
    const uint32_t frame_height = static_cast<uint32_t>(frame_size.y);
    const uint64_t frame_hash = _HashBytes(frame_rgba);
    const size_t frame_pixel_count =
      static_cast<size_t>(frame_width) * static_cast<size_t>(frame_height);
    size_t frame_non_empty_pixel_count = 0u;
    for (size_t pixel_index = 0u; pixel_index < frame_pixel_count; ++pixel_index) {
      const size_t byte_index = pixel_index * 4u;
      if (frame_rgba[byte_index + 0u] != std::byte{0} ||
          frame_rgba[byte_index + 1u] != std::byte{0} ||
          frame_rgba[byte_index + 2u] != std::byte{0} ||
          frame_rgba[byte_index + 3u] != std::byte{0}) {
        ++frame_non_empty_pixel_count;
      }
    }
    if (tick_index == 0u) {
      first_preview_rgba = frame_rgba;
      first_preview_hash = frame_hash;
    }
    last_preview_rgba = std::move(frame_rgba);
    last_preview_size = frame_size;
    last_preview_hash = frame_hash;
    if (tick_index < 2u &&
        !_WritePngImage(
          _BuildPipelineFramePreviewPath(render_preview_output_path, tick_index + 1u),
          tick_index == 0u ? first_preview_rgba : last_preview_rgba,
          frame_width,
          frame_height)) {
      throw std::runtime_error(
        "Failed to write pipeline preview frame " + std::to_string(tick_index + 1u) + ".");
    }
    std::cout
      << "  frame[" << (tick_index + 1u) << "] hash=0x"
      << std::hex << frame_hash << std::dec
      << " pixels=" << frame_non_empty_pixel_count
      << " output="
      << _BuildPipelineFramePreviewPath(render_preview_output_path, tick_index + 1u).string()
      << '\n';

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

#if ALGOFORGE_VERBOSE_RUNTIME_LOGGING
    std::cout << "[tick " << (tick_index + 1u) << "] stage_count=" << pipeline_stages.size() << '\n';
#endif
    for (const debug_tool::AlgorithmRuntimeSummary& summary : pipeline_stages) {
#if ALGOFORGE_VERBOSE_RUNTIME_LOGGING
      append_reflection_probe(
        "tick=" + std::to_string(tick_index + 1u) +
        " stage_index=" + std::to_string(summary.pipeline_stage_index) +
        " stage=" + summary.algorithm_name +
        " reflection_valid=" + std::string(summary.reflection_snapshot.valid ? "true" : "false") +
        " vars=" + std::to_string(summary.reflection_snapshot.variables.size()) +
        " arrays=" + std::to_string(summary.reflection_snapshot.variable_arrays.size()) +
        " bridge_input_valid=" +
          std::string(summary.bridge_debug_set.has_stage_input_reflection_snapshot ? "true" : "false") +
        " bridge_output_valid=" +
          std::string(summary.bridge_debug_set.has_stage_output_reflection_snapshot ? "true" : "false") +
        " bridge_next_valid=" +
          std::string(summary.bridge_debug_set.has_next_stage_input_reflection_snapshot ? "true" : "false") +
        " bridge_replay_valid=" +
          std::string(summary.bridge_debug_set.replay_valid ? "true" : "false"));
      std::cout
        << "  stage[" << summary.pipeline_stage_index << "] "
        << summary.algorithm_name
        << " state=" << _AssemblyStateName(summary.assembly_state)
        << " exec=" << _ExecutionPreferenceName(summary.execution_preference)
        << " jobs_symbol=" << (summary.jobs_symbol ? "true" : "false")
        << " vk_symbol=" << (summary.vk_symbol ? "true" : "false");
      if (summary.pipeline_active_stage_index_valid) {
        std::cout << " active_stage=" << summary.pipeline_active_stage_index;
      }
      std::cout << '\n';

      _PrintReflectionSnapshotPresence("reflection", summary.reflection_snapshot);
      _PrintReflectionSnapshot(summary.reflection_snapshot);
#endif
      if (summary.pipeline_stage_index == 0u) {
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
#if ALGOFORGE_VERBOSE_RUNTIME_LOGGING
      _PrintBridgeDebugSummary(summary.bridge_debug_set);
#endif
    }
  }

  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::PauseTick,
      }, nullptr)) {
    throw std::runtime_error("Failed to pause pipeline ticking.");
  }
  append_progress("ticking_paused");
  if (options.export_swapchain_gif) {
    std::vector<runtimesys::DebugToolRecordedFrame> recorded_frames =
      runtime.EndDebugToolRecording();
    append_progress("debug_tool_recording_stopped");
    const uint64_t recorded_tick_count = runtime.DebugToolRecordingTickCount();
    debug_tool::GifWriter gif_writer{};
    uint32_t gif_frame_count = 0u;
    const uint16_t frame_delay_centiseconds = static_cast<uint16_t>(std::clamp(
      static_cast<long>(std::lround(100.0 / gif_engine_frequency_hz)),
      1l,
      65535l));
    for (runtimesys::DebugToolRecordedFrame& frame : recorded_frames) {
      if (gif_frame_count == 0u) {
        gif_writer.Open(
          options.swapchain_gif_output_path,
          frame.width,
          frame.height,
          frame_delay_centiseconds);
      }
      gif_writer.WriteRgbaFrame(frame.rgba, frame_delay_centiseconds);
      ++gif_frame_count;
    }
    if (gif_frame_count > 0u) {
      gif_writer.Close();
      last_preview_rgba = std::move(recorded_frames.back().rgba);
      last_preview_size = ImVec2(
        static_cast<float>(recorded_frames.back().width),
        static_cast<float>(recorded_frames.back().height));
    }
    std::cout
      << "swapchain_gif.end path=" << options.swapchain_gif_output_path
      << " frames=" << gif_frame_count
      << " scheduler_ticks=" << recorded_tick_count
      << " duration_seconds=" << (
        static_cast<double>(gif_frame_count) *
        std::max(0.01, 1.0 / gif_engine_frequency_hz))
      << " effective_frequency_hz=" << gif_engine_frequency_hz
      << " source=backend.offscreen_preview_texture.recording_buffer\n";
    append_progress("swapchain_gif_closed");
  }
  if (options.ticks >= 2u && first_preview_hash == last_preview_hash) {
    throw std::runtime_error(
      "VK/pipeline preview frames are identical; pipeline execution did not advance the image.");
  }

  const uint32_t preview_width = static_cast<uint32_t>(last_preview_size.x);
  const uint32_t preview_height = static_cast<uint32_t>(last_preview_size.y);
  const std::vector<std::byte>& preview_rgba = last_preview_rgba;
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
  if (!_WritePngImage(render_preview_output_path, preview_rgba, preview_width, preview_height)) {
    throw std::runtime_error(
      "Failed to write render preview image: " + render_preview_output_path.string());
  }
  append_progress("preview_image_written");

  debug_tool::DebugCommandResult timing_export_result{};
  if (!debug_tool::DebugCmd::Execute(
        runtime,
        debug_tool::DebugCommand{
          .id = debug_tool::DebugCommandId::ExportPipelineTiming,
          .agent_index = 0u,
          .pipeline_name = mounted_pipeline_name,
        },
        &timing_export_result)) {
    throw std::runtime_error(
      timing_export_result.message.empty()
        ? "Failed to export pipeline timing artifacts."
        : timing_export_result.message);
  }
  append_progress("pipeline_timing_csv=" + timing_export_result.csv_path);
  append_progress("pipeline_timing_mermaid=" + timing_export_result.mermaid_path);
  std::cout
    << "pipeline_timing.csv=" << timing_export_result.csv_path << '\n'
    << "pipeline_timing.mermaid=" << timing_export_result.mermaid_path << '\n';

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
    << " render_preview_path=" << render_preview_output_path.string()
    << " preview_summary=" << runtime.render_preview_debug_summary();
  std::cout << '\n';
  std::cout.flush();
  std::cout.rdbuf(original_cout_buffer);
  std::cerr.rdbuf(original_cerr_buffer);
  append_progress("runner.completed");
  std::cerr << "pipeline_runner.log=" << log_path.string() << '\n';
  runtime.Destroy();
  append_progress("runtime.destroyed");
  return true;
}

bool _RunAlgorithmRunner(const AlgorithmRunnerOptions& options) {
  const std::filesystem::path log_directory =
    algomanager::ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot();
  std::error_code ec;
  std::filesystem::create_directories(log_directory, ec);
  if (ec) {
    throw std::runtime_error(
      "Failed to create algorithm runner log directory: " + log_directory.string());
  }
  const std::filesystem::path cache_loader_probe_path = log_directory / "cache_loader_probe.log";
  const std::filesystem::path attach_probe_path = log_directory / "attach_probe.log";
  const std::filesystem::path progress_path = log_directory / "progress_probe.log";
  const std::filesystem::path reflection_probe_path = log_directory / "algorithm_reflection_probe.log";
  const std::filesystem::path log_path = log_directory / "last_run.log";
  const std::filesystem::path render_preview_output_path(options.render_preview_output_path);
  std::filesystem::remove(cache_loader_probe_path, ec);
  ec.clear();
  std::filesystem::remove(attach_probe_path, ec);
  ec.clear();
  std::filesystem::remove(progress_path, ec);
  ec.clear();
  std::filesystem::remove(reflection_probe_path, ec);
  ec.clear();
  std::filesystem::remove(log_path, ec);
  ec.clear();
  std::filesystem::remove(render_preview_output_path, ec);
  ec.clear();
  const auto append_progress = [&](const std::string& line) {
    std::ofstream progress_file(progress_path, std::ios::binary | std::ios::app);
    if (progress_file) {
      progress_file << line << '\n';
    }
  };
  const auto append_reflection_probe = [&](const std::string& line) {
    std::ofstream probe_file(reflection_probe_path, std::ios::binary | std::ios::app);
    if (probe_file) {
      probe_file << line << '\n';
    }
  };
  append_progress("runner.begin");
  std::ofstream log_file(log_path, std::ios::binary | std::ios::trunc);
  if (!log_file) {
    throw std::runtime_error("Failed to open algorithm runner log file: " + log_path.string());
  }
  std::streambuf* const original_cout_buffer = std::cout.rdbuf(log_file.rdbuf());
  std::streambuf* const original_cerr_buffer = std::cerr.rdbuf(log_file.rdbuf());

  DebugToolBackendRuntime runtime;
  std::vector<std::byte> last_recorded_preview_rgba{};
  ImVec2 last_recorded_preview_size{};
  append_progress("runtime.created");
  append_progress("runtime_init_begin");
  if (!runtime.Init("debugToolRunner", 1280, 720)) {
    throw std::runtime_error("DebugToolBackendRuntime init failed in algorithm runner mode.");
  }
  append_progress("runtime_init_end");
  append_progress("runtime.initialized");

  if (options.export_swapchain_gif) {
    runtime.runtime_environment().SetDrawCallback([&runtime]() {
      if (runtime.has_render_preview_texture()) {
        ImGui::GetBackgroundDrawList()->AddImage(
          runtime.render_preview_texture_id(),
          ImVec2(0.0f, 0.0f),
          ImVec2(1280.0f, 720.0f));
      }
    });
  } else {
    runtime.runtime_environment().SetDrawCallback([]() {});
  }
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
  if (is_pipeline) {
    throw std::runtime_error(
      "Algorithm runner requires a normal algorithm, but '" + options.algorithm_name + "' is pipeline.");
  }

  std::vector<debug_tool::AlgorithmResourceBinding> resource_bindings;
  std::vector<debug_tool::AlgorithmDescriptorValue> descriptor_values;
  bool has_default_file = false;
  append_progress("default_bindings_begin");
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
  append_progress("default_bindings_end");
  append_progress("default_bindings_loaded");

  size_t mounted_algorithm_index = 0u;
  debug_tool::DebugCommandResult mount_result{};
  if (!debug_tool::DebugCmd::Execute(
        runtime,
        debug_tool::DebugCommand{
          .id = debug_tool::DebugCommandId::AttachAlgorithm,
          .algorithm_name = options.algorithm_name,
          .mount_mode = debug_tool::AlgorithmMountMode::Direct,
          .execution_preference = options.execution_preference,
          .resource_bindings = resource_bindings,
          .descriptor_values = descriptor_values,
        },
        &mount_result)) {
    throw std::runtime_error(
      mount_result.message.empty()
        ? ("Failed to mount algorithm '" + options.algorithm_name + "'.")
        : mount_result.message);
  }
  mounted_algorithm_index = mount_result.algorithm_index;
  append_progress("algorithm_mounted");

  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::SetRenderPreviewExtent,
        .preview_extent = ImVec2(
          static_cast<float>(options.preview_width),
          static_cast<float>(options.preview_height)),
      }, nullptr)) {
    throw std::runtime_error("Failed to set algorithm preview extent.");
  }
  append_progress("preview_extent_set");

  runtimesys::RenderPreviewRequest tick_preview_request{};
  if (!runtime.BuildRenderPreviewRequest(
        0u,
        mounted_algorithm_index,
        &tick_preview_request,
        &error_message) ||
      !tick_preview_request.valid) {
    throw std::runtime_error(
      error_message.empty()
        ? "Render preview request is invalid before algorithm ticking."
        : error_message);
  }
  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::SetRenderPreviewRequest,
        .preview_request = std::move(tick_preview_request),
      }, nullptr)) {
    throw std::runtime_error("Failed to set algorithm preview request before ticking.");
  }
  append_progress("preview_request_set_before_ticks");

  if (!runtime.runtime_environment().Tick()) {
    throw std::runtime_error("Runtime environment failed before algorithm ticking.");
  }
  append_progress("preview_frame_rendered_before_ticks");

  append_progress("start_ticking_begin");
  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::StartTick,
      }, nullptr)) {
    throw std::runtime_error("Failed to start algorithm ticking.");
  }
  append_progress("start_ticking_end");
  append_progress("ticking_started");

  double gif_engine_frequency_hz = static_cast<double>(common_data::DefaultAgentLimitFpsFlag());
  if (options.export_swapchain_gif) {
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    if (!runtime.Tick()) {
      throw std::runtime_error("Runtime environment failed during GIF sampling warm-up tick.");
    }
    append_progress("gif_sampling_warmup_complete");
    gif_engine_frequency_hz = _MeasureGifSamplingFrequency(runtime);
    append_progress("gif_sampling_frequency_measured");
    runtimesys::RenderPreviewRequest gif_preview_request{};
    if (!runtime.BuildRenderPreviewRequest(
          0u,
          mounted_algorithm_index,
          &gif_preview_request,
          &error_message) ||
        !gif_preview_request.valid) {
      throw std::runtime_error(
        error_message.empty()
          ? "Render preview request is invalid after GIF sampling warm-up."
          : error_message);
    }
    if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
          .id = debug_tool::DebugCommandId::SetRenderPreviewRequest,
          .preview_request = std::move(gif_preview_request),
        }, nullptr)) {
      throw std::runtime_error("Failed to update render preview request before GIF recording.");
    }
    runtime.BeginDebugToolRecording();
    append_progress("debug_tool_recording_started");
  }

  std::cout
    << "algorithm_runner.begin algorithm=" << options.algorithm_name
    << " execution=" << _ExecutionPreferenceName(options.execution_preference)
    << " ticks=" << (options.export_swapchain_gif ? 0u : options.ticks)
    << " preview=" << options.preview_width << 'x' << options.preview_height
    << " defaults=" << (has_default_file ? "true" : "false")
    << " swapchain_gif=" << (options.export_swapchain_gif ? "true" : "false")
    << " gif_duration_seconds=" << (options.export_swapchain_gif ? options.swapchain_gif_duration_seconds : 0.0)
    << '\n';

  const uint32_t runner_tick_count = options.ticks;
  uint32_t gif_total_tick_count = static_cast<uint32_t>(std::ceil(
    options.swapchain_gif_duration_seconds *
    gif_engine_frequency_hz));
  if (gif_total_tick_count == 0u) {
    gif_total_tick_count = 1u;
  }

  for (uint32_t tick_index = 0u;
       options.export_swapchain_gif
         ? tick_index < gif_total_tick_count
         : tick_index < runner_tick_count;
       ++tick_index) {
    append_progress("tick_loop_begin_" + std::to_string(tick_index + 1u));
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    if (!runtime.Tick()) {
      throw std::runtime_error(
        runtime.ui_status_message().empty()
          ? "Algorithm runner tick failed."
          : runtime.ui_status_message());
    }
    append_progress("tick_complete_" + std::to_string(tick_index + 1u));

    if (options.export_swapchain_gif) {
      runtimesys::RenderPreviewRequest gif_preview_request{};
      if (!runtime.BuildRenderPreviewRequest(
            0u,
            mounted_algorithm_index,
            &gif_preview_request,
            &error_message) ||
          !gif_preview_request.valid) {
        throw std::runtime_error(
          error_message.empty()
            ? "Render preview request is invalid during GIF recording."
            : error_message);
      }
      if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
            .id = debug_tool::DebugCommandId::SetRenderPreviewRequest,
            .preview_request = std::move(gif_preview_request),
          }, nullptr)) {
        throw std::runtime_error("Failed to update render preview request during GIF recording.");
      }
    }

    if (!options.export_swapchain_gif) {
      std::vector<std::byte> frame_rgba{};
      ImVec2 frame_size{};
      if (!runtime.runtime_environment().ReadbackRenderPreviewTexture(&frame_rgba, &frame_size)) {
        throw std::runtime_error(
          "Failed to read back algorithm preview frame " + std::to_string(tick_index + 1u) + ".");
      }
      const uint32_t frame_width = static_cast<uint32_t>(frame_size.x);
      const uint32_t frame_height = static_cast<uint32_t>(frame_size.y);
      const uint64_t frame_hash = _HashBytes(frame_rgba);
      const size_t frame_pixel_count =
        static_cast<size_t>(frame_width) * static_cast<size_t>(frame_height);
      size_t frame_non_empty_pixel_count = 0u;
      for (size_t pixel_index = 0u; pixel_index < frame_pixel_count; ++pixel_index) {
        const size_t byte_index = pixel_index * 4u;
        if (frame_rgba[byte_index + 0u] != std::byte{0} ||
            frame_rgba[byte_index + 1u] != std::byte{0} ||
            frame_rgba[byte_index + 2u] != std::byte{0} ||
            frame_rgba[byte_index + 3u] != std::byte{0}) {
          ++frame_non_empty_pixel_count;
        }
      }
      if (tick_index < 2u &&
          !_WritePngImage(
            _BuildPipelineFramePreviewPath(render_preview_output_path, tick_index + 1u),
            frame_rgba,
            frame_width,
            frame_height)) {
        throw std::runtime_error(
          "Failed to write algorithm preview frame " + std::to_string(tick_index + 1u) + ".");
      }
      std::cout
        << "  frame[" << (tick_index + 1u) << "] hash=0x"
        << std::hex << frame_hash << std::dec
        << " pixels=" << frame_non_empty_pixel_count
        << " output="
        << _BuildPipelineFramePreviewPath(render_preview_output_path, tick_index + 1u).string()
        << '\n';
    }

    debug_tool::AgentRuntimeSummary agent_summary{};
    if (!runtime.GetAgentSummary(0u, &agent_summary)) {
      throw std::runtime_error("Failed to collect agent summary after algorithm tick.");
    }
#if ALGOFORGE_VERBOSE_RUNTIME_LOGGING
    std::cout << "[tick " << (tick_index + 1u) << "] algorithm_count=" << agent_summary.algorithms.size() << '\n';
    for (const debug_tool::AlgorithmRuntimeSummary& summary : agent_summary.algorithms) {
      std::cout
        << "  algorithm " << summary.algorithm_name
        << " state=" << _AssemblyStateName(summary.assembly_state)
        << " exec=" << _ExecutionPreferenceName(summary.execution_preference)
        << " jobs_symbol=" << (summary.jobs_symbol ? "true" : "false")
        << " vk_symbol=" << (summary.vk_symbol ? "true" : "false")
        << " active_bundle_valid=" << (summary.pipeline_active_bundle_valid ? "true" : "false");
      if (summary.pipeline_active_bundle_valid) {
        std::cout
          << " active_bundle_begin=" << summary.pipeline_active_bundle_begin_stage_index
          << " active_bundle_count=" << summary.pipeline_active_bundle_stage_count
          << " active_bundle_exec=" << _ExecutionPreferenceName(summary.pipeline_active_bundle_preference);
      }
      std::cout
        << '\n';
      _PrintReflectionSnapshotPresence("reflection", summary.reflection_snapshot);
      _PrintReflectionSnapshot(summary.reflection_snapshot);
      if (!summary.intervention_phase_summaries.empty()) {
        std::cout << "    phase_count=" << summary.intervention_phase_summaries.size() << '\n';
        for (const debug_tool::AlgorithmPhaseSummary& phase_summary : summary.intervention_phase_summaries) {
          std::cout
            << "      phase " << (phase_summary.phase_name.empty() ? "<phase>" : phase_summary.phase_name)
            << " kind=" << _PhaseKindName(phase_summary.phase_kind)
            << " exec=" << _ExecutionPreferenceName(phase_summary.execution_preference)
            << '\n';
          if (!phase_summary.functions.empty()) {
            std::cout << "        functions=";
            for (size_t i = 0u; i < phase_summary.functions.size(); ++i) {
              if (i > 0u) {
                std::cout << ',';
              }
              std::cout << phase_summary.functions[i];
            }
            std::cout << '\n';
          }
          if (!phase_summary.used_algorithm_containers.empty()) {
            std::cout << "        containers=" << phase_summary.used_algorithm_containers.size() << '\n';
            for (const algomanager::bridge::AlgorithmPhaseContainerBinding& binding :
                 phase_summary.used_algorithm_containers) {
              std::cout
                << "          " << binding.container_name
                << " kind=" << binding.container_kind
                << " required=" << (binding.required ? "true" : "false")
                << '\n';
            }
          }
          if (!phase_summary.vertex_shader_path.empty() || !phase_summary.fragment_shader_path.empty()) {
            std::cout
              << "        shaders vertex="
              << (phase_summary.vertex_shader_path.empty() ? "<none>" : phase_summary.vertex_shader_path)
              << " fragment="
              << (phase_summary.fragment_shader_path.empty() ? "<none>" : phase_summary.fragment_shader_path)
              << '\n';
          }
        }
      }
    }
#endif
  }

  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::PauseTick,
      }, nullptr)) {
    throw std::runtime_error("Failed to pause algorithm ticking.");
  }
  append_progress("ticking_paused");

  if (options.export_swapchain_gif) {
    std::vector<runtimesys::DebugToolRecordedFrame> recorded_frames =
      runtime.EndDebugToolRecording();
    append_progress("debug_tool_recording_stopped");
    const uint64_t recorded_tick_count = runtime.DebugToolRecordingTickCount();
    debug_tool::GifWriter gif_writer{};
    uint32_t gif_frame_count = 0u;
    const uint16_t frame_delay_centiseconds = static_cast<uint16_t>(std::clamp(
      static_cast<long>(std::lround(100.0 / gif_engine_frequency_hz)),
      1l,
      65535l));
    for (runtimesys::DebugToolRecordedFrame& frame : recorded_frames) {
      if (gif_frame_count == 0u) {
        gif_writer.Open(
          options.swapchain_gif_output_path,
          frame.width,
          frame.height,
          frame_delay_centiseconds);
      }
      gif_writer.WriteRgbaFrame(frame.rgba, frame_delay_centiseconds);
      ++gif_frame_count;
    }
    if (gif_frame_count > 0u) {
      gif_writer.Close();
      last_recorded_preview_rgba = std::move(recorded_frames.back().rgba);
      last_recorded_preview_size = ImVec2(
        static_cast<float>(recorded_frames.back().width),
        static_cast<float>(recorded_frames.back().height));
    }
    std::cout
      << "swapchain_gif.end path=" << options.swapchain_gif_output_path
      << " frames=" << gif_frame_count
      << " scheduler_ticks=" << recorded_tick_count
      << " duration_seconds=" << (
        static_cast<double>(gif_frame_count) *
        std::max(0.01, 1.0 / gif_engine_frequency_hz))
      << " effective_frequency_hz=" << gif_engine_frequency_hz
      << " source=backend.offscreen_preview_texture.recording_buffer\n";
    append_progress("swapchain_gif_closed");
  }

  runtimesys::RenderPreviewRequest preview_request{};
  if (!runtime.BuildRenderPreviewRequest(0u, mounted_algorithm_index, &preview_request, &error_message)) {
    throw std::runtime_error(
      error_message.empty()
        ? "Failed to build render preview request for mounted algorithm."
        : error_message);
  }
  if (!preview_request.valid) {
    throw std::runtime_error("Render preview request is invalid after algorithm execution.");
  }
  if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
        .id = debug_tool::DebugCommandId::SetRenderPreviewRequest,
        .preview_request = std::move(preview_request),
      }, nullptr)) {
    throw std::runtime_error("Failed to set algorithm preview request.");
  }
  append_progress("preview_request_set");

  if (!runtime.runtime_environment().Tick()) {
    throw std::runtime_error("Runtime environment failed while rendering the preview frame.");
  }
  append_progress("preview_frame_rendered");

  if (!runtime.has_render_preview_texture()) {
    throw std::runtime_error(
      "Render preview texture was not created. summary=" + runtime.render_preview_debug_summary());
  }

  if (options.display_window) {
    if (!debug_tool::DebugCmd::Execute(runtime, debug_tool::DebugCommand{
          .id = debug_tool::DebugCommandId::StartTick,
        }, nullptr)) {
      throw std::runtime_error("Failed to restart algorithm ticking for the render preview window.");
    }
    append_progress("preview_window_ticking_started");
    DebugToolFrontendPanel ui_panel;
    runtime.runtime_environment().SetDrawCallback([&]() {
      ui_panel.DrawRenderPreviewOnly(runtime);
    });
    while (runtime.Tick()) {
    }
    ui_panel.Destroy();
    std::cout.rdbuf(original_cout_buffer);
    std::cerr.rdbuf(original_cerr_buffer);
    append_progress("runner.completed");
    runtime.Destroy();
    append_progress("runtime.destroyed");
    return true;
  }

  std::vector<std::byte> preview_rgba{};
  ImVec2 preview_size{};
  if (options.export_swapchain_gif && !last_recorded_preview_rgba.empty()) {
    preview_rgba = std::move(last_recorded_preview_rgba);
    preview_size = last_recorded_preview_size;
  } else {
    if (!runtime.runtime_environment().ReadbackRenderPreviewTexture(&preview_rgba, &preview_size)) {
      throw std::runtime_error(
        "Failed to read back render preview texture. summary=" + runtime.render_preview_debug_summary());
    }
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
  if (!_WritePngImage(render_preview_output_path, preview_rgba, preview_width, preview_height)) {
    throw std::runtime_error(
      "Failed to write render preview image: " + render_preview_output_path.string());
  }
  append_progress("preview_image_written");

  std::cout
    << "algorithm_runner.end"
    << " preview_pixels=" << non_empty_pixel_count
    << " render_preview_path=" << render_preview_output_path.string()
    << " preview_summary=" << runtime.render_preview_debug_summary();
  std::cout << '\n';
  std::cout.flush();
  std::cout.rdbuf(original_cout_buffer);
  std::cerr.rdbuf(original_cerr_buffer);
  append_progress("runner.completed");
  std::cerr << "algorithm_runner.log=" << log_path.string() << '\n';
  runtime.Destroy();
  append_progress("runtime.destroyed");
  return true;
}

std::filesystem::path _RunnerControlEndpointFilePath() {
  return algorithm::library_paths::ResolveTestDataRoot() / "runner_control" / "endpoint.txt";
}

std::filesystem::path _RunnerControlLogPath(const char* file_name) {
  return algorithm::library_paths::ResolveTestDataRoot() / "runner_control" / file_name;
}

void _AppendRunnerControlLog(const char* file_name, const std::string& line) {
  const std::filesystem::path path = _RunnerControlLogPath(file_name);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

std::string _ReadRunnerEndpointFromFile() {
  const std::filesystem::path endpoint_path = _RunnerControlEndpointFilePath();
  std::ifstream file(endpoint_path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::string endpoint{};
  std::getline(file, endpoint);
  return endpoint;
}

std::string _ResolveRunnerEndpointText(const std::string& requested_endpoint) {
  if (!requested_endpoint.empty()) {
    debug_tool_backend::runner_control::Endpoint requested{};
    if (debug_tool_backend::runner_control::ParseEndpoint(requested_endpoint, &requested)) {
      if (requested.port != 0u) {
        return requested_endpoint;
      }
      const std::string file_endpoint = _ReadRunnerEndpointFromFile();
      if (!file_endpoint.empty()) {
        return file_endpoint;
      }
      const char* env_endpoint = std::getenv("DEBUGTOOL_RUNNER_ENDPOINT");
      if (env_endpoint && *env_endpoint) {
        return env_endpoint;
      }
      return requested_endpoint;
    }
  }
  const char* env_endpoint = std::getenv("DEBUGTOOL_RUNNER_ENDPOINT");
  if (env_endpoint && *env_endpoint) {
    return env_endpoint;
  }
  const std::string file_endpoint = _ReadRunnerEndpointFromFile();
  if (!file_endpoint.empty()) {
    return file_endpoint;
  }
  return requested_endpoint.empty() ? std::string("127.0.0.1:0") : requested_endpoint;
}

std::string _BuildRunnerRequestLine(int argc, char** argv) {
  std::ostringstream stream{};
  bool first_token = true;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i] ? argv[i] : "";
    if (argument == "--runner-endpoint") {
      ++i;
      continue;
    }
    if (!first_token) {
      stream << ' ';
    }
    first_token = false;
    stream << debug_tool_backend::runner_control::QuoteToken(argument);
  }
  return stream.str();
}

std::string _ExecuteRunnerRequestLine(const std::string& request_line, bool* out_shutdown_requested) {
  if (out_shutdown_requested) {
    *out_shutdown_requested = false;
  }

  _AppendRunnerControlLog("server.log", "execute.begin request=" + request_line);
  std::cout << "runner_server.request=" << request_line << '\n';
  std::cout.flush();

  if (request_line == "shutdown" || request_line == "--runner-shutdown") {
    _AppendRunnerControlLog("server.log", "execute.shutdown");
    if (out_shutdown_requested) {
      *out_shutdown_requested = true;
    }
    return "OK shutdown";
  }

  std::vector<std::string> tokens = debug_tool_backend::runner_control::TokenizeCommandLine(request_line);
  if (tokens.empty()) {
    _AppendRunnerControlLog("server.log", "execute.empty_tokens");
    return "ERR empty request";
  }

  std::vector<char*> argv{};
  argv.reserve(tokens.size() + 1u);
  std::string request_program_name = "runner-control";
  argv.push_back(request_program_name.data());
  for (std::string& token : tokens) {
    argv.push_back(token.data());
  }

  PipelineRunnerOptions pipeline_options{};
  std::string error_message;
  _AppendRunnerControlLog("server.log", "execute.pipeline_probe_begin");
  if (_ParsePipelineRunnerOptions(
        static_cast<int>(argv.size()),
        argv.data(),
        &pipeline_options,
        &error_message) &&
      pipeline_options.enabled) {
    try {
      _AppendRunnerControlLog("server.log", "execute.pipeline_run_begin");
      const bool executed = _RunPipelineRunner(pipeline_options);
      _AppendRunnerControlLog(
        "server.log",
        std::string("execute.pipeline_run_end executed=") + (executed ? "true" : "false"));
      return executed ? "OK pipeline_runner" : "ERR pipeline_runner failed";
    } catch (const std::exception& e) {
      _AppendRunnerControlLog("server.log", std::string("execute.pipeline_exception=") + e.what());
      return std::string("ERR ") + e.what();
    }
  }

  AlgorithmRunnerOptions algorithm_options{};
  error_message.clear();
  _AppendRunnerControlLog("server.log", "execute.algorithm_probe_begin");
  if (_ParseAlgorithmRunnerOptions(
        static_cast<int>(argv.size()),
        argv.data(),
        &algorithm_options,
        &error_message) &&
      algorithm_options.enabled) {
    try {
      _AppendRunnerControlLog("server.log", "execute.algorithm_run_begin");
      const bool executed = _RunAlgorithmRunner(algorithm_options);
      _AppendRunnerControlLog(
        "server.log",
        std::string("execute.algorithm_run_end executed=") + (executed ? "true" : "false"));
      return executed ? "OK algorithm_runner" : "ERR algorithm_runner failed";
    } catch (const std::exception& e) {
      _AppendRunnerControlLog("server.log", std::string("execute.algorithm_exception=") + e.what());
      return std::string("ERR ") + e.what();
    }
  }

  if (!error_message.empty()) {
    _AppendRunnerControlLog("server.log", "execute.parse_error=" + error_message);
    return "ERR " + error_message;
  }
  _AppendRunnerControlLog("server.log", "execute.unrecognized");
  return "ERR runner request did not contain a recognized command.";
}

bool _RunRunnerControlServer(const RunnerServerOptions& options) {
  const std::string endpoint_text = _ResolveRunnerEndpointText(options.runner_endpoint);
  debug_tool_backend::runner_control::Endpoint endpoint{};
  if (!debug_tool_backend::runner_control::ParseEndpoint(endpoint_text, &endpoint)) {
    throw std::runtime_error("Invalid runner control endpoint: " + endpoint_text);
  }

  const std::filesystem::path endpoint_path = _RunnerControlEndpointFilePath();
  std::error_code ec;
  std::filesystem::create_directories(endpoint_path.parent_path(), ec);
  ec.clear();
  std::ofstream endpoint_file(endpoint_path, std::ios::binary | std::ios::trunc);
  if (!endpoint_file) {
    throw std::runtime_error("Failed to open runner control endpoint file: " + endpoint_path.string());
  }

  const auto handler = [&](const std::string& request, bool* out_shutdown_requested) -> std::string {
    try {
      _AppendRunnerControlLog("server.log", "request=" + request);
      bool request_shutdown = false;
      const std::string response = _ExecuteRunnerRequestLine(request, &request_shutdown);
      _AppendRunnerControlLog("server.log", "handler.after_execute response=" + response);
      if (out_shutdown_requested) {
        *out_shutdown_requested = request_shutdown || options.once;
      }
      _AppendRunnerControlLog("server.log", "response=" + response);
      return response;
    } catch (const std::exception& e) {
      const std::string response = std::string("ERR ") + e.what();
      _AppendRunnerControlLog("server.log", "exception=" + response);
      if (out_shutdown_requested) {
        *out_shutdown_requested = options.once;
      }
      return response;
    }
  };

  const auto on_bound_endpoint = [&](const debug_tool_backend::runner_control::Endpoint& bound_endpoint) {
    endpoint_file << debug_tool_backend::runner_control::FormatEndpoint(bound_endpoint) << '\n';
    endpoint_file.flush();
    _AppendRunnerControlLog("server.log", "runner_server.begin endpoint=" + debug_tool_backend::runner_control::FormatEndpoint(bound_endpoint));
    std::cout << "runner_server.begin endpoint=" << debug_tool_backend::runner_control::FormatEndpoint(bound_endpoint) << '\n';
    std::cout.flush();
  };

  const bool served = debug_tool_backend::runner_control::RunServer(endpoint, handler, on_bound_endpoint);
  if (!served) {
    return false;
  }

  if (options.once) {
    _AppendRunnerControlLog("server.log", "runner_server.once_completed");
    std::cout << "runner_server.once_completed\n";
    std::cout.flush();
  }
  return true;
}

bool _RunRunnerControlClient(
  int argc,
  char** argv,
  const std::string& requested_endpoint,
  std::string* out_error_message) {
  const std::string endpoint_text = _ResolveRunnerEndpointText(requested_endpoint);
  debug_tool_backend::runner_control::Endpoint endpoint{};
  if (!debug_tool_backend::runner_control::ParseEndpoint(endpoint_text, &endpoint)) {
    if (out_error_message) {
      *out_error_message = "Invalid runner control endpoint: " + endpoint_text;
    }
    return false;
  }

  const std::string request = _BuildRunnerRequestLine(argc, argv);
  _AppendRunnerControlLog("client.log", "request=" + request);
  std::cout << "runner_client.begin endpoint=" << debug_tool_backend::runner_control::FormatEndpoint(endpoint) << '\n';
  std::cout << "runner_client.request=" << request << '\n';
  std::cout.flush();
  std::string response{};
  if (!debug_tool_backend::runner_control::SendCommand(endpoint, request, &response, out_error_message)) {
    if (out_error_message && !out_error_message->empty()) {
      _AppendRunnerControlLog("client.log", "error=" + *out_error_message);
    }
    return false;
  }

  _AppendRunnerControlLog("client.log", "response=" + response);
  if (!response.empty()) {
    std::cout << response;
    if (response.back() != '\n') {
      std::cout << '\n';
    }
  }
  if (response.rfind("ERR", 0u) == 0u) {
    if (out_error_message) {
      *out_error_message = response;
    }
    return false;
  }
  return true;
}

std::filesystem::path _PreviewRenderEndpointFilePath() {
  return algorithm::library_paths::ResolveTestDataRoot() / "runner_control" / "preview_render_endpoint.txt";
}

bool _RunPreviewRenderServer(const PreviewRenderServerOptions& options) {
  const std::filesystem::path preview_render_probe_path =
    algomanager::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() / "preview_render_server_probe.log";
  std::error_code probe_ec{};
  std::filesystem::create_directories(preview_render_probe_path.parent_path(), probe_ec);
  const auto append_preview_render_probe = [&](const std::string& line) {
    std::ofstream file(preview_render_probe_path, std::ios::binary | std::ios::app);
    if (file) {
      file << line << '\n';
    }
  };
  append_preview_render_probe("preview_render_server.begin");
  bool is_pipeline = false;
  std::string query_error_message;
  DebugToolBackendRuntime type_probe_runtime;
  append_preview_render_probe("preview_render_server.type_probe.begin");
  if (!type_probe_runtime.IsPipelineAlgorithm(
        options.algorithm_name,
        &is_pipeline,
        &query_error_message)) {
    append_preview_render_probe("preview_render_server.type_probe.failed");
    throw std::runtime_error(
      query_error_message.empty()
        ? ("Failed to query algorithm type for '" + options.algorithm_name + "'.")
        : query_error_message);
  }
  append_preview_render_probe(
    std::string("preview_render_server.type_probe.end pipeline=") + (is_pipeline ? "true" : "false"));

  const bool rendered = is_pipeline
    ? _RunPipelineRunner(
        PipelineRunnerOptions{
          .enabled = true,
          .display_window = options.display_window,
          .algorithm_name = options.algorithm_name,
          .ticks = options.ticks,
          .preview_width = options.preview_width,
          .preview_height = options.preview_height,
          .runner_endpoint = options.runner_endpoint,
          .render_preview_output_path = options.render_preview_output_path,
          .execution_preference = options.execution_preference,
        })
    : _RunAlgorithmRunner(
        AlgorithmRunnerOptions{
          .enabled = true,
          .display_window = options.display_window,
          .algorithm_name = options.algorithm_name,
          .ticks = options.ticks,
          .preview_width = options.preview_width,
          .preview_height = options.preview_height,
          .runner_endpoint = options.runner_endpoint,
          .render_preview_output_path = options.render_preview_output_path,
          .execution_preference = options.execution_preference,
        });
  append_preview_render_probe(
    std::string("preview_render_server.run.end rendered=") + (rendered ? "true" : "false"));
  if (options.display_window) {
    return rendered;
  }
  if (!rendered) {
    return false;
  }

  const std::filesystem::path render_preview_path(options.render_preview_output_path);
  const std::vector<std::byte> preview_bytes = _ReadBinaryFile(render_preview_path);
  if (preview_bytes.empty()) {
    append_preview_render_probe("preview_render_server.preview_bytes.empty");
    throw std::runtime_error("Preview render output is empty: " + render_preview_path.string());
  }

  const std::filesystem::path endpoint_path = _PreviewRenderEndpointFilePath();
  std::error_code ec;
  std::filesystem::create_directories(endpoint_path.parent_path(), ec);
  ec.clear();
  std::ofstream endpoint_file(endpoint_path, std::ios::binary | std::ios::trunc);
  if (!endpoint_file) {
    throw std::runtime_error("Failed to open preview render endpoint file: " + endpoint_path.string());
  }

  debug_tool_backend::runner_control::Endpoint endpoint{};
  if (!debug_tool_backend::runner_control::ParseEndpoint(options.runner_endpoint, &endpoint)) {
    throw std::runtime_error("Invalid preview render endpoint: " + options.runner_endpoint);
  }

  const auto handler =
    [preview_bytes](const std::string& request, bool* out_shutdown_requested) -> debug_tool_backend::runner_control::BinaryResponse {
      if (request == "shutdown" || request == "--runner-shutdown") {
        if (out_shutdown_requested) {
          *out_shutdown_requested = true;
        }
        return {"OK shutdown", {}};
      }
      if (request != "frame" && request != "preview-frame" && request != "render-preview") {
        return {"ERR unsupported request", {}};
      }
      return {
        "OK frame bytes=" + std::to_string(preview_bytes.size()),
        preview_bytes,
      };
    };

  const auto on_bound_endpoint = [&](const debug_tool_backend::runner_control::Endpoint& bound_endpoint) {
    endpoint_file << debug_tool_backend::runner_control::FormatEndpoint(bound_endpoint) << '\n';
    endpoint_file.flush();
    _AppendRunnerControlLog(
      "preview_render_server.log",
      "preview_render_server.begin endpoint=" +
        debug_tool_backend::runner_control::FormatEndpoint(bound_endpoint));
    std::cout
      << "preview_render_server.begin endpoint="
      << debug_tool_backend::runner_control::FormatEndpoint(bound_endpoint)
      << '\n';
    std::cout.flush();
  };

  const bool served = debug_tool_backend::runner_control::RunBinaryServer(
    endpoint,
    handler,
    on_bound_endpoint);
  if (!served) {
    return false;
  }

  _AppendRunnerControlLog("preview_render_server.log", "preview_render_server.stopped");
  std::cout << "preview_render_server.stopped\n";
  std::cout.flush();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  try {
#ifdef _WIN32
    _InstallCrashDumpHandler();
#endif
    {
      const std::filesystem::path main_entry_probe_path =
        algomanager::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() / "main_entry_probe.log";
      std::error_code ec;
      std::filesystem::create_directories(main_entry_probe_path.parent_path(), ec);
      std::ofstream probe_file(main_entry_probe_path, std::ios::binary | std::ios::app);
      if (probe_file) {
        probe_file << "main.begin argc=" << argc << '\n';
      }
    }
    if (_IsRunnerServerInvocation(argc, argv)) {
      RunnerServerOptions runner_server_options{};
      std::string runner_server_parse_error;
      if (!_ParseRunnerServerOptions(argc, argv, &runner_server_options, &runner_server_parse_error)) {
        if (!runner_server_parse_error.empty()) {
          throw std::runtime_error(runner_server_parse_error);
        }
        return 0;
      }
      if (runner_server_options.enabled) {
        return _RunRunnerControlServer(runner_server_options) ? 0 : 1;
      }
    }

    if (_IsPreviewRenderServerInvocation(argc, argv) || _IsPreviewWindowInvocation(argc, argv)) {
      PreviewRenderServerOptions preview_render_server_options{};
      std::string preview_render_server_parse_error;
      if (!_ParsePreviewRenderServerOptions(
            argc,
            argv,
            &preview_render_server_options,
            &preview_render_server_parse_error)) {
        if (!preview_render_server_parse_error.empty()) {
          throw std::runtime_error(preview_render_server_parse_error);
        }
        return 0;
      }
      if (preview_render_server_options.enabled) {
        return _RunPreviewRenderServer(preview_render_server_options) ? 0 : 1;
      }
    }

    {
      std::filesystem::create_directories(
        algomanager::ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot());
      std::filesystem::create_directories(
        algomanager::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot());
      const std::filesystem::path argv_probe_root =
        _IsPipelineRunnerInvocation(argc, argv)
          ? algomanager::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot()
          : algomanager::ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot();
      std::filesystem::create_directories(argv_probe_root);
      std::ofstream probe_file(
        (argv_probe_root / "argv_probe.log"),
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
      return _RunRunnerControlClient(argc, argv, runner_options.runner_endpoint, &runner_parse_error) ? 0 : 1;
    }

    AlgorithmRunnerOptions algorithm_options{};
    std::string algorithm_parse_error;
    if (!_ParseAlgorithmRunnerOptions(argc, argv, &algorithm_options, &algorithm_parse_error)) {
      if (!algorithm_parse_error.empty()) {
        throw std::runtime_error(algorithm_parse_error);
      }
      return 0;
    }
    if (algorithm_options.enabled) {
      return _RunRunnerControlClient(argc, argv, algorithm_options.runner_endpoint, &algorithm_parse_error) ? 0 : 1;
    }

    DebugToolBackendRuntime runtime;
    DebugToolFrontendPanel ui_panel;
    const std::filesystem::path gui_debug_log_path =
      std::filesystem::path("testData") / "debugTool_gui.log";
    std::filesystem::create_directories(gui_debug_log_path.parent_path());
    std::ofstream gui_debug_log(gui_debug_log_path, std::ios::binary | std::ios::trunc);
    std::streambuf* const original_cerr_buffer = std::cerr.rdbuf(gui_debug_log.rdbuf());
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
    std::cerr.rdbuf(original_cerr_buffer);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "debugTool error: " << e.what() << '\n';
    return 1;
  }
}
