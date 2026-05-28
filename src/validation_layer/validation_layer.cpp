#include "validation_layer.h"
#include "validation_script.h"

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <iomanip>
#include <ios>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string ReadPipeMessage(HANDLE pipe) {
  std::string request;
  std::array<char, 4096> buffer{};
  DWORD bytes_read = 0;
  BOOL ok = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr);
  if (!ok) {
    DWORD error = GetLastError();
    if (error == ERROR_MORE_DATA && bytes_read > 0) {
      request.append(buffer.data(), buffer.data() + bytes_read);
      return request;
    }
    return {};
  }
  request.append(buffer.data(), buffer.data() + bytes_read);
  return request;
}

bool WritePipeMessage(HANDLE pipe, const std::string& response) {
  DWORD bytes_written = 0;
  return WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytes_written, nullptr) &&
    bytes_written == response.size();
}

bool BuildPipeSecurityAttributes(SECURITY_ATTRIBUTES& attributes, PSECURITY_DESCRIPTOR& descriptor) {
  descriptor = nullptr;
  constexpr const wchar_t* kLocalPipeAcl = L"D:(A;;GA;;;WD)";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kLocalPipeAcl, SDDL_REVISION_1, &descriptor, nullptr)) {
    return false;
  }
  attributes.nLength = sizeof(attributes);
  attributes.lpSecurityDescriptor = descriptor;
  attributes.bInheritHandle = FALSE;
  return true;
}

std::string TrimText(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return std::string(text.substr(begin, end - begin));
}

