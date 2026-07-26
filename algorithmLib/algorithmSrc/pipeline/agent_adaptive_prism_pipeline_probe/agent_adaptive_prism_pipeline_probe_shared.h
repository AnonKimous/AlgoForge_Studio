#pragma once

#include "../algorithm_plugin_api.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace agent_adaptive_prism_pipeline_probe {
namespace detail {

struct Vec3 {
  float x;
  float y;
  float z;
};

inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

inline algorithm::AlgorithmContainer& Container(
    algorithm::AlgorithmContainerSet* set,
    const char* name) {
  return *algorithm::FindAlgorithmContainer(set, name);
}

template <typename T>
inline T Read(const algorithm::AlgorithmContainer& container, size_t index = 0u) {
  T value{};
  std::memcpy(&value, container.bytes.data() + index * container.element_stride, sizeof(T));
  return value;
}

template <typename T>
inline void Write(algorithm::AlgorithmContainer& container, size_t index, const T& value) {
  std::memcpy(container.bytes.data() + index * container.element_stride, &value, sizeof(T));
}

inline void WriteUint(algorithm::AlgorithmContainer& container, uint32_t value) {
  Write<uint32_t>(container, 0u, value);
}

inline size_t ElementCount(const algorithm::AlgorithmContainer& container) {
  return container.bytes.size() / container.element_stride;
}

inline Vec3 ReadPosition(const algorithm::AlgorithmContainer& positions, size_t index) {
  const std::array<float, 3> value = Read<std::array<float, 3>>(positions, index);
  return {value[0], value[1], value[2]};
}

inline std::array<uint32_t, 3> ReadTriangle(
    const algorithm::AlgorithmContainer& triangles,
    size_t index) {
  return Read<std::array<uint32_t, 3>>(triangles, index);
}

inline void WritePrismRecord(
    algorithm::AlgorithmContainer& buffer,
    size_t record_index,
    const std::array<Vec3, 6>& points,
    float kind,
    float first_triangle,
    float second_triangle,
    float level) {
  std::array<float, 28> record{};
  for (size_t point_index = 0u; point_index < 6u; ++point_index) {
    record[point_index * 4u + 0u] = points[point_index].x;
    record[point_index * 4u + 1u] = points[point_index].y;
    record[point_index * 4u + 2u] = points[point_index].z;
    record[point_index * 4u + 3u] = 1.0f;
  }
  record[24] = kind;
  record[25] = first_triangle;
  record[26] = second_triangle;
  record[27] = level;
  Write<std::array<float, 28>>(buffer, record_index, record);
}

inline void WriteRenderTriangle(
    algorithm::AlgorithmContainer& buffer,
    size_t triangle_index,
    Vec3 a,
    Vec3 b,
    Vec3 c,
    std::array<float, 3> color) {
  Write<std::array<float, 4>>(buffer, triangle_index * 4u + 0u, {a.x, a.y, a.z, 1.0f});
  Write<std::array<float, 4>>(buffer, triangle_index * 4u + 1u, {b.x, b.y, b.z, 1.0f});
  Write<std::array<float, 4>>(buffer, triangle_index * 4u + 2u, {c.x, c.y, c.z, 1.0f});
  Write<std::array<float, 4>>(buffer, triangle_index * 4u + 3u, {color[0], color[1], color[2], 1.0f});
}

inline uint64_t EdgeKey(uint32_t a, uint32_t b) {
  if (a > b) std::swap(a, b);
  return (static_cast<uint64_t>(a) << 32u) | static_cast<uint64_t>(b);
}

inline uint32_t ParseObjFaceIndex(const std::string& token) {
  const size_t slash = token.find('/');
  return static_cast<uint32_t>(std::stoul(token.substr(0u, slash))) - 1u;
}

inline std::array<size_t, 2u> LoadStageBeginMesh(
    algorithm::AlgorithmContainer& positions,
    algorithm::AlgorithmContainer& triangles) {
  std::ifstream input("data/agent_adaptive_prism_banana_decomposition_probe/banana.obj");
  std::string line;
  size_t position_count = 0u;
  size_t triangle_count = 0u;
  while (std::getline(input, line)) {
    std::istringstream stream(line);
    std::string kind;
    stream >> kind;
    if (kind == "v") {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      stream >> x >> y >> z;
      Write<std::array<float, 3>>(positions, position_count++, {x, y, z});
    } else if (kind == "f") {
      std::string a;
      std::string b;
      std::string c;
      stream >> a >> b >> c;
      Write<std::array<uint32_t, 3>>(triangles, triangle_count++, {
        ParseObjFaceIndex(a), ParseObjFaceIndex(b), ParseObjFaceIndex(c)});
    }
  }
  return {position_count, triangle_count};
}

inline bool ExecuteMeshIngest(algorithm::AlgorithmContainerSet* set) {
  algorithm::AlgorithmContainer& frame_tick = Container(set, "frame_tick");
  algorithm::AlgorithmContainer& source_vertex_count = Container(set, "source_vertex_count");
  algorithm::AlgorithmContainer& source_triangle_count = Container(set, "source_triangle_count");
  algorithm::AlgorithmContainer& prism_count = Container(set, "prism_count");
  algorithm::AlgorithmContainer& audit_status = Container(set, "audit_status");
  algorithm::AlgorithmContainer& mesh_positions = Container(set, "mesh_positions");
  algorithm::AlgorithmContainer& mesh_triangles = Container(set, "mesh_triangles");
  algorithm::AlgorithmContainer& prism_buffer = Container(set, "prism_buffer");
  algorithm::AlgorithmContainer& audit_buffer = Container(set, "audit_buffer");
  WriteUint(frame_tick, Read<uint32_t>(frame_tick) + 1u);
  const std::array<size_t, 2u> imported_counts = LoadStageBeginMesh(mesh_positions, mesh_triangles);
  const size_t vertex_count = imported_counts[0u];
  const size_t triangle_count = imported_counts[1u];

  WriteUint(source_vertex_count, static_cast<uint32_t>(vertex_count));
  WriteUint(source_triangle_count, static_cast<uint32_t>(triangle_count));
  WriteUint(prism_count, 0u);
  WriteUint(audit_status, 0u);
  Write<std::array<float, 4>>(audit_buffer, 0u, {
    static_cast<float>(vertex_count),
    static_cast<float>(triangle_count),
    0.0f,
    0.0f});
  (void)prism_buffer;
  return true;
}

inline bool ExecuteSourceAudit(algorithm::AlgorithmContainerSet* set) {
  algorithm::AlgorithmContainer& source_vertex_count = Container(set, "source_vertex_count");
  algorithm::AlgorithmContainer& source_triangle_count = Container(set, "source_triangle_count");
  algorithm::AlgorithmContainer& audit_buffer = Container(set, "audit_buffer");
  const float vertex_count = static_cast<float>(Read<uint32_t>(source_vertex_count));
  const float triangle_count = static_cast<float>(Read<uint32_t>(source_triangle_count));
  Write<std::array<float, 4>>(audit_buffer, 0u, {vertex_count, triangle_count, 0.0f, 0.0f});
  return true;
}

inline bool ExecuteSurfacePairing(algorithm::AlgorithmContainerSet* set) {
  algorithm::AlgorithmContainer& source_vertex_count = Container(set, "source_vertex_count");
  algorithm::AlgorithmContainer& source_triangle_count = Container(set, "source_triangle_count");
  algorithm::AlgorithmContainer& prism_count = Container(set, "prism_count");
  algorithm::AlgorithmContainer& paired_count_value = Container(set, "paired_count");
  algorithm::AlgorithmContainer& mesh_positions = Container(set, "mesh_positions");
  algorithm::AlgorithmContainer& mesh_triangles = Container(set, "mesh_triangles");
  algorithm::AlgorithmContainer& prism_buffer = Container(set, "prism_buffer");
  const size_t vertex_count = Read<uint32_t>(source_vertex_count);
  const size_t triangle_count = Read<uint32_t>(source_triangle_count);
  (void)vertex_count;

  std::vector<std::array<uint32_t, 3>> triangles(triangle_count);
  std::vector<bool> used(triangle_count, false);
  std::map<uint64_t, uint32_t> first_triangle_for_edge;
  for (size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
    triangles[triangle_index] = ReadTriangle(mesh_triangles, triangle_index);
    for (size_t edge_index = 0u; edge_index < 3u; ++edge_index) {
      const uint32_t a = triangles[triangle_index][edge_index];
      const uint32_t b = triangles[triangle_index][(edge_index + 1u) % 3u];
      first_triangle_for_edge.emplace(EdgeKey(a, b), static_cast<uint32_t>(triangle_index));
    }
  }

  size_t output_count = 0u;
  size_t paired_count = 0u;
  for (size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
    if (used[triangle_index]) continue;
    const auto triangle = triangles[triangle_index];
    uint32_t partner = UINT32_MAX;
    for (size_t edge_index = 0u; edge_index < 3u; ++edge_index) {
      const uint32_t a = triangle[edge_index];
      const uint32_t b = triangle[(edge_index + 1u) % 3u];
      const auto found = first_triangle_for_edge.find(EdgeKey(a, b));
      if (found != first_triangle_for_edge.end() && found->second != triangle_index && !used[found->second]) {
        partner = found->second;
        break;
      }
    }

    std::array<Vec3, 6> points{};
    for (size_t point_index = 0u; point_index < 3u; ++point_index) {
      points[point_index] = ReadPosition(mesh_positions, triangle[point_index]);
    }
    float kind = 1.0f;
    float second_triangle = -1.0f;
    if (partner != UINT32_MAX) {
      const auto partner_triangle = triangles[partner];
      for (size_t point_index = 0u; point_index < 3u; ++point_index) {
        points[3u + point_index] = ReadPosition(mesh_positions, partner_triangle[point_index]);
      }
      kind = 2.0f;
      second_triangle = static_cast<float>(partner);
      used[partner] = true;
      ++paired_count;
    } else {
      points[3] = points[0];
      points[4] = points[1];
      points[5] = points[2];
    }
    used[triangle_index] = true;
    WritePrismRecord(
      prism_buffer,
      output_count,
      points,
      kind,
      static_cast<float>(triangle_index),
      second_triangle,
      0.0f);
    ++output_count;
  }
  WriteUint(prism_count, static_cast<uint32_t>(output_count));
  WriteUint(paired_count_value, static_cast<uint32_t>(paired_count));
  Write<std::array<float, 4>>(Container(set, "audit_buffer"), 1u, {
    static_cast<float>(output_count),
    static_cast<float>(paired_count),
    static_cast<float>(triangle_count - paired_count * 2u),
    0.0f});
  return true;
}

inline bool ExecuteAuditAndLod(algorithm::AlgorithmContainerSet* set) {
  algorithm::AlgorithmContainer& frame_tick = Container(set, "frame_tick");
  algorithm::AlgorithmContainer& source_triangle_count = Container(set, "source_triangle_count");
  algorithm::AlgorithmContainer& prism_count = Container(set, "prism_count");
  algorithm::AlgorithmContainer& audit_status = Container(set, "audit_status");
  algorithm::AlgorithmContainer& render_triangle_count = Container(set, "render_triangle_count");
  algorithm::AlgorithmContainer& paired_count_value = Container(set, "paired_count");
  algorithm::AlgorithmContainer& lod_level_count = Container(set, "lod_level_count");
  algorithm::AlgorithmContainer& render_scene = Container(set, "render_scene");
  algorithm::AlgorithmContainer& render_buffer = Container(set, "render_triangle_buffer");
  algorithm::AlgorithmContainer& prism_buffer = Container(set, "prism_buffer");
  algorithm::AlgorithmContainer& lod_buffer = Container(set, "lod_buffer");
  algorithm::AlgorithmContainer& audit_buffer = Container(set, "audit_buffer");

  const size_t record_count = Read<uint32_t>(prism_count);
  const size_t source_count = Read<uint32_t>(source_triangle_count);
  size_t render_count = 0u;
  Vec3 min_position{1.0e30f, 1.0e30f, 1.0e30f};
  Vec3 max_position{-1.0e30f, -1.0e30f, -1.0e30f};
  size_t paired_count = 0u;
  size_t unmatched_count = 0u;
  for (size_t record_index = 0u; record_index < record_count; ++record_index) {
    const std::array<float, 28> record = Read<std::array<float, 28>>(prism_buffer, record_index);
    const float kind = record[24];
    Write<std::array<float, 4>>(lod_buffer, record_index, {
      static_cast<float>(record_index),
      0.0f,
      kind,
      0.0f});
    if (kind > 1.5f) ++paired_count; else ++unmatched_count;
    for (size_t point_index = 0u; point_index < (kind > 1.5f ? 6u : 3u); ++point_index) {
      const Vec3 point{record[point_index * 4u], record[point_index * 4u + 1u], record[point_index * 4u + 2u]};
      min_position.x = std::min(min_position.x, point.x);
      min_position.y = std::min(min_position.y, point.y);
      min_position.z = std::min(min_position.z, point.z);
      max_position.x = std::max(max_position.x, point.x);
      max_position.y = std::max(max_position.y, point.y);
      max_position.z = std::max(max_position.z, point.z);
    }
    WriteRenderTriangle(
      render_buffer,
      render_count++,
      {record[0], record[1], record[2]},
      {record[4], record[5], record[6]},
      {record[8], record[9], record[10]},
      {0.95f, 0.47f, 0.12f});
    if (kind > 1.5f) {
      WriteRenderTriangle(
        render_buffer,
        render_count++,
        {record[12], record[13], record[14]},
        {record[16], record[17], record[18]},
        {record[20], record[21], record[22]},
        {0.12f, 0.65f, 0.95f});
    }
  }
  const Vec3 center{
    (min_position.x + max_position.x) * 0.5f,
    (min_position.y + max_position.y) * 0.5f,
    (min_position.z + max_position.z) * 0.5f};
  const Vec3 extent = max_position - min_position;
  const float scale = std::max(extent.x, std::max(extent.y, extent.z)) * 0.72f;
  Write<std::array<float, 4>>(render_scene, 0u, {center.x, center.y, center.z, scale});
  WriteUint(render_triangle_count, static_cast<uint32_t>(render_count));
  WriteUint(paired_count_value, static_cast<uint32_t>(paired_count));
  WriteUint(lod_level_count, static_cast<uint32_t>(record_count > 0u ? 1u : 0u));
  WriteUint(audit_status, render_count > 0u && source_count > 0u ? 1u : 0u);
  Write<std::array<float, 4>>(audit_buffer, 2u, {
    static_cast<float>(source_count),
    static_cast<float>(record_count),
    static_cast<float>(paired_count),
    static_cast<float>(unmatched_count)});
  WriteUint(frame_tick, Read<uint32_t>(frame_tick));
  return true;
}

class PipelineJobsExecutor final : public algomanager::bridge::IAlgorithmJobsExecutor {
 public:
  explicit PipelineJobsExecutor(std::string algorithm_name)
      : algorithm_name_(std::move(algorithm_name)) {}

