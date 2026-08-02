#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaximumRenderTriangles = 16384u;

using ProfileClock = std::chrono::steady_clock;

float ElapsedMilliseconds(ProfileClock::time_point begin) {
  return std::chrono::duration<float, std::milli>(ProfileClock::now() - begin).count();
}

struct Vec3 {
  float x{};
  float y{};
  float z{};
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 operator*(float s, Vec3 a) { return a * s; }
Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }

float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) {
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Normalize(Vec3 a) { return a * (1.0f / Length(a)); }

struct Mat3 {
  Vec3 column[3]{};
};

Mat3 operator+(Mat3 a, Mat3 b) {
  return {{a.column[0] + b.column[0], a.column[1] + b.column[1], a.column[2] + b.column[2]}};
}
Mat3 operator*(Mat3 a, float s) {
  return {{a.column[0] * s, a.column[1] * s, a.column[2] * s}};
}
Vec3 operator*(Mat3 a, Vec3 b) {
  return a.column[0] * b.x + a.column[1] * b.y + a.column[2] * b.z;
}
Mat3 Outer(Vec3 a, Vec3 b) { return {{a * b.x, a * b.y, a * b.z}}; }
Mat3 Transpose(Mat3 a) {
  return {{
    {a.column[0].x, a.column[1].x, a.column[2].x},
    {a.column[0].y, a.column[1].y, a.column[2].y},
    {a.column[0].z, a.column[1].z, a.column[2].z},
  }};
}

template <typename T>
const algorithm::AlgorithmContainer& RequireContainer(
    const algorithm::AlgorithmContainerSet& set,
    const char* name) {
  const algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(set, name);
  assert(container);
  assert(container->element_stride >= sizeof(T));
  return *container;
}

template <typename T>
algorithm::AlgorithmContainer& RequireMutableContainer(
    algorithm::AlgorithmContainerSet& set,
    const char* name) {
  algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(&set, name);
  assert(container);
  assert(container->element_stride >= sizeof(T));
  return *container;
}

template <typename T>
T ReadElement(const algorithm::AlgorithmContainer& container, size_t index) {
  T value{};
  std::memcpy(&value, container.bytes.data() + index * container.element_stride, sizeof(T));
  return value;
}

template <typename T>
void WriteElement(algorithm::AlgorithmContainer& container, size_t index, const T& value) {
  std::memcpy(container.bytes.data() + index * container.element_stride, &value, sizeof(T));
}

float ReadFloat(const algorithm::AlgorithmContainer& container, size_t index) {
  return ReadElement<float>(container, index);
}

void WriteFloat(algorithm::AlgorithmContainer& container, size_t index, float value) {
  WriteElement<float>(container, index, value);
}

uint32_t ReadUint32(const algorithm::AlgorithmContainer& container) {
  return ReadElement<uint32_t>(container, 0u);
}

void WriteUint32(algorithm::AlgorithmContainer& container, uint32_t value) {
  WriteElement<uint32_t>(container, 0u, value);
}

void WriteVec4(algorithm::AlgorithmContainer& container, size_t index, Vec3 xyz, float w) {
  WriteElement<std::array<float, 4>>(container, index, {xyz.x, xyz.y, xyz.z, w});
}

struct Config {
  std::array<Vec3, 4> tetra{};
  Vec3 blast{};
  Vec3 plane_normal{};
  float plane_offset{};
  float peak_pressure{};
  float decay_length{};
  float wave_speed{};
  float sample_time{};
  float pulse_width{};
  float density{};
  float tensile_strength{};
  float shear_strength{};
  float pressure_tolerance_ratio{};
  float force_tolerance_ratio{};
  float moment_tolerance_ratio{};
  float refine_threshold{};
  float fracture_observation_time{};
  Vec3 external_pressure_direction{};
  float external_pressure{};
  Vec3 secondary_plane_normal{};
  float secondary_plane_offset{};
  float external_pressure_duration{};
};

Config ReadConfig(const algorithm::AlgorithmContainerSet& set) {
  const algorithm::AlgorithmContainer& source = RequireContainer<float>(set, "blast_plane_config");
  Config config{};
  for (size_t vertex = 0u; vertex < 4u; ++vertex) {
    config.tetra[vertex] = {
      ReadFloat(source, vertex * 3u + 0u),
      ReadFloat(source, vertex * 3u + 1u),
      ReadFloat(source, vertex * 3u + 2u),
    };
  }
  config.blast = {ReadFloat(source, 12u), ReadFloat(source, 13u), ReadFloat(source, 14u)};
  config.plane_normal = Normalize({ReadFloat(source, 15u), ReadFloat(source, 16u), ReadFloat(source, 17u)});
  config.plane_offset = ReadFloat(source, 18u) / Length({ReadFloat(source, 15u), ReadFloat(source, 16u), ReadFloat(source, 17u)});
  config.peak_pressure = ReadFloat(source, 19u);
  config.decay_length = ReadFloat(source, 20u);
  config.wave_speed = ReadFloat(source, 21u);
  config.sample_time = ReadFloat(source, 22u);
  config.pulse_width = ReadFloat(source, 23u);
  config.density = ReadFloat(source, 24u);
  config.tensile_strength = ReadFloat(source, 25u);
  config.shear_strength = ReadFloat(source, 26u);
  config.pressure_tolerance_ratio = ReadFloat(source, 27u);
  config.force_tolerance_ratio = ReadFloat(source, 28u);
  config.moment_tolerance_ratio = ReadFloat(source, 29u);
  config.refine_threshold = ReadFloat(source, 30u);
  config.fracture_observation_time = ReadFloat(source, 31u);
  config.external_pressure_direction = Normalize({ReadFloat(source, 32u), ReadFloat(source, 33u), ReadFloat(source, 34u)});
  config.external_pressure = ReadFloat(source, 35u);
  config.secondary_plane_normal = Normalize({ReadFloat(source, 36u), ReadFloat(source, 37u), ReadFloat(source, 38u)});
  config.secondary_plane_offset = ReadFloat(source, 39u) / Length({ReadFloat(source, 36u), ReadFloat(source, 37u), ReadFloat(source, 38u)});
  config.external_pressure_duration = ReadFloat(source, 40u);
  assert(config.peak_pressure > 0.0f);
  assert(config.decay_length > 0.0f);
  assert(config.wave_speed > 0.0f);
  assert(config.pulse_width > 0.0f);
  assert(config.density > 0.0f);
  assert(config.tensile_strength > 0.0f);
  assert(config.shear_strength > 0.0f);
  assert(config.pressure_tolerance_ratio > 0.0f);
  assert(config.force_tolerance_ratio > 0.0f);
  assert(config.moment_tolerance_ratio > 0.0f);
  assert(config.refine_threshold > 0.0f);
  assert(config.fracture_observation_time > 0.0f);
  assert(config.external_pressure > 0.0f);
  assert(config.external_pressure_duration > 0.0f);
  assert(std::abs(Dot(config.plane_normal, config.secondary_plane_normal)) < 0.95f);
  return config;
}

struct FaceSample {
  Vec3 position{};
  Vec3 force{};
  float arrival{};
  float pressure{};
};

struct SurfaceFieldSample {
  Vec3 traction{};
  Vec3 blast_traction{};
  Vec3 external_pressure_traction{};
  float arrival{};
  float pressure{};
  float blast_pressure{};
  float external_pressure{};
};

SurfaceFieldSample SampleSurfaceField(
    const Config& config,
    Vec3 position,
    Vec3 outward_normal) {
  const Vec3 source_delta = position - config.blast;
  const float distance = Length(source_delta);
  const Vec3 radial = source_delta * (1.0f / distance);
  const float arrival = distance / config.wave_speed;
  const float phase = (config.sample_time - arrival) / config.pulse_width;
  const float exposure = std::max(-Dot(outward_normal, radial), 0.0f);
  const float blast_pressure =
    config.peak_pressure *
    std::exp(-distance / config.decay_length) *
    std::exp(-(phase * phase)) *
    exposure;
  const float external_exposure = std::max(-Dot(outward_normal, config.external_pressure_direction), 0.0f);
  const float external_pressure = config.external_pressure * external_exposure;
  const Vec3 blast_traction = radial * blast_pressure;
  const Vec3 external_pressure_traction = config.external_pressure_direction * external_pressure;
  return {
    blast_traction + external_pressure_traction,
    blast_traction,
    external_pressure_traction,
    arrival,
    blast_pressure + external_pressure,
    blast_pressure,
    external_pressure,
  };
}

using ChildTetra = std::array<Vec3, 4>;

struct RefineDecision {
  uint32_t cell_id{};
  uint32_t source_lg_depth{};
  uint32_t target_lg_depth{1u};
  uint32_t refine_mode{3u};
  uint32_t reason_mask{};
  float pressure_error{};
  float force_error{};
  float subface_force_error{};
  float moment_error{};
  float refine_score{};
  Vec3 fine_force{};
  Vec3 fine_moment{};
  uint32_t coarse_sample_count{};
  uint32_t fine_sample_count{};
  bool enter_precision_group{};
};

struct PrecisionGroup {
  uint32_t group_id{};
  RefineDecision decision{};
  std::array<ChildTetra, 8> active_leaf_tetra{};
  float child_volume_sum{};
  float execution_checksum{};
  bool executed{};
};

struct FineTetra {
  std::array<uint32_t, 4> node{};
  float volume{};
  Vec3 center{};
  uint32_t component{};
};

struct FineFaceOwner {
  uint32_t tetra{};
  std::array<uint32_t, 3> oriented{};
};

struct FineInterface {
  uint32_t left{};
  uint32_t right{};
  std::array<uint32_t, 3> oriented{};
  Vec3 normal{};
  float area{};
  bool broken{};
  uint32_t fracture_direction{};
};

struct FineBoundaryFace {
  uint32_t tetra{};
  std::array<uint32_t, 3> oriented{};
};

struct FineFragment {
  float mass{};
  Vec3 center{};
  Vec3 velocity{};
  Vec3 translation{};
};

struct FaceResult {
  Vec3 center{};
  Vec3 force{};
  float maximum_pressure{};
  bool loaded{};
};

struct RenderTriangle {
  Vec3 point[3]{};
  Vec3 color{};
  uint32_t panel{};
};

struct StageTiming {
  float evaluate_ms{};
  float admission_ms{};
  float mesh_ms{};
  float adjacency_ms{};
  float partition_ms{};
  float regularization_ms{};
  float components_ms{};
  float impulse_ms{};
  float closure_ms{};
  float render_ms{};
  float write_ms{};
  float total_ms{};
};

struct Result {
  Vec3 center{};
  float volume{};
  std::array<Vec3, 4> nodal_force{};
  std::array<FaceResult, 4> faces{};
  Vec3 external_force{};
  Vec3 blast_force{};
  Vec3 external_pressure_force{};
  Vec3 external_moment{};
  Vec3 nodal_resultant{};
  Vec3 nodal_moment{};
  Mat3 stress{};
  std::vector<Vec3> cut_polygon{};
  Vec3 cut_center{};
  float cut_area{};
  Vec3 traction{};
  Vec3 cut_force{};
  Vec3 opposite_cut_force{};
  float normal_traction{};
  float shear_traction{};
  float fracture_index{};
  std::vector<Vec3> secondary_cut_polygon{};
  Vec3 secondary_cut_center{};
  float secondary_cut_area{};
  Vec3 secondary_traction{};
  float secondary_normal_traction{};
  float secondary_shear_traction{};
  float secondary_fracture_index{};
  float minimum_arrival{std::numeric_limits<float>::infinity()};
  float maximum_arrival{};
  float maximum_pressure{};
  uint32_t loaded_face_count{};
  RefineDecision refine_decision{};
  std::array<ChildTetra, 8> precision_leaf_tetra{};
  uint32_t precision_group_count{};
  uint32_t accepted_lg_depth{};
  uint32_t precision_leaf_count{};
  uint32_t precision_group_executed{};
  float precision_child_volume_sum{};
  float precision_execution_checksum{};
  std::vector<Vec3> fine_nodes{};
  std::vector<FineTetra> fine_tetra{};
  std::vector<FineInterface> fine_interfaces{};
  std::vector<FineBoundaryFace> fine_boundaries{};
  std::vector<FineFragment> fragments{};
  uint32_t broken_face_count{};
  uint32_t primary_broken_face_count{};
  uint32_t secondary_broken_face_count{};
  uint32_t fragment_count{};
  float crack_area{};
  float fine_volume_sum{};
  float fracture_momentum_residual{};
  float crack_opening_distance{};
  float fracture_checksum{};
  uint32_t fragment_surface_bad_edge_count{};
  float fracture_mass_residual{};
  Vec3 expected_external_impulse{};
  uint32_t partition_regularization_count{};
  bool fracture_committed{};
  std::vector<RenderTriangle> render_triangles{};
  StageTiming timing{};
};

void AddTriangle(Result& result, Vec3 a, Vec3 b, Vec3 c, Vec3 color, uint32_t panel) {
  assert(result.render_triangles.size() < kMaximumRenderTriangles);
  result.render_triangles.push_back({{a, b, c}, color, panel});
}

void AddQuad(Result& result, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color, uint32_t panel) {
  AddTriangle(result, a, b, c, color, panel);
  AddTriangle(result, a, c, d, color, panel);
}

void AddSegment(Result& result, Vec3 start, Vec3 end, float radius, Vec3 color, uint32_t panel) {
  const Vec3 direction = Normalize(end - start);
  const Vec3 reference = std::abs(direction.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  const Vec3 side = Normalize(Cross(direction, reference)) * radius;
  const Vec3 up = Normalize(Cross(direction, side)) * radius;
  const std::array<Vec3, 4> a{{start - side - up, start + side - up, start + side + up, start - side + up}};
  const std::array<Vec3, 4> b{{end - side - up, end + side - up, end + side + up, end - side + up}};
  for (size_t edge = 0u; edge < 4u; ++edge) {
    const size_t next = (edge + 1u) % 4u;
    AddQuad(result, a[edge], b[edge], b[next], a[next], color, panel);
  }
  AddQuad(result, a[3], a[2], a[1], a[0], color, panel);
  AddQuad(result, b[0], b[1], b[2], b[3], color, panel);
}

void AddOctahedron(Result& result, Vec3 center, float radius, Vec3 color, uint32_t panel) {
  const Vec3 x{radius, 0.0f, 0.0f};
  const Vec3 y{0.0f, radius, 0.0f};
  const Vec3 z{0.0f, 0.0f, radius};
  AddTriangle(result, center + y, center + x, center + z, color, panel);
  AddTriangle(result, center + y, center + z, center - x, color, panel);
  AddTriangle(result, center + y, center - x, center - z, color, panel);
  AddTriangle(result, center + y, center - z, center + x, color, panel);
  AddTriangle(result, center - y, center + z, center + x, color, panel);
  AddTriangle(result, center - y, center - x, center + z, color, panel);
  AddTriangle(result, center - y, center - z, center - x, color, panel);
  AddTriangle(result, center - y, center + x, center - z, color, panel);
}

void AddArrow(Result& result, Vec3 start, Vec3 vector, float radius, Vec3 color, uint32_t panel) {
  const Vec3 end = start + vector;
  AddSegment(result, start, end, radius, color, panel);
  AddOctahedron(result, end, radius * 2.4f, color, panel);
}

std::vector<Vec3> IntersectTetraWithPlane(
    const Config& config,
    Vec3 plane_normal,
    float plane_offset) {
  constexpr std::array<std::array<size_t, 2>, 6> edges{{
    {{0u, 1u}}, {{0u, 2u}}, {{0u, 3u}}, {{1u, 2u}}, {{1u, 3u}}, {{2u, 3u}},
  }};
  std::array<float, 4> distance{};
  for (size_t i = 0u; i < 4u; ++i) {
    distance[i] = Dot(plane_normal, config.tetra[i]) - plane_offset;
    assert(distance[i] != 0.0f);
  }
  std::vector<Vec3> polygon;
  for (const auto& edge : edges) {
    const float da = distance[edge[0]];
    const float db = distance[edge[1]];
    if (da * db >= 0.0f) continue;
    const float t = da / (da - db);
    polygon.push_back(config.tetra[edge[0]] + (config.tetra[edge[1]] - config.tetra[edge[0]]) * t);
  }
  assert(polygon.size() == 3u || polygon.size() == 4u);
  Vec3 center{};
  for (Vec3 point : polygon) center += point;
  center = center * (1.0f / static_cast<float>(polygon.size()));
  const Vec3 reference = std::abs(plane_normal.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  const Vec3 axis_u = Normalize(Cross(reference, plane_normal));
  const Vec3 axis_v = Cross(plane_normal, axis_u);
  std::sort(polygon.begin(), polygon.end(), [&](Vec3 a, Vec3 b) {
    const Vec3 da = a - center;
    const Vec3 db = b - center;
    return std::atan2(Dot(da, axis_v), Dot(da, axis_u)) < std::atan2(Dot(db, axis_v), Dot(db, axis_u));
  });
  return polygon;
}

void EvaluateCutGeometry(
    const std::vector<Vec3>& polygon,
    float& area,
    Vec3& center) {
  Vec3 polygon_average{};
  for (Vec3 point : polygon) polygon_average += point;
  polygon_average = polygon_average * (1.0f / static_cast<float>(polygon.size()));
  Vec3 weighted_center{};
  for (size_t i = 0u; i < polygon.size(); ++i) {
    const Vec3 a = polygon[i];
    const Vec3 b = polygon[(i + 1u) % polygon.size()];
    const float triangle_area = 0.5f * Length(Cross(a - polygon_average, b - polygon_average));
    area += triangle_area;
    weighted_center += (polygon_average + a + b) * (triangle_area / 3.0f);
  }
  center = weighted_center * (1.0f / area);
}

Result Evaluate(const Config& config) {
  Result result{};
  result.center = (config.tetra[0] + config.tetra[1] + config.tetra[2] + config.tetra[3]) * 0.25f;
  const float determinant = Dot(
    config.tetra[1] - config.tetra[0],
    Cross(config.tetra[2] - config.tetra[0], config.tetra[3] - config.tetra[0]));
  assert(determinant > 0.0f);
  result.volume = determinant / 6.0f;

  constexpr std::array<std::array<size_t, 3>, 4> faces{{
    {{1u, 2u, 3u}}, {{0u, 3u, 2u}}, {{0u, 1u, 3u}}, {{0u, 2u, 1u}},
  }};
  constexpr std::array<std::array<float, 3>, 3> quadrature{{
    {{2.0f / 3.0f, 1.0f / 6.0f, 1.0f / 6.0f}},
    {{1.0f / 6.0f, 2.0f / 3.0f, 1.0f / 6.0f}},
    {{1.0f / 6.0f, 1.0f / 6.0f, 2.0f / 3.0f}},
  }};
  Mat3 stress_integral{};
  for (size_t face_index = 0u; face_index < faces.size(); ++face_index) {
    const auto face = faces[face_index];
    const Vec3 p0 = config.tetra[face[0]];
    const Vec3 p1 = config.tetra[face[1]];
    const Vec3 p2 = config.tetra[face[2]];
    const Vec3 area_vector = Cross(p1 - p0, p2 - p0) * 0.5f;
    const float area = Length(area_vector);
    const Vec3 outward_normal = area_vector * (1.0f / area);
    FaceResult& face_result = result.faces[face_index];
    face_result.center = (p0 + p1 + p2) * (1.0f / 3.0f);
    for (const auto& barycentric : quadrature) {
      FaceSample sample{};
      sample.position = p0 * barycentric[0] + p1 * barycentric[1] + p2 * barycentric[2];
      const SurfaceFieldSample field = SampleSurfaceField(config, sample.position, outward_normal);
      sample.arrival = field.arrival;
      sample.pressure = field.pressure;
      sample.force = field.traction * (area / 3.0f);
      result.external_force += sample.force;
      result.blast_force += field.blast_traction * (area / 3.0f);
      result.external_pressure_force += field.external_pressure_traction * (area / 3.0f);
      result.external_moment += Cross(sample.position - result.center, sample.force);
      stress_integral = stress_integral + Outer(sample.force, sample.position - result.center);
      face_result.force += sample.force;
      face_result.maximum_pressure = std::max(face_result.maximum_pressure, sample.pressure);
      result.minimum_arrival = std::min(result.minimum_arrival, sample.arrival);
      result.maximum_arrival = std::max(result.maximum_arrival, sample.arrival);
      result.maximum_pressure = std::max(result.maximum_pressure, sample.pressure);
      for (size_t local = 0u; local < 3u; ++local) {
        result.nodal_force[face[local]] += sample.force * barycentric[local];
      }
    }
    face_result.loaded = face_result.maximum_pressure > 0.0f;
    result.loaded_face_count += face_result.loaded ? 1u : 0u;
  }
  for (size_t node = 0u; node < 4u; ++node) {
    result.nodal_resultant += result.nodal_force[node];
    result.nodal_moment += Cross(config.tetra[node] - result.center, result.nodal_force[node]);
  }
  result.stress = (stress_integral + Transpose(stress_integral)) * (0.5f / result.volume);
  const float force_residual = Length(result.nodal_resultant - result.external_force);
  const float moment_residual = Length(result.nodal_moment - result.external_moment);
  assert(force_residual <= std::max(Length(result.external_force), 1.0f) * 1.0e-5f);
  assert(moment_residual <= std::max(Length(result.external_moment), 1.0f) * 1.0e-5f);

  result.cut_polygon = IntersectTetraWithPlane(config, config.plane_normal, config.plane_offset);
  EvaluateCutGeometry(result.cut_polygon, result.cut_area, result.cut_center);
  result.traction = result.stress * config.plane_normal;
  result.cut_force = result.traction * result.cut_area;
  result.opposite_cut_force = (result.stress * (-config.plane_normal)) * result.cut_area;
  const float signed_normal_traction = Dot(result.traction, config.plane_normal);
  result.normal_traction = std::max(signed_normal_traction, 0.0f);
  result.shear_traction = Length(result.traction - config.plane_normal * signed_normal_traction);
  const float normal_ratio = result.normal_traction / config.tensile_strength;
  const float shear_ratio = result.shear_traction / config.shear_strength;
  result.fracture_index = normal_ratio * normal_ratio + shear_ratio * shear_ratio;
  result.secondary_cut_polygon = IntersectTetraWithPlane(
    config,
    config.secondary_plane_normal,
    config.secondary_plane_offset);
  EvaluateCutGeometry(
    result.secondary_cut_polygon,
    result.secondary_cut_area,
    result.secondary_cut_center);
  result.secondary_traction = result.stress * config.secondary_plane_normal;
  const float secondary_signed_normal_traction = Dot(
    result.secondary_traction,
    config.secondary_plane_normal);
  result.secondary_normal_traction = std::max(secondary_signed_normal_traction, 0.0f);
  result.secondary_shear_traction = Length(
    result.secondary_traction -
    config.secondary_plane_normal * secondary_signed_normal_traction);
  const float secondary_normal_ratio = result.secondary_normal_traction / config.tensile_strength;
  const float secondary_shear_ratio = result.secondary_shear_traction / config.shear_strength;
  result.secondary_fracture_index =
    secondary_normal_ratio * secondary_normal_ratio +
    secondary_shear_ratio * secondary_shear_ratio;
  return result;
}

float TetraVolume(const ChildTetra& tetra) {
  return Dot(
    tetra[1] - tetra[0],
    Cross(tetra[2] - tetra[0], tetra[3] - tetra[0])) / 6.0f;
}

std::array<ChildTetra, 8> BuildFull8TetrahedralCapGroup(const Config& config) {
  const std::array<Vec3, 10> node{{
    config.tetra[0],
    config.tetra[1],
    config.tetra[2],
    config.tetra[3],
    (config.tetra[0] + config.tetra[1]) * 0.5f,
    (config.tetra[0] + config.tetra[2]) * 0.5f,
    (config.tetra[0] + config.tetra[3]) * 0.5f,
    (config.tetra[1] + config.tetra[2]) * 0.5f,
    (config.tetra[1] + config.tetra[3]) * 0.5f,
    (config.tetra[2] + config.tetra[3]) * 0.5f,
  }};
  constexpr std::array<std::array<size_t, 4>, 8> topology{{
    {{0u, 4u, 5u, 6u}},
    {{1u, 4u, 7u, 8u}},
    {{2u, 5u, 7u, 9u}},
    {{3u, 6u, 8u, 9u}},
    {{4u, 5u, 6u, 9u}},
    {{4u, 5u, 7u, 9u}},
    {{4u, 6u, 8u, 9u}},
    {{4u, 7u, 8u, 9u}},
  }};
  std::array<ChildTetra, 8> child{};
  for (size_t i = 0u; i < child.size(); ++i) {
    child[i] = {
      node[topology[i][0]],
      node[topology[i][1]],
      node[topology[i][2]],
      node[topology[i][3]],
    };
    const float signed_volume = TetraVolume(child[i]);
    assert(signed_volume != 0.0f);
    if (signed_volume < 0.0f) std::swap(child[i][1], child[i][2]);
  }
  return child;
}

struct FacePoint {
  Vec3 position{};
  std::array<float, 3> parent_barycentric{};
};

RefineDecision EvaluateFull8Admission(
    const Config& config,
    const Result& coarse) {
  constexpr std::array<std::array<size_t, 3>, 4> faces{{
    {{1u, 2u, 3u}}, {{0u, 3u, 2u}}, {{0u, 1u, 3u}}, {{0u, 2u, 1u}},
  }};
  constexpr std::array<std::array<float, 3>, 3> quadrature{{
    {{2.0f / 3.0f, 1.0f / 6.0f, 1.0f / 6.0f}},
    {{1.0f / 6.0f, 2.0f / 3.0f, 1.0f / 6.0f}},
    {{1.0f / 6.0f, 1.0f / 6.0f, 2.0f / 3.0f}},
  }};
  constexpr std::array<std::array<size_t, 3>, 4> subface_topology{{
    {{0u, 3u, 5u}},
    {{3u, 1u, 4u}},
    {{5u, 4u, 2u}},
    {{3u, 4u, 5u}},
  }};

  RefineDecision decision{};
  decision.coarse_sample_count = 12u;
  float maximum_traction_error = 0.0f;
  float maximum_fine_pressure = 0.0f;
  float accumulated_subface_force_error = 0.0f;
  for (const auto& face : faces) {
    const Vec3 p0 = config.tetra[face[0]];
    const Vec3 p1 = config.tetra[face[1]];
    const Vec3 p2 = config.tetra[face[2]];
    const Vec3 area_vector = Cross(p1 - p0, p2 - p0) * 0.5f;
    const Vec3 outward_normal = Normalize(area_vector);
    std::array<Vec3, 3> coarse_traction{};
    Vec3 coarse_traction_sum{};
    for (size_t q = 0u; q < quadrature.size(); ++q) {
      const Vec3 position =
        p0 * quadrature[q][0] +
        p1 * quadrature[q][1] +
        p2 * quadrature[q][2];
      coarse_traction[q] = SampleSurfaceField(config, position, outward_normal).traction;
      coarse_traction_sum += coarse_traction[q];
    }
    const Vec3 mean = coarse_traction_sum * (1.0f / 3.0f);
    const std::array<Vec3, 3> projected_vertex_traction{{
      coarse_traction[0] * 2.0f - mean,
      coarse_traction[1] * 2.0f - mean,
      coarse_traction[2] * 2.0f - mean,
    }};
    const std::array<FacePoint, 6> point{{
      {p0, {1.0f, 0.0f, 0.0f}},
      {p1, {0.0f, 1.0f, 0.0f}},
      {p2, {0.0f, 0.0f, 1.0f}},
      {(p0 + p1) * 0.5f, {0.5f, 0.5f, 0.0f}},
      {(p1 + p2) * 0.5f, {0.0f, 0.5f, 0.5f}},
      {(p2 + p0) * 0.5f, {0.5f, 0.0f, 0.5f}},
    }};
    for (const auto& subface : subface_topology) {
      const FacePoint& a = point[subface[0]];
      const FacePoint& b = point[subface[1]];
      const FacePoint& c = point[subface[2]];
      const float subface_area = 0.5f * Length(Cross(b.position - a.position, c.position - a.position));
      Vec3 true_subface_force{};
      Vec3 projected_subface_force{};
      for (const auto& local_barycentric : quadrature) {
        const Vec3 position =
          a.position * local_barycentric[0] +
          b.position * local_barycentric[1] +
          c.position * local_barycentric[2];
        std::array<float, 3> parent_barycentric{};
        for (size_t axis = 0u; axis < 3u; ++axis) {
          parent_barycentric[axis] =
            a.parent_barycentric[axis] * local_barycentric[0] +
            b.parent_barycentric[axis] * local_barycentric[1] +
            c.parent_barycentric[axis] * local_barycentric[2];
        }
        const SurfaceFieldSample fine_sample = SampleSurfaceField(config, position, outward_normal);
        const Vec3 projected_traction =
          projected_vertex_traction[0] * parent_barycentric[0] +
          projected_vertex_traction[1] * parent_barycentric[1] +
          projected_vertex_traction[2] * parent_barycentric[2];
        maximum_traction_error = std::max(
          maximum_traction_error,
          Length(fine_sample.traction - projected_traction));
        maximum_fine_pressure = std::max(maximum_fine_pressure, fine_sample.pressure);
        const Vec3 fine_force = fine_sample.traction * (subface_area / 3.0f);
        const Vec3 projected_force = projected_traction * (subface_area / 3.0f);
        true_subface_force += fine_force;
        projected_subface_force += projected_force;
        decision.fine_force += fine_force;
        decision.fine_moment += Cross(position - coarse.center, fine_force);
        ++decision.fine_sample_count;
      }
      accumulated_subface_force_error += Length(true_subface_force - projected_subface_force);
    }
  }
  float parent_length = 0.0f;
  for (size_t a = 0u; a < 4u; ++a) {
    for (size_t b = a + 1u; b < 4u; ++b) {
      parent_length = std::max(parent_length, Length(config.tetra[b] - config.tetra[a]));
    }
  }
  decision.pressure_error =
    maximum_traction_error /
    (config.pressure_tolerance_ratio * maximum_fine_pressure);
  decision.force_error =
    Length(decision.fine_force - coarse.external_force) /
    (config.force_tolerance_ratio * Length(decision.fine_force));
  decision.subface_force_error =
    accumulated_subface_force_error /
    (config.force_tolerance_ratio * Length(decision.fine_force));
  decision.moment_error =
    Length(decision.fine_moment - coarse.external_moment) /
    (config.moment_tolerance_ratio * Length(decision.fine_force) * parent_length);
  decision.refine_score = std::max(
    std::max(decision.pressure_error, decision.force_error),
    std::max(decision.subface_force_error, decision.moment_error));
  if (decision.pressure_error > config.refine_threshold) decision.reason_mask |= 1u;
  if (decision.force_error > config.refine_threshold) decision.reason_mask |= 2u;
  if (decision.subface_force_error > config.refine_threshold) decision.reason_mask |= 4u;
  if (decision.moment_error > config.refine_threshold) decision.reason_mask |= 8u;
  decision.enter_precision_group = decision.refine_score > config.refine_threshold;
  return decision;
}

void ExecutePrecisionGroupQueue(
    const Config& config,
    Result& result) {
  result.refine_decision = EvaluateFull8Admission(config, result);
  std::vector<PrecisionGroup> queue;
  if (result.refine_decision.enter_precision_group) {
    PrecisionGroup group{};
    group.decision = result.refine_decision;
    group.active_leaf_tetra = BuildFull8TetrahedralCapGroup(config);
    queue.push_back(group);
  }
  while (!queue.empty()) {
    PrecisionGroup group = queue.back();
    queue.pop_back();
    for (const ChildTetra& child : group.active_leaf_tetra) {
      group.child_volume_sum += TetraVolume(child);
    }
    const RefineDecision executed = EvaluateFull8Admission(config, result);
    group.execution_checksum =
      Length(executed.fine_force) +
      Length(executed.fine_moment) +
      executed.refine_score +
      group.child_volume_sum;
    group.executed = true;
    assert(std::abs(group.child_volume_sum - result.volume) <= result.volume * 1.0e-5f);
    assert(std::abs(executed.refine_score - group.decision.refine_score) <= 1.0e-6f);
    result.precision_leaf_tetra = group.active_leaf_tetra;
    result.precision_group_count = 1u;
    result.accepted_lg_depth = 1u;
    result.precision_leaf_count = 8u;
    result.precision_group_executed = group.executed ? 1u : 0u;
    result.precision_child_volume_sum = group.child_volume_sum;
    result.precision_execution_checksum = group.execution_checksum;
  }
  if (!result.refine_decision.enter_precision_group) {
    result.accepted_lg_depth = 0u;
    result.precision_leaf_count = 1u;
  }
}

using FineFaceKey = std::array<uint32_t, 3>;
using FineEdgeKey = std::array<uint32_t, 2>;

FineFaceKey MakeFineFaceKey(std::array<uint32_t, 3> face) {
  std::sort(face.begin(), face.end());
  return face;
}

FineEdgeKey MakeFineEdgeKey(uint32_t a, uint32_t b) {
  if (a > b) std::swap(a, b);
  return {a, b};
}

float IndexedTetraVolume(
    const std::vector<Vec3>& nodes,
    const std::array<uint32_t, 4>& tetra) {
  return Dot(
    nodes[tetra[1]] - nodes[tetra[0]],
    Cross(nodes[tetra[2]] - nodes[tetra[0]], nodes[tetra[3]] - nodes[tetra[0]])) / 6.0f;
}

std::array<std::array<uint32_t, 3>, 4> FineTetraFaces(const FineTetra& tetra) {
  return {{
    {tetra.node[1], tetra.node[2], tetra.node[3]},
    {tetra.node[0], tetra.node[3], tetra.node[2]},
    {tetra.node[0], tetra.node[1], tetra.node[3]},
    {tetra.node[0], tetra.node[2], tetra.node[1]},
  }};
}

void BuildUniformMinusLg4Mesh(const Config& config, Result& result) {
  result.fine_nodes.assign(config.tetra.begin(), config.tetra.end());
  std::vector<std::array<uint32_t, 4>> active{{0u, 1u, 2u, 3u}};
  for (uint32_t level = 0u; level < 4u; ++level) {
    std::map<FineEdgeKey, uint32_t> midpoint;
    auto midpoint_node = [&](uint32_t a, uint32_t b) {
      const FineEdgeKey key = MakeFineEdgeKey(a, b);
      const auto found = midpoint.find(key);
      if (found != midpoint.end()) return found->second;
      const uint32_t index = static_cast<uint32_t>(result.fine_nodes.size());
      result.fine_nodes.push_back((result.fine_nodes[a] + result.fine_nodes[b]) * 0.5f);
      midpoint.emplace(key, index);
      return index;
    };
    std::vector<std::array<uint32_t, 4>> next;
    next.reserve(active.size() * 8u);
    for (const auto& tetra : active) {
      const uint32_t m01 = midpoint_node(tetra[0], tetra[1]);
      const uint32_t m02 = midpoint_node(tetra[0], tetra[2]);
      const uint32_t m03 = midpoint_node(tetra[0], tetra[3]);
      const uint32_t m12 = midpoint_node(tetra[1], tetra[2]);
      const uint32_t m13 = midpoint_node(tetra[1], tetra[3]);
      const uint32_t m23 = midpoint_node(tetra[2], tetra[3]);
      std::array<std::array<uint32_t, 4>, 8> child{{
        {{tetra[0], m01, m02, m03}},
        {{tetra[1], m01, m12, m13}},
        {{tetra[2], m02, m12, m23}},
        {{tetra[3], m03, m13, m23}},
        {{m01, m02, m03, m23}},
        {{m01, m02, m12, m23}},
        {{m01, m03, m13, m23}},
        {{m01, m12, m13, m23}},
      }};
      for (auto& fine : child) {
        const float signed_volume = IndexedTetraVolume(result.fine_nodes, fine);
        assert(signed_volume != 0.0f);
        if (signed_volume < 0.0f) std::swap(fine[1], fine[2]);
        next.push_back(fine);
      }
    }
    active = std::move(next);
  }
  assert(active.size() == 4096u);
  result.fine_tetra.resize(active.size());
  for (size_t i = 0u; i < active.size(); ++i) {
    FineTetra& tetra = result.fine_tetra[i];
    tetra.node = active[i];
    tetra.volume = IndexedTetraVolume(result.fine_nodes, tetra.node);
    assert(tetra.volume > 0.0f);
    tetra.center = (
      result.fine_nodes[tetra.node[0]] +
      result.fine_nodes[tetra.node[1]] +
      result.fine_nodes[tetra.node[2]] +
      result.fine_nodes[tetra.node[3]]) * 0.25f;
    result.fine_volume_sum += tetra.volume;
  }
  assert(std::abs(result.fine_volume_sum - result.volume) <= result.volume * 1.0e-5f);
}

void BuildFineAdjacency(Result& result) {
  std::map<FineFaceKey, std::vector<FineFaceOwner>> owners;
  for (size_t tetra_index = 0u; tetra_index < result.fine_tetra.size(); ++tetra_index) {
    for (const auto& face : FineTetraFaces(result.fine_tetra[tetra_index])) {
      owners[MakeFineFaceKey(face)].push_back({
        static_cast<uint32_t>(tetra_index),
        face,
      });
    }
  }
  for (const auto& pair : owners) {
    assert(pair.second.size() == 1u || pair.second.size() == 2u);
    if (pair.second.size() == 1u) {
      result.fine_boundaries.push_back({
        pair.second[0].tetra,
        pair.second[0].oriented,
      });
      continue;
    }
    FineInterface interface_face{};
    interface_face.left = pair.second[0].tetra;
    interface_face.right = pair.second[1].tetra;
    interface_face.oriented = pair.second[0].oriented;
    const Vec3 a = result.fine_nodes[interface_face.oriented[0]];
    const Vec3 b = result.fine_nodes[interface_face.oriented[1]];
    const Vec3 c = result.fine_nodes[interface_face.oriented[2]];
    const Vec3 area_vector = Cross(b - a, c - a) * 0.5f;
    interface_face.area = Length(area_vector);
    interface_face.normal = area_vector * (1.0f / interface_face.area);
    result.fine_interfaces.push_back(interface_face);
  }
  assert(result.fine_boundaries.size() == 1024u);
  assert(result.fine_interfaces.size() == 7680u);
}

void AuditFragmentSurfaceClosure(Result& result) {
  std::vector<std::map<FineEdgeKey, uint32_t>> edge_incidence(result.fragment_count);
  const auto add_face = [&](uint32_t component, const std::array<uint32_t, 3>& face) {
    ++edge_incidence[component][MakeFineEdgeKey(face[0], face[1])];
    ++edge_incidence[component][MakeFineEdgeKey(face[1], face[2])];
    ++edge_incidence[component][MakeFineEdgeKey(face[2], face[0])];
  };
  for (const FineBoundaryFace& face : result.fine_boundaries) {
    add_face(result.fine_tetra[face.tetra].component, face.oriented);
  }
  for (const FineInterface& face : result.fine_interfaces) {
    if (!face.broken) continue;
    add_face(result.fine_tetra[face.left].component, face.oriented);
    add_face(result.fine_tetra[face.right].component, face.oriented);
  }
  for (size_t component = 0u; component < edge_incidence.size(); ++component) {
    const auto& component_edges = edge_incidence[component];
    for (const auto& edge : component_edges) {
      if (edge.second != 2u) {
        ++result.fragment_surface_bad_edge_count;
        std::fprintf(
          stderr,
          "tetra_blast_fracture.bad_surface_edge component=%zu edge=(%u,%u) incidence=%u p0=(%.9f,%.9f,%.9f) p1=(%.9f,%.9f,%.9f)\n",
          component,
          edge.first[0],
          edge.first[1],
          edge.second,
          result.fine_nodes[edge.first[0]].x,
          result.fine_nodes[edge.first[0]].y,
          result.fine_nodes[edge.first[0]].z,
          result.fine_nodes[edge.first[1]].x,
          result.fine_nodes[edge.first[1]].y,
          result.fine_nodes[edge.first[1]].z);
      }
    }
  }
  assert(result.fragment_surface_bad_edge_count == 0u);
  if (result.fragment_surface_bad_edge_count != 0u) std::abort();
}

uint32_t IdealPartitionLabel(const Config& config, Vec3 point) {
  const float primary_distance = Dot(config.plane_normal, point) - config.plane_offset;
  if (primary_distance < 0.0f) return 0u;
  const float secondary_distance =
    Dot(config.secondary_plane_normal, point) - config.secondary_plane_offset;
  return secondary_distance < 0.0f ? 1u : 2u;
}

float PartitionWrongDistance(const Config& config, Vec3 point, uint32_t label) {
  const float primary_distance = Dot(config.plane_normal, point) - config.plane_offset;
  const float secondary_distance =
    Dot(config.secondary_plane_normal, point) - config.secondary_plane_offset;
  if (label == 0u) return std::max(primary_distance, 0.0f);
  if (label == 1u) {
    return std::max(-primary_distance, 0.0f) + std::max(secondary_distance, 0.0f);
  }
  assert(label == 2u);
  return std::max(-primary_distance, 0.0f) + std::max(-secondary_distance, 0.0f);
}

void CommitMinusLg4Fracture(const Config& config, Result& result) {
  std::fprintf(stderr, "tetra_blast_fracture.stage=build_lg4_begin\n");
  ProfileClock::time_point stage_begin = ProfileClock::now();
  BuildUniformMinusLg4Mesh(config, result);
  result.timing.mesh_ms = ElapsedMilliseconds(stage_begin);
  std::fprintf(stderr, "tetra_blast_fracture.stage=build_lg4_end nodes=%zu tetra=%zu\n", result.fine_nodes.size(), result.fine_tetra.size());
  stage_begin = ProfileClock::now();
  BuildFineAdjacency(result);
  result.timing.adjacency_ms = ElapsedMilliseconds(stage_begin);
  result.fragments.resize(3u);
  std::fprintf(stderr, "tetra_blast_fracture.stage=adjacency_end interfaces=%zu boundaries=%zu\n", result.fine_interfaces.size(), result.fine_boundaries.size());
  if (result.fracture_index <= 1.0f || result.secondary_fracture_index <= 1.0f) {
    result.accepted_lg_depth = 4u;
    result.precision_group_count = 1u;
    result.precision_leaf_count = static_cast<uint32_t>(result.fine_tetra.size());
    result.precision_group_executed = 1u;
    return;
  }

  stage_begin = ProfileClock::now();
  std::vector<std::vector<uint32_t>> dual_adjacency(result.fine_tetra.size());
  for (const FineInterface& interface_face : result.fine_interfaces) {
    dual_adjacency[interface_face.left].push_back(interface_face.right);
    dual_adjacency[interface_face.right].push_back(interface_face.left);
  }
  std::array<uint32_t, 3> seed{};
  std::array<float, 3> seed_margin{{
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
  }};
  for (size_t i = 0u; i < result.fine_tetra.size(); ++i) {
    const Vec3 center = result.fine_tetra[i].center;
    const float primary_distance = Dot(config.plane_normal, center) - config.plane_offset;
    const float secondary_distance = Dot(config.secondary_plane_normal, center) - config.secondary_plane_offset;
    const std::array<float, 3> margin{{
      -primary_distance,
      std::min(primary_distance, -secondary_distance),
      std::min(primary_distance, secondary_distance),
    }};
    for (uint32_t label = 0u; label < 3u; ++label) {
      if (margin[label] > seed_margin[label]) {
        seed_margin[label] = margin[label];
        seed[label] = static_cast<uint32_t>(i);
      }
    }
  }
  for (float margin : seed_margin) assert(margin > 0.0f);

  float parent_length = 0.0f;
  for (size_t a = 0u; a < 4u; ++a) {
    for (size_t b = a + 1u; b < 4u; ++b) {
      parent_length = std::max(parent_length, Length(config.tetra[b] - config.tetra[a]));
    }
  }
  const float fine_length = parent_length / 16.0f;
  struct WavefrontNode {
    float cost{};
    uint32_t tetra{};
    uint32_t label{};
  };
  struct WavefrontCompare {
    bool operator()(const WavefrontNode& a, const WavefrontNode& b) const {
      return a.cost > b.cost;
    }
  };
  std::priority_queue<WavefrontNode, std::vector<WavefrontNode>, WavefrontCompare> wavefront;
  std::vector<int32_t> partition(result.fine_tetra.size(), -1);
  for (uint32_t label = 0u; label < 3u; ++label) wavefront.push({0.0f, seed[label], label});
  while (!wavefront.empty()) {
    const WavefrontNode current = wavefront.top();
    wavefront.pop();
    if (partition[current.tetra] >= 0) continue;
    partition[current.tetra] = static_cast<int32_t>(current.label);
    for (uint32_t neighbor : dual_adjacency[current.tetra]) {
      if (partition[neighbor] >= 0) continue;
      const float wrong_distance = PartitionWrongDistance(
        config,
        result.fine_tetra[neighbor].center,
        current.label);
      wavefront.push({
        current.cost + 1.0f + 128.0f * wrong_distance / fine_length,
        neighbor,
        current.label,
      });
    }
  }

  result.timing.partition_ms = ElapsedMilliseconds(stage_begin);
  stage_begin = ProfileClock::now();
  std::map<FineEdgeKey, std::vector<uint32_t>> tetra_around_edge;
  constexpr std::array<std::array<size_t, 2>, 6> tetra_edges{{
    {{0u, 1u}}, {{0u, 2u}}, {{0u, 3u}}, {{1u, 2u}}, {{1u, 3u}}, {{2u, 3u}},
  }};
  for (size_t tetra_index = 0u; tetra_index < result.fine_tetra.size(); ++tetra_index) {
    const FineTetra& tetra = result.fine_tetra[tetra_index];
    for (const auto& edge : tetra_edges) {
      tetra_around_edge[MakeFineEdgeKey(
        tetra.node[edge[0]],
        tetra.node[edge[1]])].push_back(static_cast<uint32_t>(tetra_index));
    }
  }
  for (uint32_t pass = 0u; pass < 32u; ++pass) {
    std::array<std::map<FineEdgeKey, uint32_t>, 3> edge_incidence;
    const auto add_face = [&](uint32_t label, const std::array<uint32_t, 3>& face) {
      ++edge_incidence[label][MakeFineEdgeKey(face[0], face[1])];
      ++edge_incidence[label][MakeFineEdgeKey(face[1], face[2])];
      ++edge_incidence[label][MakeFineEdgeKey(face[2], face[0])];
    };
    for (const FineBoundaryFace& face : result.fine_boundaries) {
      add_face(static_cast<uint32_t>(partition[face.tetra]), face.oriented);
    }
    for (const FineInterface& face : result.fine_interfaces) {
      const uint32_t left_label = static_cast<uint32_t>(partition[face.left]);
      const uint32_t right_label = static_cast<uint32_t>(partition[face.right]);
      if (left_label == right_label) continue;
      add_face(left_label, face.oriented);
      add_face(right_label, face.oriented);
    }
    std::map<FineEdgeKey, uint32_t> nonmanifold_edges;
    for (uint32_t label = 0u; label < 3u; ++label) {
      for (const auto& edge : edge_incidence[label]) {
        if (edge.second != 2u) nonmanifold_edges.emplace(edge.first, 0u);
      }
    }
    if (nonmanifold_edges.empty()) break;
    ++result.partition_regularization_count;
    for (const auto& edge : nonmanifold_edges) {
      const Vec3 midpoint =
        (result.fine_nodes[edge.first[0]] + result.fine_nodes[edge.first[1]]) * 0.5f;
      const uint32_t label = IdealPartitionLabel(config, midpoint);
      for (uint32_t tetra : tetra_around_edge[edge.first]) {
        partition[tetra] = static_cast<int32_t>(label);
      }
    }
  }
  std::fprintf(
    stderr,
    "tetra_blast_fracture.stage=partition_regularized passes=%u\n",
    result.partition_regularization_count);

  for (FineInterface& interface_face : result.fine_interfaces) {
    const uint32_t left_label = static_cast<uint32_t>(partition[interface_face.left]);
    const uint32_t right_label = static_cast<uint32_t>(partition[interface_face.right]);
    interface_face.broken = left_label != right_label;
    if (!interface_face.broken) continue;
    interface_face.fracture_direction = left_label == 0u || right_label == 0u ? 1u : 2u;
    ++result.broken_face_count;
    result.crack_area += interface_face.area;
    if (interface_face.fracture_direction == 1u) {
      ++result.primary_broken_face_count;
    } else {
      ++result.secondary_broken_face_count;
    }
  }
  result.timing.regularization_ms = ElapsedMilliseconds(stage_begin);
  assert(result.primary_broken_face_count > 0u);
  assert(result.secondary_broken_face_count > 0u);
  std::fprintf(
    stderr,
    "tetra_blast_fracture.stage=candidates_end broken=%u primary=%u secondary=%u area=%.9f\n",
    result.broken_face_count,
    result.primary_broken_face_count,
    result.secondary_broken_face_count,
    result.crack_area);

  stage_begin = ProfileClock::now();
  std::vector<uint32_t> parent(result.fine_tetra.size());
  for (size_t i = 0u; i < parent.size(); ++i) parent[i] = static_cast<uint32_t>(i);
  const auto find_root = [&](uint32_t value) {
    uint32_t root = value;
    while (parent[root] != root) root = parent[root];
    while (parent[value] != value) {
      const uint32_t next = parent[value];
      parent[value] = root;
      value = next;
    }
    return root;
  };
  const auto unite = [&](uint32_t a, uint32_t b) {
    const uint32_t root_a = find_root(a);
    const uint32_t root_b = find_root(b);
    if (root_a != root_b) parent[root_b] = root_a;
  };
  for (const FineInterface& interface_face : result.fine_interfaces) {
    if (!interface_face.broken) unite(interface_face.left, interface_face.right);
  }
  std::map<uint32_t, uint32_t> component_for_root;
  for (size_t i = 0u; i < result.fine_tetra.size(); ++i) {
    component_for_root.emplace(find_root(static_cast<uint32_t>(i)), 0u);
  }
  result.fragment_count = static_cast<uint32_t>(component_for_root.size());
  assert(result.fragment_count == 3u);
  if (result.fragment_count != 3u) std::abort();
  std::fprintf(stderr, "tetra_blast_fracture.stage=components_end fragments=%u\n", result.fragment_count);
  uint32_t next_component = 0u;
  for (auto& pair : component_for_root) pair.second = next_component++;
  for (size_t i = 0u; i < result.fine_tetra.size(); ++i) {
    FineTetra& tetra = result.fine_tetra[i];
    tetra.component = component_for_root[find_root(static_cast<uint32_t>(i))];
    FineFragment& fragment = result.fragments[tetra.component];
    const float mass = config.density * tetra.volume;
    fragment.mass += mass;
    fragment.center += tetra.center * mass;
  }
  float fragment_mass_sum = 0.0f;
  for (FineFragment& fragment : result.fragments) {
    fragment.center = fragment.center * (1.0f / fragment.mass);
    fragment_mass_sum += fragment.mass;
  }
  result.fracture_mass_residual = std::abs(fragment_mass_sum - config.density * result.volume);
  result.timing.components_ms = ElapsedMilliseconds(stage_begin);

  stage_begin = ProfileClock::now();
  const float gaussian_impulse_time = config.pulse_width * std::sqrt(3.14159265359f);
  std::vector<Vec3> fragment_momentum(result.fragment_count);
  for (const FineInterface& face : result.fine_interfaces) {
    if (!face.broken) continue;
    const uint32_t left_component = result.fine_tetra[face.left].component;
    const uint32_t right_component = result.fine_tetra[face.right].component;
    const Vec3 release_impulse =
      (result.stress * face.normal) * (face.area * gaussian_impulse_time);
    fragment_momentum[left_component] += release_impulse;
    fragment_momentum[right_component] += -release_impulse;
  }
  for (const FineBoundaryFace& face : result.fine_boundaries) {
    const Vec3 a = result.fine_nodes[face.oriented[0]];
    const Vec3 b = result.fine_nodes[face.oriented[1]];
    const Vec3 c = result.fine_nodes[face.oriented[2]];
    const Vec3 area_vector = Cross(b - a, c - a) * 0.5f;
    const float area = Length(area_vector);
    const Vec3 outward_normal = area_vector * (1.0f / area);
    const SurfaceFieldSample field = SampleSurfaceField(
      config,
      (a + b + c) * (1.0f / 3.0f),
      outward_normal);
    const Vec3 external_impulse =
      field.blast_traction * (area * gaussian_impulse_time) +
      field.external_pressure_traction * (area * config.external_pressure_duration);
    const uint32_t component = result.fine_tetra[face.tetra].component;
    fragment_momentum[component] += external_impulse;
    result.expected_external_impulse += external_impulse;
  }
  Vec3 resulting_momentum{};
  for (size_t component = 0u; component < result.fragments.size(); ++component) {
    FineFragment& fragment = result.fragments[component];
    fragment.velocity = fragment_momentum[component] * (1.0f / fragment.mass);
    fragment.translation = fragment.velocity * config.fracture_observation_time;
    resulting_momentum += fragment.velocity * fragment.mass;
  }
  result.fracture_momentum_residual = Length(
    resulting_momentum - result.expected_external_impulse);
  for (size_t a = 0u; a < result.fragments.size(); ++a) {
    for (size_t b = a + 1u; b < result.fragments.size(); ++b) {
      result.crack_opening_distance = std::max(
        result.crack_opening_distance,
        Length(result.fragments[a].translation - result.fragments[b].translation));
    }
  }
  assert(result.fracture_momentum_residual <=
    std::max(Length(result.expected_external_impulse), 1.0f) * 1.0e-5f);
  result.timing.impulse_ms = ElapsedMilliseconds(stage_begin);
  std::fprintf(stderr, "tetra_blast_fracture.stage=closure_begin\n");
  stage_begin = ProfileClock::now();
  AuditFragmentSurfaceClosure(result);
  result.timing.closure_ms = ElapsedMilliseconds(stage_begin);
  std::fprintf(stderr, "tetra_blast_fracture.stage=closure_end\n");
  result.fracture_checksum =
    static_cast<float>(result.fine_nodes.size()) +
    static_cast<float>(result.fine_tetra.size()) +
    static_cast<float>(result.broken_face_count) +
    result.crack_area +
    fragment_mass_sum +
    result.crack_opening_distance;
  result.accepted_lg_depth = 4u;
  result.precision_group_count = 1u;
  result.precision_leaf_count = static_cast<uint32_t>(result.fine_tetra.size());
  result.precision_group_executed = 1u;
  result.fracture_committed = true;
}

void AddTetraWire(Result& result, const Config& config, float width, Vec3 color, uint32_t panel) {
  constexpr std::array<std::array<size_t, 2>, 6> edges{{
    {{0u, 1u}}, {{0u, 2u}}, {{0u, 3u}}, {{1u, 2u}}, {{1u, 3u}}, {{2u, 3u}},
  }};
  for (const auto& edge : edges) AddSegment(result, config.tetra[edge[0]], config.tetra[edge[1]], width, color, panel);
}

void AddPrecisionGroupWire(Result& result, float width, Vec3 color, uint32_t panel) {
  constexpr std::array<std::array<size_t, 2>, 6> edges{{
    {{0u, 1u}}, {{0u, 2u}}, {{0u, 3u}}, {{1u, 2u}}, {{1u, 3u}}, {{2u, 3u}},
  }};
  assert(result.precision_group_count == 1u);
  assert(result.precision_group_executed == 1u);
  for (const ChildTetra& child : result.precision_leaf_tetra) {
    for (const auto& edge : edges) {
      AddSegment(result, child[edge[0]], child[edge[1]], width, color, panel);
    }
  }
}

void AddCutPolygon(Result& result, Vec3 color, uint32_t panel) {
  for (size_t i = 1u; i + 1u < result.cut_polygon.size(); ++i) {
    AddTriangle(result, result.cut_polygon[0], result.cut_polygon[i], result.cut_polygon[i + 1u], color, panel);
  }
}

void AddSecondaryCutPolygon(Result& result, Vec3 color, uint32_t panel) {
  for (size_t i = 1u; i + 1u < result.secondary_cut_polygon.size(); ++i) {
    AddTriangle(
      result,
      result.secondary_cut_polygon[0],
      result.secondary_cut_polygon[i],
      result.secondary_cut_polygon[i + 1u],
      color,
      panel);
  }
}

Vec3 FractureRenderPoint(
    const Result& result,
    uint32_t node,
    uint32_t component) {
  return result.fine_nodes[node] + result.fragments[component].translation;
}

void AddFractureMesh(Result& result, uint32_t panel) {
  assert(result.fracture_committed);
  const std::array<Vec3, 3> fragment_color{{
    {0.16f, 0.48f, 0.92f},
    {0.72f, 0.78f, 0.86f},
    {0.18f, 0.76f, 0.48f},
  }};
  for (const FineBoundaryFace& face : result.fine_boundaries) {
    const uint32_t component = result.fine_tetra[face.tetra].component;
    AddTriangle(
      result,
      FractureRenderPoint(result, face.oriented[0], component),
      FractureRenderPoint(result, face.oriented[1], component),
      FractureRenderPoint(result, face.oriented[2], component),
      fragment_color[component],
      panel);
  }
  for (const FineInterface& face : result.fine_interfaces) {
    if (!face.broken) continue;
    const uint32_t left_component = result.fine_tetra[face.left].component;
    const uint32_t right_component = result.fine_tetra[face.right].component;
    assert(left_component != right_component);
    const Vec3 left_crack_color = face.fracture_direction == 1u
      ? Vec3{1.0f, 0.08f, 0.025f}
      : Vec3{0.92f, 0.08f, 0.88f};
    const Vec3 right_crack_color = face.fracture_direction == 1u
      ? Vec3{1.0f, 0.55f, 0.04f}
      : Vec3{0.12f, 0.95f, 0.92f};
    AddTriangle(
      result,
      FractureRenderPoint(result, face.oriented[0], left_component),
      FractureRenderPoint(result, face.oriented[1], left_component),
      FractureRenderPoint(result, face.oriented[2], left_component),
      left_crack_color,
      panel);
    AddTriangle(
      result,
      FractureRenderPoint(result, face.oriented[2], right_component),
      FractureRenderPoint(result, face.oriented[1], right_component),
      FractureRenderPoint(result, face.oriented[0], right_component),
      right_crack_color,
      panel);
  }
}

void BuildRender(const Config& config, Result& result) {
  float scale = 0.0f;
  for (Vec3 point : config.tetra) scale = std::max(scale, Length(point - result.center));
  const float line_width = scale * 0.018f;
  const Vec3 wire_color{0.18f, 0.72f, 1.0f};
  const Vec3 blast_color{1.0f, 0.20f, 0.04f};
  const Vec3 external_pressure_color{0.95f, 0.10f, 0.92f};
  const Vec3 plane_color = result.fracture_index > 1.0f ? Vec3{1.0f, 0.10f, 0.04f} : Vec3{0.12f, 0.92f, 0.42f};

  AddTetraWire(result, config, line_width, wire_color, 1u);
  AddCutPolygon(result, {0.18f, 0.62f, 0.95f}, 1u);
  AddSecondaryCutPolygon(result, {0.92f, 0.12f, 0.78f}, 1u);
  AddOctahedron(result, config.blast, scale * 0.09f, blast_color, 1u);
  AddArrow(result, config.blast, Normalize(result.center - config.blast) * scale * 0.42f, line_width * 1.2f, blast_color, 1u);
  AddArrow(
    result,
    result.center - config.external_pressure_direction * scale * 0.88f,
    config.external_pressure_direction * scale * 0.46f,
    line_width * 1.2f,
    external_pressure_color,
    1u);

  constexpr std::array<std::array<size_t, 3>, 4> faces{{
    {{1u, 2u, 3u}}, {{0u, 3u, 2u}}, {{0u, 1u, 3u}}, {{0u, 2u, 1u}},
  }};
  for (size_t face_index = 0u; face_index < faces.size(); ++face_index) {
    const float heat = result.faces[face_index].maximum_pressure / (config.peak_pressure + config.external_pressure);
    const Vec3 color{0.08f + 1.15f * heat, 0.22f + 0.28f * (1.0f - heat), 0.85f - 0.68f * heat};
    const auto face = faces[face_index];
    AddTriangle(result, config.tetra[face[0]], config.tetra[face[1]], config.tetra[face[2]], color, 2u);
    if (result.faces[face_index].loaded) {
      AddArrow(
        result,
        result.faces[face_index].center,
        Normalize(result.faces[face_index].force) * scale * (0.12f + 0.28f * heat),
        line_width,
        {1.0f, 0.72f, 0.06f},
        2u);
    }
  }
  AddOctahedron(result, config.blast, scale * 0.09f, blast_color, 2u);
  AddArrow(
    result,
    result.center - config.external_pressure_direction * scale * 0.88f,
    config.external_pressure_direction * scale * 0.46f,
    line_width * 1.2f,
    external_pressure_color,
    2u);

  if (result.fracture_committed) {
    AddFractureMesh(result, 3u);
  } else if (result.precision_group_executed != 0u) {
    AddPrecisionGroupWire(result, line_width * 0.55f, {0.18f, 1.0f, 0.42f}, 3u);
  } else {
    AddTetraWire(result, config, line_width, {0.28f, 0.48f, 0.68f}, 3u);
  }
  if (!result.fracture_committed) {
    AddCutPolygon(result, plane_color, 3u);
    AddSecondaryCutPolygon(result, {0.92f, 0.12f, 0.78f}, 3u);
    AddOctahedron(result, result.cut_center, scale * 0.055f, plane_color, 3u);
    AddArrow(result, result.cut_center, Normalize(result.cut_force) * scale * 0.38f, line_width, {0.95f, 0.70f, 0.08f}, 3u);
    AddArrow(result, result.cut_center, Normalize(result.opposite_cut_force) * scale * 0.38f, line_width, {0.14f, 0.72f, 1.0f}, 3u);
  }
}

void WriteResult(algorithm::AlgorithmContainerSet& set, const Config& config, const Result& result) {
  algorithm::AlgorithmContainer& frame_tick = RequireMutableContainer<uint32_t>(set, "frame_tick");
  algorithm::AlgorithmContainer& triangle_count = RequireMutableContainer<uint32_t>(set, "render_triangle_count");
  algorithm::AlgorithmContainer& loaded_face_count = RequireMutableContainer<uint32_t>(set, "loaded_face_count");
  algorithm::AlgorithmContainer& cut_vertex_count = RequireMutableContainer<uint32_t>(set, "cut_vertex_count");
  algorithm::AlgorithmContainer& fracture_candidate = RequireMutableContainer<uint32_t>(set, "fracture_candidate");
  algorithm::AlgorithmContainer& refine_decision = RequireMutableContainer<uint32_t>(set, "refine_decision");
  algorithm::AlgorithmContainer& precision_group_count = RequireMutableContainer<uint32_t>(set, "precision_group_count");
  algorithm::AlgorithmContainer& accepted_lg_depth = RequireMutableContainer<uint32_t>(set, "accepted_lg_depth");
  algorithm::AlgorithmContainer& precision_leaf_count = RequireMutableContainer<uint32_t>(set, "precision_leaf_count");
  algorithm::AlgorithmContainer& precision_group_executed = RequireMutableContainer<uint32_t>(set, "precision_group_executed");
  algorithm::AlgorithmContainer& fracture_committed = RequireMutableContainer<uint32_t>(set, "fracture_committed");
  algorithm::AlgorithmContainer& fine_node_count = RequireMutableContainer<uint32_t>(set, "fine_node_count");
  algorithm::AlgorithmContainer& fine_tetra_count = RequireMutableContainer<uint32_t>(set, "fine_tetra_count");
  algorithm::AlgorithmContainer& broken_face_count = RequireMutableContainer<uint32_t>(set, "broken_face_count");
  algorithm::AlgorithmContainer& fragment_count = RequireMutableContainer<uint32_t>(set, "fragment_count");
  algorithm::AlgorithmContainer& interface_face_count = RequireMutableContainer<uint32_t>(set, "interface_face_count");
  algorithm::AlgorithmContainer& metrics = RequireMutableContainer<float>(set, "blast_plane_metrics");
  algorithm::AlgorithmContainer& decision_packet = RequireMutableContainer<float>(set, "refine_decision_packet");
  algorithm::AlgorithmContainer& fracture_metrics = RequireMutableContainer<float>(set, "fracture_metrics");
  algorithm::AlgorithmContainer& scene = RequireMutableContainer<std::array<float, 4>>(set, "render_scene");
  algorithm::AlgorithmContainer& triangles = RequireMutableContainer<std::array<float, 4>>(set, "render_triangle_buffer");
  WriteUint32(frame_tick, ReadUint32(frame_tick) + 1u);
  WriteUint32(triangle_count, static_cast<uint32_t>(result.render_triangles.size()));
  WriteUint32(loaded_face_count, result.loaded_face_count);
  WriteUint32(cut_vertex_count, static_cast<uint32_t>(result.cut_polygon.size()));
  WriteUint32(fracture_candidate, result.fracture_index > 1.0f ? 1u : 0u);
  WriteUint32(refine_decision, result.refine_decision.enter_precision_group ? 1u : 0u);
  WriteUint32(precision_group_count, result.precision_group_count);
  WriteUint32(accepted_lg_depth, result.accepted_lg_depth);
  WriteUint32(precision_leaf_count, result.precision_leaf_count);
  WriteUint32(precision_group_executed, result.precision_group_executed);
  WriteUint32(fracture_committed, result.fracture_committed ? 1u : 0u);
  WriteUint32(fine_node_count, static_cast<uint32_t>(result.fine_nodes.size()));
  WriteUint32(fine_tetra_count, static_cast<uint32_t>(result.fine_tetra.size()));
  WriteUint32(broken_face_count, result.broken_face_count);
  WriteUint32(fragment_count, result.fragment_count);
  WriteUint32(interface_face_count, static_cast<uint32_t>(result.fine_interfaces.size()));
  const float force_residual = Length(result.nodal_resultant - result.external_force);
  const float moment_residual = Length(result.nodal_moment - result.external_moment);
  const float mass = config.density * result.volume;
  const float precision_volume_residual = result.precision_group_count != 0u
    ? std::abs(result.precision_child_volume_sum - result.volume)
    : 0.0f;
  const std::array<float, 32> values{{
    result.external_force.x,
    result.external_force.y,
    result.external_force.z,
    force_residual,
    moment_residual,
    result.cut_area,
    Length(result.cut_force),
    result.normal_traction,
    result.shear_traction,
    result.fracture_index,
    Length(result.cut_force + result.opposite_cut_force),
    result.minimum_arrival,
    result.maximum_arrival,
    result.maximum_pressure,
    mass,
    Length(result.external_force) / mass,
    result.refine_decision.pressure_error,
    result.refine_decision.force_error,
    result.refine_decision.subface_force_error,
    result.refine_decision.moment_error,
    result.refine_decision.refine_score,
    config.refine_threshold,
    static_cast<float>(result.refine_decision.reason_mask),
    static_cast<float>(result.precision_group_count),
    static_cast<float>(result.accepted_lg_depth),
    static_cast<float>(result.precision_leaf_count),
    static_cast<float>(result.precision_group_executed),
    result.volume,
    result.precision_child_volume_sum,
    precision_volume_residual,
    static_cast<float>(result.refine_decision.coarse_sample_count),
    static_cast<float>(result.refine_decision.fine_sample_count),
  }};
  for (size_t i = 0u; i < values.size(); ++i) WriteFloat(metrics, i, values[i]);
  const std::array<float, 16> packet{{
    static_cast<float>(result.refine_decision.cell_id),
    0.0f,
    -1.0f,
    static_cast<float>(result.refine_decision.refine_mode),
    result.refine_decision.refine_score,
    static_cast<float>(result.refine_decision.reason_mask),
    result.refine_decision.pressure_error,
    result.refine_decision.force_error,
    result.refine_decision.subface_force_error,
    result.refine_decision.moment_error,
    0.0f,
    static_cast<float>(result.precision_leaf_count),
    static_cast<float>(result.precision_group_executed),
    result.volume,
    result.precision_child_volume_sum,
    precision_volume_residual,
  }};
  for (size_t i = 0u; i < packet.size(); ++i) WriteFloat(decision_packet, i, packet[i]);
  float maximum_fragment_speed = 0.0f;
  for (const FineFragment& fragment : result.fragments) {
    maximum_fragment_speed = std::max(maximum_fragment_speed, Length(fragment.velocity));
  }
  const std::array<float, 32> fracture_values{{
    static_cast<float>(result.fine_nodes.size()),
    static_cast<float>(result.fine_tetra.size()),
    static_cast<float>(result.fine_interfaces.size()),
    static_cast<float>(result.fine_boundaries.size()),
    static_cast<float>(result.broken_face_count),
    static_cast<float>(result.fragment_count),
    result.crack_area,
    result.fine_volume_sum,
    std::abs(result.fine_volume_sum - result.volume),
    result.fragments[0].mass,
    result.fragments[1].mass,
    result.fracture_momentum_residual,
    result.crack_opening_distance,
    Length(result.fragments[0].velocity),
    Length(result.fragments[1].velocity),
    result.fracture_checksum,
    static_cast<float>(result.fragment_surface_bad_edge_count),
    result.fracture_mass_residual,
    result.crack_area / (result.cut_area + result.secondary_cut_area),
    result.fracture_momentum_residual /
      std::max(Length(result.expected_external_impulse), 1.0f),
    static_cast<float>(result.partition_regularization_count),
    Length(result.external_pressure_force),
    result.secondary_cut_area,
    result.secondary_normal_traction,
    result.secondary_shear_traction,
    result.secondary_fracture_index,
    static_cast<float>(result.primary_broken_face_count),
    static_cast<float>(result.secondary_broken_face_count),
    Length(result.expected_external_impulse),
    2.0f,
    maximum_fragment_speed,
    result.crack_area / (result.cut_area + result.secondary_cut_area),
  }};
  for (size_t i = 0u; i < fracture_values.size(); ++i) {
    WriteFloat(fracture_metrics, i, fracture_values[i]);
  }
  float tetra_scale = 0.0f;
  for (Vec3 point : config.tetra) tetra_scale = std::max(tetra_scale, Length(point - result.center));
  const Vec3 blast_view_center = (result.center + config.blast) * 0.5f;
  float blast_view_scale = Length(config.blast - blast_view_center);
  for (Vec3 point : config.tetra) blast_view_scale = std::max(blast_view_scale, Length(point - blast_view_center));
  WriteVec4(scene, 0u, result.center, tetra_scale * 1.55f);
  WriteVec4(scene, 1u, config.blast, blast_view_scale * 1.55f);
  for (size_t i = 0u; i < result.render_triangles.size(); ++i) {
    const RenderTriangle& triangle = result.render_triangles[i];
    WriteVec4(triangles, i * 4u + 0u, triangle.point[0], 1.0f);
    WriteVec4(triangles, i * 4u + 1u, triangle.point[1], 1.0f);
    WriteVec4(triangles, i * 4u + 2u, triangle.point[2], 1.0f);
    WriteVec4(triangles, i * 4u + 3u, triangle.color, static_cast<float>(triangle.panel));
  }
}

class TetraBlastPlaneProbeJobsExecutor final : public algomanager::bridge::IAlgorithmJobsExecutor {
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
    assert(algorithm_to_agent_signal);
    assert(debug_state);
    const ProfileClock::time_point total_begin = ProfileClock::now();
    const Config config = ReadConfig(*container_set);
    ProfileClock::time_point stage_begin = ProfileClock::now();
    Result result = Evaluate(config);
    result.timing.evaluate_ms = ElapsedMilliseconds(stage_begin);
    stage_begin = ProfileClock::now();
    ExecutePrecisionGroupQueue(config, result);
    result.timing.admission_ms = ElapsedMilliseconds(stage_begin);
    CommitMinusLg4Fracture(config, result);
    std::fprintf(stderr, "tetra_blast_fracture.stage=render_begin\n");
    stage_begin = ProfileClock::now();
    BuildRender(config, result);
    result.timing.render_ms = ElapsedMilliseconds(stage_begin);
    std::fprintf(stderr, "tetra_blast_fracture.stage=render_end triangles=%zu\n", result.render_triangles.size());
    stage_begin = ProfileClock::now();
    WriteResult(*container_set, config, result);
    result.timing.write_ms = ElapsedMilliseconds(stage_begin);
    result.timing.total_ms = ElapsedMilliseconds(total_begin);
    std::fprintf(stderr, "tetra_blast_fracture.stage=write_end\n");
    std::array<size_t, 3> panel_triangle_count{};
    for (const RenderTriangle& triangle : result.render_triangles) {
      ++panel_triangle_count[triangle.panel - 1u];
    }
    std::fprintf(
      stderr,
      "agent_adaptive_prism_tetra_blast_plane_probe force=(%.9f,%.9f,%.9f) force_residual=%.9g moment_residual=%.9g cut_area=%.9f cut_force=%.9f cut_balance_residual=%.9g normal_traction=%.9f shear_traction=%.9f fracture_index=%.9f arrival=(%.9f,%.9f) max_pressure=%.9f refine=(pressure=%.9f,force=%.9f,subface=%.9f,moment=%.9f,score=%.9f,threshold=%.9f,reasons=%u) precision=(groups=%u,accepted_depth=%u,leaves=%u,executed=%u,volume_residual=%.9g,checksum=%.9f) fracture=(committed=%u,nodes=%zu,tetra=%zu,interfaces=%zu,broken=%u,fragments=%u,area=%.9f,momentum_residual=%.9g,mass_residual=%.9g,bad_surface_edges=%u,opening=%.9f) loaded_faces=%u cut_vertices=%zu render_triangles=%zu panels=(%zu,%zu,%zu)\n",
      result.external_force.x,
      result.external_force.y,
      result.external_force.z,
      Length(result.nodal_resultant - result.external_force),
      Length(result.nodal_moment - result.external_moment),
      result.cut_area,
      Length(result.cut_force),
      Length(result.cut_force + result.opposite_cut_force),
      result.normal_traction,
      result.shear_traction,
      result.fracture_index,
      result.minimum_arrival,
      result.maximum_arrival,
      result.maximum_pressure,
      result.refine_decision.pressure_error,
      result.refine_decision.force_error,
      result.refine_decision.subface_force_error,
      result.refine_decision.moment_error,
      result.refine_decision.refine_score,
      config.refine_threshold,
      result.refine_decision.reason_mask,
      result.precision_group_count,
      result.accepted_lg_depth,
      result.precision_leaf_count,
      result.precision_group_executed,
      result.precision_group_count != 0u
        ? std::abs(result.precision_child_volume_sum - result.volume)
        : 0.0f,
      result.precision_execution_checksum,
      result.fracture_committed ? 1u : 0u,
      result.fine_nodes.size(),
      result.fine_tetra.size(),
      result.fine_interfaces.size(),
      result.broken_face_count,
      result.fragment_count,
      result.crack_area,
      result.fracture_momentum_residual,
      result.fracture_mass_residual,
      result.fragment_surface_bad_edge_count,
      result.crack_opening_distance,
      result.loaded_face_count,
      result.cut_polygon.size(),
      result.render_triangles.size(),
      panel_triangle_count[0],
      panel_triangle_count[1],
      panel_triangle_count[2]);
    std::fprintf(
      stderr,
      "agent_adaptive_prism_tetra_blast_plane_probe multidirection=(external_pressure_pa=%.9f,external_force=%.9f,secondary_area=%.9f,secondary_normal=%.9f,secondary_shear=%.9f,secondary_index=%.9f,primary_faces=%u,secondary_faces=%u,directions=2,fragments=%u,expected_external_impulse=%.9f)\n",
      config.external_pressure,
      Length(result.external_pressure_force),
      result.secondary_cut_area,
      result.secondary_normal_traction,
      result.secondary_shear_traction,
      result.secondary_fracture_index,
      result.primary_broken_face_count,
      result.secondary_broken_face_count,
      result.fragment_count,
      Length(result.expected_external_impulse));
    std::fprintf(
      stderr,
      "agent_adaptive_prism_tetra_blast_plane_probe profile=(evaluate_ms=%.6f,admission_ms=%.6f,mesh_ms=%.6f,adjacency_ms=%.6f,partition_ms=%.6f,regularization_ms=%.6f,components_ms=%.6f,impulse_ms=%.6f,closure_ms=%.6f,render_ms=%.6f,write_ms=%.6f,total_ms=%.6f)\n",
      result.timing.evaluate_ms,
      result.timing.admission_ms,
      result.timing.mesh_ms,
      result.timing.adjacency_ms,
      result.timing.partition_ms,
      result.timing.regularization_ms,
      result.timing.components_ms,
      result.timing.impulse_ms,
      result.timing.closure_ms,
      result.timing.render_ms,
      result.timing.write_ms,
      result.timing.total_ms);
    *algorithm_to_agent_signal = {};
    debug_state->signals.push_back({
      .name = "agent_adaptive_prism_tetra_blast_plane_probe.jobs",
      .payload = "force=" + std::to_string(Length(result.external_force)) +
        ", force_residual=" + std::to_string(Length(result.nodal_resultant - result.external_force)) +
        ", moment_residual=" + std::to_string(Length(result.nodal_moment - result.external_moment)) +
        ", cut_area=" + std::to_string(result.cut_area) +
        ", normal_traction=" + std::to_string(result.normal_traction) +
        ", shear_traction=" + std::to_string(result.shear_traction) +
        ", fracture_index=" + std::to_string(result.fracture_index) +
        ", refine_score=" + std::to_string(result.refine_decision.refine_score) +
        ", reason_mask=" + std::to_string(result.refine_decision.reason_mask) +
        ", precision_groups=" + std::to_string(result.precision_group_count) +
        ", accepted_lg_depth=" + std::to_string(result.accepted_lg_depth) +
        ", precision_leaves=" + std::to_string(result.precision_leaf_count) +
        ", precision_executed=" + std::to_string(result.precision_group_executed) +
        ", fracture_committed=" + std::to_string(result.fracture_committed ? 1u : 0u) +
        ", fine_tetra=" + std::to_string(result.fine_tetra.size()) +
        ", broken_faces=" + std::to_string(result.broken_face_count) +
        ", primary_broken_faces=" + std::to_string(result.primary_broken_face_count) +
        ", secondary_broken_faces=" + std::to_string(result.secondary_broken_face_count) +
        ", secondary_fracture_index=" + std::to_string(result.secondary_fracture_index) +
        ", external_pressure_force=" + std::to_string(Length(result.external_pressure_force)) +
        ", fragments=" + std::to_string(result.fragment_count) +
        ", momentum_residual=" + std::to_string(result.fracture_momentum_residual) +
        ", mass_residual=" + std::to_string(result.fracture_mass_residual) +
        ", bad_surface_edges=" + std::to_string(result.fragment_surface_bad_edge_count) +
        ", cut_vertices=" + std::to_string(result.cut_polygon.size()) +
        ", render_triangles=" + std::to_string(result.render_triangles.size()),
    });
    return true;
  }
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
  out_bundle->vk_symbol = false;
  out_bundle->cuda_symbol = false;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->jobs_executor = new TetraBlastPlaneProbeJobsExecutor();
  out_bundle->destroy_jobs_executor = &DestroyJobsExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algorithm::AlgorithmReflector* out_reflector) {
  assert(request);
  assert(request->algorithm_name);
  assert(out_reflector);
  std::shared_ptr<algorithm::AlgorithmReflector> reflector{};
  algorithm::AlgorithmPackageLocation location{};
  const bool resolved = algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
    request->algorithm_name, &location, nullptr);
  assert(resolved);
  const bool loaded = algomanager::algocatalog::LoadAlgorithmPackageReflectorFromLocation(
    location, &reflector, nullptr);
  assert(loaded);
  assert(reflector);
  *out_reflector = *reflector;
  return true;
}
