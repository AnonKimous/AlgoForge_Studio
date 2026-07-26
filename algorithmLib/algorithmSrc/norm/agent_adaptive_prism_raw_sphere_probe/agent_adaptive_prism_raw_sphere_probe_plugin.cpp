#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Vec3 { float x; float y; float z; };
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
struct SourceTriangle { Vec3 p[3]; };
struct RenderTriangle { Vec3 p[3]; Vec3 color; };
struct SourceMesh {
  std::vector<Vec3> positions;
  std::vector<SourceTriangle> triangles;
  Vec3 min_position{};
  Vec3 max_position{};
};

template <typename T>
const algorithm::AlgorithmContainer* RequireContainer(
    const algorithm::AlgorithmContainerSet* set,
    const char* name) {
  const algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(*set, name);
  assert(container);
  assert(container->element_stride >= sizeof(T));
  return container;
}

template <typename T>
algorithm::AlgorithmContainer* RequireMutableContainer(
    algorithm::AlgorithmContainerSet* set,
    const char* name) {
  algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(set, name);
  assert(container);
  assert(container->element_stride >= sizeof(T));
  return container;
}

template <typename T>
T ReadElement(const algorithm::AlgorithmContainer& container, size_t index) {
  T value{};
  std::memcpy(
    &value,
    container.bytes.data() + index * container.element_stride,
    sizeof(T));
  return value;
}

template <typename T>
void WriteElement(algorithm::AlgorithmContainer& container, size_t index, const T& value) {
  std::memcpy(
    container.bytes.data() + index * container.element_stride,
    &value,
    sizeof(T));
}

void WriteUint32(algorithm::AlgorithmContainer& container, uint32_t value) {
  WriteElement<uint32_t>(container, 0u, value);
}

void WriteVec4(algorithm::AlgorithmContainer& container, size_t index, Vec3 xyz, float w) {
  WriteElement<std::array<float, 4>>(container, index, {xyz.x, xyz.y, xyz.z, w});
}

SourceMesh LoadSourceMesh(const algorithm::AlgorithmContainerSet* set) {
  const algorithm::AlgorithmContainer* positions = RequireContainer<std::array<float, 3>>(set, "vertex_mesh_0");
  const algorithm::AlgorithmContainer* triangles = RequireContainer<std::array<uint32_t, 3>>(set, "triangle_mesh_0");
  SourceMesh mesh{};
  const size_t position_count = positions->bytes.size() / positions->element_stride;
  mesh.positions.resize(position_count);
  mesh.min_position = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
  mesh.max_position = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  for (size_t i = 0u; i < position_count; ++i) {
    const std::array<float, 3> p = ReadElement<std::array<float, 3>>(*positions, i);
    mesh.positions[i] = {p[0], p[1], p[2]};
    mesh.min_position = {
      std::min(mesh.min_position.x, p[0]),
      std::min(mesh.min_position.y, p[1]),
      std::min(mesh.min_position.z, p[2])};
    mesh.max_position = {
      std::max(mesh.max_position.x, p[0]),
      std::max(mesh.max_position.y, p[1]),
      std::max(mesh.max_position.z, p[2])};
  }
  const size_t triangle_count = triangles->bytes.size() / triangles->element_stride;
  for (size_t i = 0u; i < triangle_count; ++i) {
    const std::array<uint32_t, 3> indices = ReadElement<std::array<uint32_t, 3>>(*triangles, i);
    if (indices == std::array<uint32_t, 3>{0u, 0u, 0u}) continue;
    SourceTriangle triangle{};
    triangle.p[0] = mesh.positions[indices[0]];
    triangle.p[1] = mesh.positions[indices[1]];
    triangle.p[2] = mesh.positions[indices[2]];
    mesh.triangles.push_back(triangle);
  }
  assert(!mesh.triangles.empty());
  return mesh;
}