std::string ExtractJsonStringFieldText(std::string_view text, std::string_view key) {
  std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = text.find(needle);
  if (pos == std::string_view::npos) return {};
  pos = text.find(':', pos + needle.size());
  if (pos == std::string_view::npos) return {};
  ++pos;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
  if (pos >= text.size() || text[pos] != '"') return {};
  ++pos;
  std::string value;
  bool escaped = false;
  for (; pos < text.size(); ++pos) {
    char ch = text[pos];
    if (escaped) {
      value.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value.push_back(ch);
  }
  return {};
}

std::string ReadRequestCommand(const std::string& request) {
  std::string trimmed = TrimText(request);
  if (trimmed.empty()) return {};

  if (trimmed.front() == '{') {
    std::string cmd = ExtractJsonStringFieldText(trimmed, "cmd");
    if (!cmd.empty()) {
      return TrimText(cmd);
    }
  }

  return trimmed;
}

std::string JsonNumber(float value) {
  if (!std::isfinite(value)) return "0.0";
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

std::string JsonNumber(double value) {
  if (!std::isfinite(value)) return "0.0";
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

std::string JsonUnsigned(uint64_t value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

std::string JsonUnsigned(uint32_t value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

std::string JsonSigned(int value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

}  // namespace

ValidationLayerApi::~ValidationLayerApi() {
  Stop();
}

bool ValidationLayerApi::Start() {
  if (enabled_.load()) return true;
  stop_requested_.store(false);
  enabled_.store(true);
  worker_ = std::thread(&ValidationLayerApi::WorkerLoop, this);
  return true;
}

void ValidationLayerApi::Stop() {
  if (!enabled_.load()) return;
  stop_requested_.store(true);

  std::wstring pipe_name{kPipeName};
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (WaitNamedPipeW(pipe_name.c_str(), 50)) {
      HANDLE client = CreateFileW(
        pipe_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
      if (client != INVALID_HANDLE_VALUE) {
        const std::string shutdown_request = "{\"cmd\":\"shutdown\"}";
        DWORD bytes_written = 0;
        WriteFile(client, shutdown_request.data(), static_cast<DWORD>(shutdown_request.size()), &bytes_written, nullptr);
        CloseHandle(client);
        break;
      }
    }
    Sleep(10);
  }

  if (worker_.joinable()) {
    worker_.join();
  }
  enabled_.store(false);
}

void ValidationLayerApi::PublishFrame(const ValidationFrameSnapshot& snapshot) {
  if (!enabled_.load()) return;

  const uint64_t sequence = latest_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
  std::string json = BuildSnapshotJson(snapshot, sequence);

  {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    latest_frame_index_.store(snapshot.frame_index, std::memory_order_relaxed);
    latest_snapshot_json_ = std::move(json);
  }
}

void ValidationLayerApi::QueueAction(ValidationAction action) {
  std::lock_guard<std::mutex> lock(action_mutex_);
  pending_actions_.push_back(std::move(action));
}

void ValidationLayerApi::QueueActions(const std::vector<ValidationAction>& actions) {
  if (actions.empty()) return;
  std::lock_guard<std::mutex> lock(action_mutex_);
  pending_actions_.insert(pending_actions_.end(), actions.begin(), actions.end());
}

std::vector<ValidationAction> ValidationLayerApi::ConsumeActions() {
  std::lock_guard<std::mutex> lock(action_mutex_);
  std::vector<ValidationAction> actions;
  actions.swap(pending_actions_);
  return actions;
}

void ValidationLayerApi::RequestPhysStep(uint32_t count) {
  if (count == 0) return;
  QueueAction(ValidationAction{ValidationActionKind::PhysStep, count});
}

uint32_t ValidationLayerApi::ConsumePhysStepRequests() {
  std::lock_guard<std::mutex> lock(action_mutex_);
  uint32_t count = 0;
  std::vector<ValidationAction> remaining;
  remaining.reserve(pending_actions_.size());
  for (ValidationAction& action : pending_actions_) {
    if (action.kind == ValidationActionKind::PhysStep) {
      count += action.step_count;
    } else {
      remaining.push_back(std::move(action));
    }
  }
  pending_actions_.swap(remaining);
  return count;
}

bool ValidationLayerApi::ParseStartupFlag(const std::vector<std::string>& args) {
  bool enabled = false;
  for (size_t i = 1; i < args.size(); ++i) {
    std::string arg = args[i];
    while (!arg.empty() && (arg.front() == L'-' || arg.front() == L'/')) {
      arg.erase(arg.begin());
    }
    std::transform(arg.begin(), arg.end(), arg.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (arg == "validationlayer:on" || arg == "vaildlayer:on") {
      enabled = true;
    } else if (arg == "validationlayer:off" || arg == "vaildlayer:off") {
      enabled = false;
    }
  }
  return enabled;
}

void ValidationLayerApi::WorkerLoop() {
  while (!stop_requested_.load()) {
    SECURITY_ATTRIBUTES sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!BuildPipeSecurityAttributes(sa, sd)) {
      Sleep(50);
      continue;
    }

    HANDLE pipe = CreateNamedPipeW(
      kPipeName,
      PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
      1,
      256 * 1024,
      256 * 1024,
      0,
      &sa);

    if (sd) {
      LocalFree(sd);
      sd = nullptr;
    }

    if (pipe == INVALID_HANDLE_VALUE) {
      Sleep(50);
      continue;
    }

    BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (connected) {
      std::string request = ReadPipeMessage(pipe);
      std::string response = HandleRequest(request);
      if (!response.empty()) {
        WritePipeMessage(pipe, response);
      }
      FlushFileBuffers(pipe);
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
}

std::string ValidationLayerApi::HandleRequest(const std::string& request) {
  std::string trimmed = Trim(request);
  std::string command_text = ReadRequestCommand(trimmed);
  std::string lowered = ToLower(command_text);
  std::string first_token = ToLower(FirstToken(lowered));

  if (lowered.empty() || first_token == "snapshot") {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (latest_snapshot_json_.empty()) {
      return "{\"ok\":false,\"error\":\"snapshot not ready\"}";
    }
    return latest_snapshot_json_;
  }

  if (first_token == "health") {
    return BuildHealthJson();
  }

  if (first_token == "script") {
    std::string script_body = ExtractJsonStringField(trimmed, "script");
    if (script_body.empty()) {
      const size_t first_space = command_text.find_first_of(" \t");
      if (first_space != std::string::npos && first_space + 1 < command_text.size()) {
        script_body = command_text.substr(first_space + 1);
      }
    }
    ValidationScriptParseResult parsed = ParseValidationScript(script_body);
    if (!parsed.ok) {
      std::ostringstream out;
      out << "{\"ok\":false,\"error\":\"" << EscapeJson(parsed.error) << "\"}";
      return out.str();
    }
    QueueActions(parsed.actions);
    return BuildActionResponseJson(parsed.actions, "script");
  }

  if (first_token == "physstep" || first_token == "step") {
    ValidationScriptParseResult parsed = ParseValidationScript(command_text);
    if (!parsed.ok) {
      std::ostringstream out;
      out << "{\"ok\":false,\"error\":\"" << EscapeJson(parsed.error) << "\"}";
      return out.str();
    }
    QueueActions(parsed.actions);
    return BuildActionResponseJson(parsed.actions, "physstep");
  }

  if (first_token == "run" || first_token == "pause" || first_token == "reset" || first_token == "guide" || first_token == "guides") {
    ValidationScriptParseResult parsed = ParseValidationScript(command_text);
    if (!parsed.ok) {
      std::ostringstream out;
      out << "{\"ok\":false,\"error\":\"" << EscapeJson(parsed.error) << "\"}";
      return out.str();
    }
    QueueActions(parsed.actions);
    return BuildActionResponseJson(parsed.actions, first_token);
  }

  if (first_token == "shutdown") {
    stop_requested_.store(true);
    return "{\"ok\":true,\"shutdown\":true}";
  }

  return "{\"ok\":false,\"error\":\"unknown command\"}";
}

std::string ValidationLayerApi::BuildHealthJson() const {
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  std::ostringstream out;
  out << "{\"ok\":true"
      << ",\"enabled\":" << BoolJson(enabled_.load())
      << ",\"sequence\":" << JsonUnsigned(latest_sequence_.load(std::memory_order_relaxed))
      << ",\"frame_index\":" << JsonSigned(latest_frame_index_.load(std::memory_order_relaxed))
      << "}";
  return out.str();
}

std::string ValidationLayerApi::BuildActionResponseJson(const std::vector<ValidationAction>& actions, const std::string& command_name) const {
  std::ostringstream out;
  out << "{\"ok\":true";
  out << ",\"cmd\":\"" << EscapeJson(command_name) << "\"";
  out << ",\"queued_actions\":" << JsonUnsigned(static_cast<uint64_t>(actions.size()));
  out << ",\"actions\":[";
  for (size_t i = 0; i < actions.size(); ++i) {
    if (i > 0) out << ",";
    out << ActionJson(actions[i]);
  }
  out << "]}";
  return out.str();
}

std::string ValidationLayerApi::BuildSnapshotJson(const ValidationFrameSnapshot& snapshot, uint64_t sequence) const {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);
  out << "{";
  out << "\"schema\":\"validation_layer.snapshot.v2\"";
  out << ",\"sequence\":" << JsonUnsigned(sequence);
  out << ",\"frame_index\":" << JsonSigned(snapshot.frame_index);
  out << ",\"mode\":\"" << ModeToString(snapshot.mode) << "\"";
  out << ",\"run_state\":\"" << RunStateToString(snapshot.run_state) << "\"";
  out << ",\"guide_enabled\":" << BoolJson(snapshot.guide_enabled);
  out << ",\"guide_velocity_magnitude\":" << JsonNumber(snapshot.guide_velocity_magnitude);
  out << ",\"guide_velocity_delay_frames\":" << JsonUnsigned(snapshot.guide_velocity_delay_frames);
  out << ",\"guide_velocity_duration_frames\":" << JsonUnsigned(snapshot.guide_velocity_duration_frames);
  out << ",\"guide_force_magnitude\":" << JsonNumber(snapshot.guide_force_magnitude);
  out << ",\"guide_force_delay_frames\":" << JsonUnsigned(snapshot.guide_force_delay_frames);
  out << ",\"guide_force_duration_frames\":" << JsonUnsigned(snapshot.guide_force_duration_frames);
  out << ",\"highlighted_vertex\":" << JsonSigned(snapshot.highlighted_vertex);
  out << ",\"draw_calls\":" << JsonUnsigned(snapshot.draw_calls);
  out << ",\"frame_dt_seconds\":" << JsonNumber(snapshot.frame_dt_seconds);
  out << ",\"animation_time\":" << JsonNumber(snapshot.animation_time);

  out << ",\"selection\":{";
  out << "\"kind\":\"" << SelectionKindToString(snapshot.selection.kind) << "\"";
  out << ",\"vertex\":" << JsonSigned(snapshot.selection.vertex);
  out << ",\"triangle\":" << JsonSigned(snapshot.selection.triangle);
  out << "}";

  out << ",\"selected_vertex_position\":" << Vec3Json(snapshot.selected_vertex_position);
  out << ",\"selected_triangle_material_gpa\":" << JsonNumber(snapshot.selected_triangle_material_gpa);

  out << ",\"mesh\":{";
  out << "\"vertex_count\":" << JsonUnsigned(snapshot.mesh.vertex_count);
  out << ",\"triangle_count\":" << JsonUnsigned(snapshot.mesh.triangle_count);

  out << ",\"positions\":[";
  for (size_t i = 0; i < snapshot.mesh.positions.size(); ++i) {
    if (i > 0) out << ",";
    out << Vec3Json(snapshot.mesh.positions[i]);
  }
  out << "]";

  out << ",\"normals\":[";
  for (size_t i = 0; i < snapshot.mesh.normals.size(); ++i) {
    if (i > 0) out << ",";
    out << Vec3Json(snapshot.mesh.normals[i]);
  }
  out << "]";

  out << ",\"triangles\":[";
  for (size_t i = 0; i < snapshot.mesh.triangles.size(); ++i) {
    if (i > 0) out << ",";
    const auto& tri = snapshot.mesh.triangles[i];
    out << "[" << JsonUnsigned(tri[0]) << "," << JsonUnsigned(tri[1]) << "," << JsonUnsigned(tri[2]) << "]";
  }
  out << "]";

  out << ",\"triangle_material_gpa\":[";
  for (size_t i = 0; i < snapshot.mesh.triangle_material_gpa.size(); ++i) {
    if (i > 0) out << ",";
    out << JsonNumber(snapshot.mesh.triangle_material_gpa[i]);
  }
  out << "]";
  out << "}";

  out << ",\"physics\":{";
  out << "\"total_velocities\":[";
  for (size_t i = 0; i < snapshot.physics.total_velocities.size(); ++i) {
    if (i > 0) out << ",";
    out << MatrixJson(snapshot.physics.total_velocities[i]);
  }
  out << "]";

  out << ",\"linear_velocities\":[";
  for (size_t i = 0; i < snapshot.physics.linear_velocities.size(); ++i) {
    if (i > 0) out << ",";
    out << MatrixJson(snapshot.physics.linear_velocities[i]);
  }
  out << "]";

  out << ",\"angular_velocities\":[";
  for (size_t i = 0; i < snapshot.physics.angular_velocities.size(); ++i) {
    if (i > 0) out << ",";
    out << MatrixJson(snapshot.physics.angular_velocities[i]);
  }
  out << "]";

  out << ",\"active_velocity_guidances\":[";
  for (size_t i = 0; i < snapshot.physics.active_velocity_guidances.size(); ++i) {
    if (i > 0) out << ",";
    const ValidationVelocityGuidance& guidance = snapshot.physics.active_velocity_guidances[i];
    out << "{";
    out << "\"vertex\":" << JsonSigned(guidance.vertex);
    out << ",\"vertices\":[";
    for (size_t v = 0; v < guidance.vertices.size(); ++v) {
      if (v > 0) out << ",";
      out << JsonSigned(guidance.vertices[v]);
    }
    out << "]";
    out << ",\"hidden\":" << BoolJson(guidance.hidden);
    out << ",\"valid\":" << BoolJson(guidance.valid);
    out << ",\"start\":" << Vec3Json(guidance.start);
    out << ",\"requested_target\":" << Vec3Json(guidance.requested_target);
    out << ",\"allowed_target\":" << Vec3Json(guidance.allowed_target);
    out << ",\"total_velocity\":" << MatrixJson(guidance.total_velocity);
    out << "}";
  }
  out << "]";

  out << ",\"active_guide_velocities\":[";
  for (size_t i = 0; i < snapshot.physics.active_guide_velocities.size(); ++i) {
    if (i > 0) out << ",";
    const ValidationGuideVelocity& guide_velocity = snapshot.physics.active_guide_velocities[i];
    out << "{";
    out << "\"vertices\":[";
    for (size_t v = 0; v < guide_velocity.vertices.size(); ++v) {
      if (v > 0) out << ",";
      out << JsonSigned(guide_velocity.vertices[v]);
    }
    out << "]";
    out << ",\"hidden\":" << BoolJson(guide_velocity.hidden);
    out << ",\"valid\":" << BoolJson(guide_velocity.valid);
    out << ",\"velocity\":" << MatrixJson(guide_velocity.velocity);
    out << ",\"start_frame_offset\":" << JsonUnsigned(guide_velocity.start_frame_offset);
    out << ",\"duration_frames\":" << JsonUnsigned(guide_velocity.duration_frames);
    out << "}";
  }
  out << "]";

  out << ",\"active_guide_forces\":[";
  for (size_t i = 0; i < snapshot.physics.active_guide_forces.size(); ++i) {
    if (i > 0) out << ",";
    const ValidationGuideForce& guide_force = snapshot.physics.active_guide_forces[i];
    out << "{";
    out << "\"vertices\":[";
    for (size_t v = 0; v < guide_force.vertices.size(); ++v) {
      if (v > 0) out << ",";
      out << JsonSigned(guide_force.vertices[v]);
    }
    out << "]";
    out << ",\"hidden\":" << BoolJson(guide_force.hidden);
    out << ",\"valid\":" << BoolJson(guide_force.valid);
    out << ",\"force\":" << Vec3Json(guide_force.force);
    out << ",\"start_frame_offset\":" << JsonUnsigned(guide_force.start_frame_offset);
    out << ",\"duration_frames\":" << JsonUnsigned(guide_force.duration_frames);
    out << "}";
  }
  out << "]";

  out << ",\"recorded_frames\":[";
  for (size_t i = 0; i < snapshot.physics.recorded_frames.size(); ++i) {
    if (i > 0) out << ",";
    const ValidationRecordedFrame& frame = snapshot.physics.recorded_frames[i];
    out << "{";
    out << "\"frame_index\":" << JsonSigned(frame.frame_index);
    out << ",\"current\":" << BoolJson(frame.current);
    out << ",\"expanded\":" << BoolJson(frame.expanded);
    out << ",\"position_count\":" << JsonUnsigned(frame.position_count);
    out << ",\"total_velocity_count\":" << JsonUnsigned(frame.total_velocity_count);
    out << ",\"linear_velocity_count\":" << JsonUnsigned(frame.linear_velocity_count);
    out << ",\"angular_velocity_count\":" << JsonUnsigned(frame.angular_velocity_count);
    out << "}";
  }
  out << "]";

  out << ",\"guide_keyframes\":[";
  for (size_t i = 0; i < snapshot.physics.guide_keyframes.size(); ++i) {
    if (i > 0) out << ",";
    const ValidationGuideKeyframe& keyframe = snapshot.physics.guide_keyframes[i];
    out << "{";
    out << "\"frame_index\":" << JsonSigned(keyframe.frame_index);
    out << ",\"enabled\":" << BoolJson(keyframe.enabled);
    out << ",\"expanded\":" << BoolJson(keyframe.expanded);
    out << ",\"guidance_count\":" << JsonUnsigned(keyframe.guidance_count);
    out << "}";
  }
  out << "]";
  out << "}";

  out << ",\"analysis\":{";
  out << "\"triangles\":[";
  for (size_t i = 0; i < snapshot.analysis.triangles.size(); ++i) {
    if (i > 0) out << ",";
    const ValidationTriangleAnalysis& tri = snapshot.analysis.triangles[i];
    out << "{";
    out << "\"index\":" << JsonUnsigned(tri.index);
    out << ",\"area\":" << JsonNumber(tri.area);
    out << ",\"rest_area\":" << JsonNumber(tri.rest_area);
    out << ",\"area_ratio\":" << JsonNumber(tri.area_ratio);
    out << ",\"determinant\":" << JsonNumber(tri.determinant);
    out << ",\"faces_viewer\":" << BoolJson(tri.faces_viewer);
    out << ",\"material_gpa\":" << JsonNumber(tri.material_gpa);
    out << "}";
  }
  out << "]";
  out << ",\"min_area\":" << JsonNumber(snapshot.analysis.min_area);
  out << ",\"min_area_ratio\":" << JsonNumber(snapshot.analysis.min_area_ratio);
  out << ",\"degenerate_triangle_count\":" << JsonUnsigned(snapshot.analysis.degenerate_triangle_count);
  out << ",\"negative_triangle_count\":" << JsonUnsigned(snapshot.analysis.negative_triangle_count);
  out << ",\"max_delta_translation\":" << JsonNumber(snapshot.analysis.max_delta_translation);
  out << "}";

  out << "}";
  return out.str();
}

std::string ValidationLayerApi::Trim(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}

std::string ValidationLayerApi::FirstToken(std::string_view text) {
  size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  size_t end = begin;
  while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
  return std::string(text.substr(begin, end - begin));
}

std::string ValidationLayerApi::ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

std::string ValidationLayerApi::ExtractJsonStringField(std::string_view text, std::string_view key) {
  std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = text.find(needle);
  if (pos == std::string_view::npos) return {};
  pos = text.find(':', pos + needle.size());
  if (pos == std::string_view::npos) return {};
  ++pos;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
  if (pos >= text.size() || text[pos] != '"') return {};
  ++pos;
  std::string value;
  bool escaped = false;
  for (; pos < text.size(); ++pos) {
    char ch = text[pos];
    if (escaped) {
      value.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value.push_back(ch);
  }
  return {};
}

std::string ValidationLayerApi::EscapeJson(std::string_view text) {
  std::ostringstream out;
  for (char ch : text) {
    switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          out << "\\u";
          out << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(ch))
              << std::dec << std::nouppercase;
        } else {
          out << ch;
        }
        break;
    }
  }
  return out.str();
}

std::string ValidationLayerApi::ActionKindToString(ValidationActionKind kind) {
  switch (kind) {
    case ValidationActionKind::PhysStep: return "physstep";
    case ValidationActionKind::Reset: return "reset";
    case ValidationActionKind::SetRunState: return "set_run_state";
    case ValidationActionKind::SetGuideEnabled: return "set_guide_enabled";
  }
  return "unknown";
}

std::string ValidationLayerApi::ActionJson(const ValidationAction& action) {
  std::ostringstream out;
  out << "{";
  out << "\"kind\":\"" << ActionKindToString(action.kind) << "\"";
  switch (action.kind) {
    case ValidationActionKind::PhysStep:
      out << ",\"step_count\":" << JsonUnsigned(action.step_count);
      break;
    case ValidationActionKind::Reset:
      break;
    case ValidationActionKind::SetRunState:
      out << ",\"run_state\":\"" << RunStateToString(action.run_state) << "\"";
      break;
    case ValidationActionKind::SetGuideEnabled:
      out << ",\"enabled\":" << BoolJson(action.enabled);
      break;
  }
  out << "}";
  return out.str();
}

std::string ValidationLayerApi::ModeToString(ValidationInteractionMode mode) {
  switch (mode) {
    case ValidationInteractionMode::Edit: return "Edit";
    case ValidationInteractionMode::Phys: return "Phys";
  }
  return "Edit";
}

std::string ValidationLayerApi::RunStateToString(ValidationPhysRunState state) {
  switch (state) {
    case ValidationPhysRunState::Run: return "Run";
    case ValidationPhysRunState::Pause: return "Pause";
  }
  return "Pause";
}

std::string ValidationLayerApi::SelectionKindToString(ValidationSelectionKind kind) {
  switch (kind) {
    case ValidationSelectionKind::None: return "none";
    case ValidationSelectionKind::Vertex: return "vertex";
    case ValidationSelectionKind::Triangle: return "triangle";
  }
  return "none";
}

std::string ValidationLayerApi::Vec3Json(const ValidationVec3& v) {
  std::ostringstream out;
  out << "[" << JsonNumber(v.x) << "," << JsonNumber(v.y) << "," << JsonNumber(v.z) << "]";
  return out.str();
}

std::string ValidationLayerApi::MatrixJson(const ValidationMatrix4& matrix) {
  std::ostringstream out;
  out << "[";
  for (int row = 0; row < 4; ++row) {
    if (row > 0) out << ",";
    out << "[";
    for (int col = 0; col < 4; ++col) {
      if (col > 0) out << ",";
      out << JsonNumber(matrix.m[row][col]);
    }
    out << "]";
  }
  out << "]";
  return out.str();
}

std::string ValidationLayerApi::BoolJson(bool value) {
  return value ? "true" : "false";
}