  bool ExecuteJobsAlgorithm(
      const algomanager::bridge::AgentTickContext& context,
      const algorithm::AlgorithmProfile& algorithm_profile,
      const AgentToAlgorithmSignal& agent_to_algorithm_signal,
      algorithm::AlgorithmContainerSet* algorithm_container_set,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      algomanager::bridge::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    (void)algorithm_to_agent_signal;
    (void)debug_state;
    if (algorithm_name_.find("stage1") != std::string::npos) return ExecuteSurfacePairing(algorithm_container_set);
    if (algorithm_name_.find("stage2") != std::string::npos) return ExecuteAuditAndLod(algorithm_container_set);
    if (algorithm_name_.find("stageEndBody") != std::string::npos) return ExecuteAuditAndLod(algorithm_container_set);
    if (algorithm_name_.find("stageBegin") != std::string::npos) return ExecuteMeshIngest(algorithm_container_set);
    if (algorithm_name_.find("stageEnd") != std::string::npos) return ExecuteAuditAndLod(algorithm_container_set);
    if (algorithm_name_.find("wrapperEnd") != std::string::npos) return true;
    return ExecuteSourceAudit(algorithm_container_set);
  }

 private:
  std::string algorithm_name_;
};

inline void DestroyJobsExecutor(algomanager::bridge::IAlgorithmJobsExecutor* executor) {
  delete executor;
}

inline bool CreateBundle(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algomanager::algocatalog::AlgorithmPluginBundle* out_bundle) {
  out_bundle->Clear();
  out_bundle->jobs_symbol = true;
  out_bundle->reflector = true;
  out_bundle->jobs_executor = new PipelineJobsExecutor(request->algorithm_name);
  out_bundle->destroy_jobs_executor = &DestroyJobsExecutor;
  return true;
}

inline bool CreateRuntimeReflector(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algorithm::AlgorithmReflector* out_reflector) {
  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name,
        &package_location,
        nullptr)) return false;
  if (!algomanager::algocatalog::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) return false;
  *out_reflector = *runtime_reflector;
  return true;
}

}  // namespace detail
}  // namespace agent_adaptive_prism_pipeline_probe
