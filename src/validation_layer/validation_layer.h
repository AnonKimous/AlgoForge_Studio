#pragma once

#include "validation_snapshot.h"

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class ValidationLayerApi {
 public:
  ValidationLayerApi() = default;
  ~ValidationLayerApi();

  ValidationLayerApi(const ValidationLayerApi&) = delete;
  ValidationLayerApi& operator=(const ValidationLayerApi&) = delete;

  bool Start();
  void Stop();
  bool enabled() const { return enabled_.load(); }

  void PublishFrame(const ValidationFrameSnapshot& snapshot);

  static bool ParseStartupFlag(const std::vector<std::string>& args);

 private:
  static constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\min_vulkan_win32_validation_layer";

  std::atomic<bool> enabled_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread worker_;
  mutable std::mutex snapshot_mutex_;
  std::string latest_snapshot_json_;
  std::atomic<uint64_t> latest_sequence_{0};
  std::atomic<int> latest_frame_index_{0};

  void WorkerLoop();
  std::string HandleRequest(const std::string& request);
  std::string BuildHealthJson() const;
  std::string BuildSnapshotJson(const ValidationFrameSnapshot& snapshot, uint64_t sequence) const;
  static std::string Trim(const std::string& text);
  static std::string ToLower(std::string text);
  static std::string EscapeJson(std::string_view text);
  static std::string ModeToString(ValidationInteractionMode mode);
  static std::string RunStateToString(ValidationPhysRunState state);
  static std::string SelectionKindToString(ValidationSelectionKind kind);
  static float TriangleArea(const ValidationFrameSnapshot& snapshot, uint32_t tri_index);
  static float RestTriangleArea(const ValidationFrameSnapshot& snapshot, uint32_t tri_index);
  static std::string Vec3Json(const ValidationVec3& v);
  static std::string MatrixJson(const ValidationMatrix4& matrix);
  static std::string BoolJson(bool value);
};