class RawSphereJobsExecutor final : public algomanager::bridge::IAlgorithmJobsExecutor {
 public:
  bool ExecuteJobsAlgorithm(
      const algomanager::bridge::AgentTickContext& context,
      const algorithm::AlgorithmProfile& algorithm_profile,
      const AgentToAlgorithmSignal& agent_to_algorithm_signal,
      algorithm::AlgorithmContainerSet* container_set,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      algomanager::bridge::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    assert(container_set);
    SourceMesh mesh = LoadSourceMesh(container_set);
    algorithm::AlgorithmContainer& frame = *RequireMutableContainer<uint32_t>(container_set, "frame_tick");
    algorithm::AlgorithmContainer& vertex_count = *RequireMutableContainer<uint32_t>(container_set, "source_vertex_count");
    algorithm::AlgorithmContainer& source_count = *RequireMutableContainer<uint32_t>(container_set, "source_triangle_count");
    algorithm::AlgorithmContainer& render_count = *RequireMutableContainer<uint32_t>(container_set, "render_triangle_count");
    algorithm::AlgorithmContainer& scene = *RequireMutableContainer<std::array<float, 4>>(container_set, "render_scene");
    algorithm::AlgorithmContainer& buffer = *RequireMutableContainer<std::array<float, 4>>(container_set, "render_triangle_buffer");
    WriteUint32(frame, ++tick_);
    WriteUint32(vertex_count, static_cast<uint32_t>(mesh.positions.size()));
    WriteUint32(source_count, static_cast<uint32_t>(mesh.triangles.size()));
    WriteUint32(render_count, static_cast<uint32_t>(mesh.triangles.size()));
    const Vec3 scene_center = (mesh.min_position + mesh.max_position) * 0.5f;
    const Vec3 scene_extent = mesh.max_position - mesh.min_position;
    const float scene_scale = std::max(scene_extent.x, std::max(scene_extent.y, scene_extent.z)) * 0.85f;
    WriteVec4(scene, 0u, scene_center, scene_scale);
    uint32_t checksum = 2166136261u;
    for (size_t i = 0u; i < mesh.triangles.size(); ++i) {
      const SourceTriangle& triangle = mesh.triangles[i];
      for (size_t corner = 0u; corner < 3u; ++corner) {
        WriteVec4(buffer, i * 4u + corner, triangle.p[corner], 1.0f);
        checksum ^= static_cast<uint32_t>((triangle.p[corner].x + 1.0f) * 100000.0f);
        checksum *= 16777619u;
      }
      WriteVec4(buffer, i * 4u + 3u, {0.10f, 0.56f, 0.96f}, 1.0f);
    }
    if (debug_state) {
      debug_state->signals.push_back({
        .name = "agent_adaptive_prism_raw_sphere_probe.jobs",
        .payload = "vertices=" + std::to_string(mesh.positions.size()) +
          ",triangles=" + std::to_string(mesh.triangles.size()) +
          ",render_triangles=" + std::to_string(mesh.triangles.size()) +
          ",checksum=" + std::to_string(checksum),
      });
    }
    if (algorithm_to_agent_signal) *algorithm_to_agent_signal = {};
    return true;
  }

 private:
  uint32_t tick_{0u};
};

void DestroyJobsExecutor(algomanager::bridge::IAlgorithmJobsExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algomanager::algocatalog::AlgorithmPluginBundle* out_bundle) {
  assert(request);
  assert(out_bundle);
  out_bundle->Clear();
  out_bundle->jobs_symbol = true;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->jobs_executor = new RawSphereJobsExecutor();
  out_bundle->destroy_jobs_executor = &DestroyJobsExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algorithm::AlgorithmReflector* out_reflector) {
  assert(request);
  assert(out_reflector);
  std::shared_ptr<algorithm::AlgorithmReflector> reflector{};
  algorithm::AlgorithmPackageLocation location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "", &location, nullptr)) return false;
  if (!algomanager::algocatalog::LoadAlgorithmPackageReflectorFromLocation(location, &reflector, nullptr)) return false;
  *out_reflector = *reflector;
  return true;
}
