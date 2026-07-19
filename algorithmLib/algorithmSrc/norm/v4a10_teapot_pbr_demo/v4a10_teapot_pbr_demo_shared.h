#pragma once

#include "../algorithm_plugin_api.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <memory>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace v4a10_teapot_pbr_demo {

namespace detail {

constexpr uint32_t kFrameOrbitTicks = 120u;
constexpr size_t kTriangleFloats = 24u;
constexpr size_t kBvhFloats = 8u;
constexpr size_t kMaterialFloats = 12u;
constexpr size_t kSceneInfoFloats = 4u;
constexpr size_t kMaxRenderTriangles = 10000u;
constexpr float kPi = 3.14159265359f;
constexpr float kTau = 6.28318530718f;

struct Vec2 {
  float x{0.0f};
  float y{0.0f};
};

struct Vec3 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct FaceRef {
  int v{0};
  int vt{0};
  int vn{0};
};

struct ParsedTriangle {
  Vec3 p[3]{};
  Vec3 n[3]{};
  Vec2 uv[3]{};
};

struct ParsedMaterial {
  Vec3 ka{0.18f, 0.14f, 0.04f};
  Vec3 kd{0.97f, 0.75f, 0.22f};
  Vec3 ks{0.90f, 0.77f, 0.42f};
  float roughness{0.11f};
  float metallic{1.0f};
  float opacity{1.0f};
};

struct ParsedMeshCache {
  bool valid{false};
  uint64_t source_hash{0u};
  std::vector<ParsedTriangle> triangles{};
  std::vector<std::array<float, kBvhFloats>> bvh_nodes{};
  std::array<float, kSceneInfoFloats> scene_info{};
  std::array<float, kMaterialFloats> material_info{};
  uint32_t texture_seed{0u};
};

template <size_t N>
using FloatPack = std::array<float, N>;

inline void AppendTeapotTrace(const char* message) {
  (void)message;
}

inline float Clamp(float value, float min_value, float max_value) {
  return std::min(std::max(value, min_value), max_value);
}

inline Vec3 Add(Vec3 a, Vec3 b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 Sub(Vec3 a, Vec3 b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 Mul(Vec3 a, float s) {
  return Vec3{a.x * s, a.y * s, a.z * s};
}

inline float Dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(Vec3 a, Vec3 b) {
  return Vec3{
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x};
}

inline float Length(Vec3 value) {
  return std::sqrt(Dot(value, value));
}

inline Vec3 Normalize(Vec3 value) {
  const float len = Length(value);
  if (len <= 1.0e-8f) {
    return Vec3{0.0f, 1.0f, 0.0f};
  }
  return Mul(value, 1.0f / len);
}

inline float ParseFloatToken(const std::string& token) {
  char* end = nullptr;
  const float value = std::strtof(token.c_str(), &end);
  assert(end != token.c_str());
  return value;
}

inline int ParseIntToken(const std::string& token) {
  char* end = nullptr;
  const long value = std::strtol(token.c_str(), &end, 10);
  assert(end != token.c_str());
  return static_cast<int>(value);
}

inline uint64_t HashBytes(const std::byte* data, size_t size) {
  uint64_t hash = 1469598103934665603ull;
  const auto* raw = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= static_cast<uint64_t>(raw[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

inline const algorithm::AlgorithmContainer* FindConstContainer(
  const algorithm::AlgorithmContainerSet* container_set,
  const char* container_name) {
  return container_set ? algorithm::FindAlgorithmContainer(*container_set, container_name ? container_name : "") : nullptr;
}

inline algorithm::AlgorithmContainer* FindMutableContainer(
  algorithm::AlgorithmContainerSet* container_set,
  const char* container_name) {
  return container_set ? algorithm::FindAlgorithmContainer(container_set, container_name ? container_name : "") : nullptr;
}

inline std::string ReadTextContainer(const algorithm::AlgorithmContainer* container) {
  if (!container || container->bytes.empty()) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(container->bytes.data()), container->bytes.size());
}

inline uint32_t ReadUint32(const algorithm::AlgorithmContainer* container) {
  assert(container);
  assert(container->bytes.size() >= sizeof(uint32_t));
  uint32_t value = 0u;
  std::memcpy(&value, container->bytes.data(), sizeof(value));
  return value;
}

inline void WriteUint32(algorithm::AlgorithmContainer* container, uint32_t value) {
  assert(container);
  assert(container->bytes.size() >= sizeof(uint32_t));
  std::memcpy(container->bytes.data(), &value, sizeof(value));
}

template <size_t N>
inline void WritePack(
  algorithm::AlgorithmContainer* container,
  size_t index,
  const FloatPack<N>& pack) {
  assert(container);
  assert(container->element_stride == N * sizeof(float));
  const size_t offset = index * static_cast<size_t>(container->element_stride);
  assert(container->bytes.size() >= offset + sizeof(float) * N);
  std::memcpy(container->bytes.data() + offset, pack.data(), sizeof(float) * N);
}

template <size_t N>
inline void WritePackArray(
  algorithm::AlgorithmContainer* container,
  const std::vector<FloatPack<N>>& packs) {
  assert(container);
  assert(container->element_stride == N * sizeof(float));
  assert(container->bytes.size() >= packs.size() * static_cast<size_t>(container->element_stride));
  for (size_t i = 0; i < packs.size(); ++i) {
    WritePack(container, i, packs[i]);
  }
}

template <typename T>
inline std::vector<T> ReadPackArray(const algorithm::AlgorithmContainer* container) {
  static_assert(std::is_trivially_copyable_v<T>, "Packed mesh field data must be trivially copyable.");
  assert(container);
  assert(container->element_stride == sizeof(T));
  assert(container->bytes.size() % sizeof(T) == 0u);
  std::vector<T> values(container->bytes.size() / sizeof(T));
  if (!values.empty()) {
    std::memcpy(values.data(), container->bytes.data(), container->bytes.size());
  }
  return values;
}

inline bool ParseFaceRef(const std::string& token, FaceRef* out_ref) {
  if (!out_ref) {
    return false;
  }
  const size_t first = token.find('/');
  const size_t second = first == std::string::npos ? std::string::npos : token.find('/', first + 1u);
  const std::string v = token.substr(0, first);
  const std::string vt = first == std::string::npos
    ? std::string{}
    : token.substr(first + 1u, second == std::string::npos ? std::string::npos : second - first - 1u);
  const std::string vn = second == std::string::npos ? std::string{} : token.substr(second + 1u);
  out_ref->v = ParseIntToken(v);
  out_ref->vt = vt.empty() ? 0 : ParseIntToken(vt);
  out_ref->vn = vn.empty() ? 0 : ParseIntToken(vn);
  return true;
}

inline size_t ResolveIndex(int index, size_t count) {
  if (index > 0) {
    return static_cast<size_t>(index - 1);
  }
  return static_cast<size_t>(static_cast<int>(count) + index);
}

inline Vec3 ResolvePosition(const std::vector<Vec3>& positions, const FaceRef& ref) {
  return positions[ResolveIndex(ref.v, positions.size())];
}

inline Vec2 ResolveUv(const std::vector<Vec2>& uvs, const FaceRef& ref) {
  if (ref.vt == 0 || uvs.empty()) {
    return Vec2{0.0f, 0.0f};
  }
  return uvs[ResolveIndex(ref.vt, uvs.size())];
}

inline Vec3 ResolveNormal(const std::vector<Vec3>& normals, const FaceRef& ref, const Vec3& fallback) {
  if (ref.vn == 0 || normals.empty()) {
    return fallback;
  }
  return Normalize(normals[ResolveIndex(ref.vn, normals.size())]);
}

inline bool ParseObj(
  const std::string& text,
  std::vector<ParsedTriangle>* out_triangles,
  Vec3* out_min,
  Vec3* out_max) {
  if (!out_triangles || !out_min || !out_max) {
    return false;
  }

  std::vector<Vec3> positions;
  std::vector<Vec2> uvs;
  std::vector<Vec3> normals;
  positions.reserve(4096u);
  uvs.reserve(8192u);
  normals.reserve(4096u);
  out_triangles->clear();

  std::istringstream stream(text);
  std::string line;
  std::string current_material;
  while (std::getline(stream, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    std::istringstream line_stream(line);
    std::string tag;
    line_stream >> tag;
    if (tag == "v") {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      line_stream >> x >> y >> z;
      positions.push_back(Vec3{x, y, z});
      continue;
    }
    if (tag == "vt") {
      float u = 0.0f;
      float v = 0.0f;
      line_stream >> u >> v;
      uvs.push_back(Vec2{u, v});
      continue;
    }
    if (tag == "vn") {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      line_stream >> x >> y >> z;
      normals.push_back(Vec3{x, y, z});
      continue;
    }
    if (tag == "usemtl") {
      line_stream >> current_material;
      continue;
    }
    if (tag != "f") {
      continue;
    }

    std::vector<FaceRef> refs;
    std::string token;
    while (line_stream >> token) {
      FaceRef ref{};
      ParseFaceRef(token, &ref);
      refs.push_back(ref);
    }
    if (refs.size() < 3u) {
      continue;
    }

    for (size_t i = 1u; i + 1u < refs.size(); ++i) {
      ParsedTriangle triangle{};
      const FaceRef refs_local[3] = {refs[0], refs[i], refs[i + 1u]};
      triangle.p[0] = ResolvePosition(positions, refs_local[0]);
      triangle.p[1] = ResolvePosition(positions, refs_local[1]);
      triangle.p[2] = ResolvePosition(positions, refs_local[2]);
      const Vec3 face_normal = Normalize(Cross(Sub(triangle.p[1], triangle.p[0]), Sub(triangle.p[2], triangle.p[0])));
      triangle.n[0] = ResolveNormal(normals, refs_local[0], face_normal);
      triangle.n[1] = ResolveNormal(normals, refs_local[1], face_normal);
      triangle.n[2] = ResolveNormal(normals, refs_local[2], face_normal);
      triangle.uv[0] = ResolveUv(uvs, refs_local[0]);
      triangle.uv[1] = ResolveUv(uvs, refs_local[1]);
      triangle.uv[2] = ResolveUv(uvs, refs_local[2]);
      out_triangles->push_back(triangle);
    }
  }

  if (out_triangles->empty()) {
    return false;
  }

  Vec3 min_value{
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity()};
  Vec3 max_value{
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity()};

  for (const ParsedTriangle& triangle : *out_triangles) {
    for (const Vec3& p : triangle.p) {
      min_value.x = std::min(min_value.x, p.x);
      min_value.y = std::min(min_value.y, p.y);
      min_value.z = std::min(min_value.z, p.z);
      max_value.x = std::max(max_value.x, p.x);
      max_value.y = std::max(max_value.y, p.y);
      max_value.z = std::max(max_value.z, p.z);
    }
  }

  *out_min = min_value;
  *out_max = max_value;
  (void)current_material;
  return true;
}

inline bool ParseMtl(
  const std::string& text,
  ParsedMaterial* out_material) {
  if (!out_material) {
    return false;
  }

  ParsedMaterial material{};
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    std::istringstream line_stream(line);
    std::string tag;
    line_stream >> tag;
    if (tag == "Ka") {
      line_stream >> material.ka.x >> material.ka.y >> material.ka.z;
      continue;
    }
    if (tag == "Kd") {
      line_stream >> material.kd.x >> material.kd.y >> material.kd.z;
      continue;
    }
    if (tag == "Ks") {
      line_stream >> material.ks.x >> material.ks.y >> material.ks.z;
      continue;
    }
    if (tag == "Ns") {
      float ns = 0.0f;
      line_stream >> ns;
      const float roughness = std::sqrt(std::max(2.0f / (ns + 2.0f), 0.02f));
      material.roughness = Clamp(roughness, 0.04f, 1.0f);
      continue;
    }
    if (tag == "Ni") {
      float ni = 1.0f;
      line_stream >> ni;
      (void)ni;
      continue;
    }
    if (tag == "d") {
      line_stream >> material.opacity;
      continue;
    }
  }

  *out_material = material;
  return true;
}

inline std::array<float, kMaterialFloats> PackMaterial(
  const ParsedMaterial& material) {
  return std::array<float, kMaterialFloats>{
    material.ka.x, material.ka.y, material.ka.z, material.roughness,
    material.kd.x, material.kd.y, material.kd.z, material.metallic,
    material.ks.x, material.ks.y, material.ks.z, material.opacity,
  };
}

inline std::array<float, kSceneInfoFloats> PackSceneInfo(
  Vec3 min_value,
  Vec3 max_value) {
  const Vec3 center = Mul(Add(min_value, max_value), 0.5f);
  float radius = 0.0f;
  radius = std::max(radius, Length(Sub(Vec3{min_value.x, min_value.y, min_value.z}, center)));
  radius = std::max(radius, Length(Sub(Vec3{max_value.x, min_value.y, min_value.z}, center)));
  radius = std::max(radius, Length(Sub(Vec3{min_value.x, max_value.y, min_value.z}, center)));
  radius = std::max(radius, Length(Sub(Vec3{min_value.x, min_value.y, max_value.z}, center)));
  radius = std::max(radius, Length(Sub(Vec3{max_value.x, max_value.y, min_value.z}, center)));
  radius = std::max(radius, Length(Sub(Vec3{max_value.x, min_value.y, max_value.z}, center)));
  radius = std::max(radius, Length(Sub(Vec3{min_value.x, max_value.y, max_value.z}, center)));
  radius = std::max(radius, Length(Sub(Vec3{max_value.x, max_value.y, max_value.z}, center)));
  return std::array<float, kSceneInfoFloats>{
    center.x,
    center.y,
    center.z,
    radius * 1.15f,
  };
}

inline std::array<float, kTriangleFloats> PackTriangle(
  const ParsedTriangle& triangle) {
  return std::array<float, kTriangleFloats>{
    triangle.p[0].x, triangle.p[0].y, triangle.p[0].z, triangle.uv[0].x,
    triangle.p[1].x, triangle.p[1].y, triangle.p[1].z, triangle.uv[1].x,
    triangle.p[2].x, triangle.p[2].y, triangle.p[2].z, triangle.uv[2].x,
    triangle.n[0].x, triangle.n[0].y, triangle.n[0].z, triangle.uv[0].y,
    triangle.n[1].x, triangle.n[1].y, triangle.n[1].z, triangle.uv[1].y,
    triangle.n[2].x, triangle.n[2].y, triangle.n[2].z, triangle.uv[2].y,
  };
}

inline void TriangleBounds(
  const ParsedTriangle& triangle,
  Vec3* out_min,
  Vec3* out_max,
  Vec3* out_centroid) {
  Vec3 min_value{
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity()};
  Vec3 max_value{
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity()};
  Vec3 centroid{0.0f, 0.0f, 0.0f};
  for (const Vec3& p : triangle.p) {
    min_value.x = std::min(min_value.x, p.x);
    min_value.y = std::min(min_value.y, p.y);
    min_value.z = std::min(min_value.z, p.z);
    max_value.x = std::max(max_value.x, p.x);
    max_value.y = std::max(max_value.y, p.y);
    max_value.z = std::max(max_value.z, p.z);
    centroid = Add(centroid, p);
  }
  centroid = Mul(centroid, 1.0f / 3.0f);
  *out_min = min_value;
  *out_max = max_value;
  *out_centroid = centroid;
}

inline int BuildBvhRecursive(
  const std::vector<ParsedTriangle>& triangles,
  const std::vector<int>& triangle_indices,
  std::vector<std::array<float, kBvhFloats>>* out_nodes) {
  assert(out_nodes);
  const int node_index = static_cast<int>(out_nodes->size());
  out_nodes->push_back(std::array<float, kBvhFloats>{});

  Vec3 min_value{
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity()};
  Vec3 max_value{
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity()};
  Vec3 centroid_min{
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity()};
  Vec3 centroid_max{
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity()};

  std::vector<std::pair<int, Vec3>> items;
  items.reserve(triangle_indices.size());
  for (int triangle_index : triangle_indices) {
    Vec3 tri_min{};
    Vec3 tri_max{};
    Vec3 centroid{};
    TriangleBounds(triangles[static_cast<size_t>(triangle_index)], &tri_min, &tri_max, &centroid);
    min_value.x = std::min(min_value.x, tri_min.x);
    min_value.y = std::min(min_value.y, tri_min.y);
    min_value.z = std::min(min_value.z, tri_min.z);
    max_value.x = std::max(max_value.x, tri_max.x);
    max_value.y = std::max(max_value.y, tri_max.y);
    max_value.z = std::max(max_value.z, tri_max.z);
    centroid_min.x = std::min(centroid_min.x, centroid.x);
    centroid_min.y = std::min(centroid_min.y, centroid.y);
    centroid_min.z = std::min(centroid_min.z, centroid.z);
    centroid_max.x = std::max(centroid_max.x, centroid.x);
    centroid_max.y = std::max(centroid_max.y, centroid.y);
    centroid_max.z = std::max(centroid_max.z, centroid.z);
    items.push_back(std::pair<int, Vec3>{triangle_index, centroid});
  }

  std::array<float, kBvhFloats>& node = (*out_nodes)[static_cast<size_t>(node_index)];
  if (triangle_indices.size() == 1u) {
    node = std::array<float, kBvhFloats>{
      min_value.x, min_value.y, min_value.z, static_cast<float>(triangle_indices.front()),
      max_value.x, max_value.y, max_value.z, -1.0f,
    };
    return node_index;
  }

  const Vec3 centroid_extent = Sub(centroid_max, centroid_min);
  const int split_axis =
    centroid_extent.x >= centroid_extent.y && centroid_extent.x >= centroid_extent.z ? 0 :
    centroid_extent.y >= centroid_extent.z ? 1 : 2;

  std::sort(items.begin(), items.end(), [&](const std::pair<int, Vec3>& lhs, const std::pair<int, Vec3>& rhs) {
    const Vec3& a = lhs.second;
    const Vec3& b = rhs.second;
    return split_axis == 0 ? a.x < b.x : split_axis == 1 ? a.y < b.y : a.z < b.z;
  });

  const size_t mid = items.size() / 2u;
  std::vector<int> left_indices;
  std::vector<int> right_indices;
  left_indices.reserve(mid);
  right_indices.reserve(items.size() - mid);
  for (size_t i = 0u; i < mid; ++i) {
    left_indices.push_back(items[i].first);
  }
  for (size_t i = mid; i < items.size(); ++i) {
    right_indices.push_back(items[i].first);
  }
  const int left_child = BuildBvhRecursive(triangles, left_indices, out_nodes);
  const int right_child = BuildBvhRecursive(triangles, right_indices, out_nodes);
  node = std::array<float, kBvhFloats>{
    min_value.x, min_value.y, min_value.z, static_cast<float>(left_child),
    max_value.x, max_value.y, max_value.z, static_cast<float>(right_child),
  };
  return node_index;
}

inline void BuildBvh(
  const std::vector<ParsedTriangle>& triangles,
  std::vector<std::array<float, kBvhFloats>>* out_nodes) {
  assert(out_nodes);
  out_nodes->clear();
  if (triangles.empty()) {
    return;
  }
  std::vector<int> indices;
  indices.reserve(triangles.size());
  for (size_t i = 0; i < triangles.size(); ++i) {
    indices.push_back(static_cast<int>(i));
  }
  out_nodes->reserve(triangles.size() * 2u);
  (void)BuildBvhRecursive(triangles, indices, out_nodes);
}

inline bool BuildMeshCache(
  const algorithm::AlgorithmContainerSet* container_set,
  ParsedMeshCache* out_cache) {
  if (!container_set || !out_cache) {
    return false;
  }
  AppendTeapotTrace("BuildMeshCache.begin");
  const std::string container_set_trace =
    "BuildMeshCache.container_set algorithm=" + container_set->algorithm_name +
    " arrays=" + std::to_string(container_set->arrays.size()) +
    " regs=" + std::to_string(container_set->temporary_registers.size()) +
    " caches=" + std::to_string(container_set->temporary_caches.size()) +
    " hidden=" + std::to_string(container_set->hidden_containers.size()) +
    " standard_enabled=" + std::string(container_set->standard_layout.enabled() ? "true" : "false");
  AppendTeapotTrace(container_set_trace.c_str());

  const algorithm::AlgorithmContainer* positions_container = FindConstContainer(container_set, "vertex_mesh_0");
  AppendTeapotTrace("BuildMeshCache.lookup.vertex_mesh_0");
  const algorithm::AlgorithmContainer* normals_container = FindConstContainer(container_set, "normal_mesh_0");
  AppendTeapotTrace("BuildMeshCache.lookup.normal_mesh_0");
  const algorithm::AlgorithmContainer* uvs_container = FindConstContainer(container_set, "uv_mesh_0");
  AppendTeapotTrace("BuildMeshCache.lookup.uv_mesh_0");
  const algorithm::AlgorithmContainer* triangles_container = FindConstContainer(container_set, "triangle_mesh_0");
  AppendTeapotTrace("BuildMeshCache.lookup.triangle_mesh_0");
  const algorithm::AlgorithmContainer* mtl_container = FindConstContainer(container_set, "mtl_bytes_0");
  AppendTeapotTrace("BuildMeshCache.lookup.mtl_bytes_0");
  const algorithm::AlgorithmContainer* texture_container = FindConstContainer(container_set, "gold_tile_bytes_0");
  AppendTeapotTrace("BuildMeshCache.lookup.gold_tile_bytes_0");
  if (!positions_container || !normals_container || !uvs_container || !triangles_container || !mtl_container || !texture_container) {
    return false;
  }

  const std::string container_trace =
    "BuildMeshCache.containers sizes positions=" + std::to_string(positions_container->bytes.size()) +
    " normals=" + std::to_string(normals_container->bytes.size()) +
    " uvs=" + std::to_string(uvs_container->bytes.size()) +
    " triangles=" + std::to_string(triangles_container->bytes.size()) +
    " mtl=" + std::to_string(mtl_container->bytes.size()) +
    " texture=" + std::to_string(texture_container->bytes.size()) +
    " strides p=" + std::to_string(positions_container->element_stride) +
    " n=" + std::to_string(normals_container->element_stride) +
    " u=" + std::to_string(uvs_container->element_stride) +
    " t=" + std::to_string(triangles_container->element_stride);
  AppendTeapotTrace(container_trace.c_str());

  const uint64_t source_hash =
    HashBytes(positions_container->bytes.data(), positions_container->bytes.size()) ^
    (HashBytes(normals_container->bytes.data(), normals_container->bytes.size()) << 1u) ^
    (HashBytes(uvs_container->bytes.data(), uvs_container->bytes.size()) << 2u) ^
    (HashBytes(triangles_container->bytes.data(), triangles_container->bytes.size()) << 3u) ^
    (HashBytes(mtl_container->bytes.data(), mtl_container->bytes.size()) << 4u) ^
    (HashBytes(texture_container->bytes.data(), texture_container->bytes.size()) << 5u);

  thread_local ParsedMeshCache cache;
  if (!cache.valid || cache.source_hash != source_hash) {
    AppendTeapotTrace("BuildMeshCache.parse.begin");
    const std::string mtl_text = ReadTextContainer(mtl_container);
    const std::vector<Vec3> positions = ReadPackArray<Vec3>(positions_container);
    const std::vector<Vec3> normals = ReadPackArray<Vec3>(normals_container);
    const std::vector<Vec2> uvs = ReadPackArray<Vec2>(uvs_container);
    std::vector<std::array<uint32_t, 3>> triangle_indices = ReadPackArray<std::array<uint32_t, 3>>(triangles_container);
    while (!triangle_indices.empty() && triangle_indices.back() == std::array<uint32_t, 3>{0u, 0u, 0u}) {
      triangle_indices.pop_back();
    }

    ParsedMaterial material{};
    std::vector<ParsedTriangle> triangles{};
    Vec3 min_value{};
    Vec3 max_value{};
    min_value = Vec3{
      std::numeric_limits<float>::infinity(),
      std::numeric_limits<float>::infinity(),
      std::numeric_limits<float>::infinity()};
    max_value = Vec3{
      -std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity()};
    triangles.reserve(triangle_indices.size());
    for (const std::array<uint32_t, 3>& triangle_index : triangle_indices) {
      ParsedTriangle triangle{};
      for (size_t vertex = 0u; vertex < 3u; ++vertex) {
        const uint32_t index = triangle_index[vertex];
        triangle.p[vertex] = positions[index];
        triangle.n[vertex] = normals[index];
        triangle.uv[vertex] = uvs[index];
        min_value.x = std::min(min_value.x, triangle.p[vertex].x);
        min_value.y = std::min(min_value.y, triangle.p[vertex].y);
        min_value.z = std::min(min_value.z, triangle.p[vertex].z);
        max_value.x = std::max(max_value.x, triangle.p[vertex].x);
        max_value.y = std::max(max_value.y, triangle.p[vertex].y);
        max_value.z = std::max(max_value.z, triangle.p[vertex].z);
      }
      triangles.push_back(triangle);
    }
    if (triangles.empty()) {
      return false;
    }
    if (!ParseMtl(mtl_text, &material)) {
      return false;
    }
    AppendTeapotTrace("BuildMeshCache.parse.mtl.end");

    cache = {};
    cache.valid = true;
    cache.source_hash = source_hash;
    cache.triangles = std::move(triangles);
    AppendTeapotTrace("BuildMeshCache.bvh.begin");
    BuildBvh(cache.triangles, &cache.bvh_nodes);
    AppendTeapotTrace("BuildMeshCache.bvh.end");
    cache.scene_info = PackSceneInfo(min_value, max_value);
    cache.material_info = PackMaterial(material);
    cache.texture_seed = static_cast<uint32_t>(HashBytes(texture_container->bytes.data(), texture_container->bytes.size()));
  }

  *out_cache = cache;
  AppendTeapotTrace("BuildMeshCache.end");
  return true;
}

inline void WriteMeshCacheToContainers(
  ParsedMeshCache* cache,
  algorithm::AlgorithmContainerSet* container_set) {
  assert(cache);
  assert(container_set);
  AppendTeapotTrace("WriteMeshCacheToContainers.begin");

  algorithm::AlgorithmContainer* frame_tick = FindMutableContainer(container_set, "frame_tick");
  algorithm::AlgorithmContainer* triangle_count = FindMutableContainer(container_set, "mesh_triangle_count");
  algorithm::AlgorithmContainer* bvh_count = FindMutableContainer(container_set, "mesh_bvh_node_count");
  algorithm::AlgorithmContainer* texture_seed = FindMutableContainer(container_set, "texture_seed");
  algorithm::AlgorithmContainer* instance_count = FindMutableContainer(container_set, "instance_count");
  algorithm::AlgorithmContainer* scene_info = FindMutableContainer(container_set, "scene_info");
  algorithm::AlgorithmContainer* material_buffer = FindMutableContainer(container_set, "material_buffer");
  algorithm::AlgorithmContainer* triangle_buffer = FindMutableContainer(container_set, "triangle_buffer");
  algorithm::AlgorithmContainer* bvh_buffer = FindMutableContainer(container_set, "bvh_buffer");
  assert(frame_tick && triangle_count && bvh_count && texture_seed && instance_count && scene_info && material_buffer && triangle_buffer && bvh_buffer);

  WriteUint32(triangle_count, static_cast<uint32_t>(cache->triangles.size()));
  WriteUint32(bvh_count, static_cast<uint32_t>(cache->bvh_nodes.size()));
  WriteUint32(texture_seed, cache->texture_seed);
  WriteUint32(instance_count, 1u);
  WritePack(scene_info, 0u, cache->scene_info);
  WritePack(material_buffer, 0u, cache->material_info);
  const std::string counts_trace =
    "WriteMeshCacheToContainers.counts triangles=" + std::to_string(cache->triangles.size()) +
    " bvh_nodes=" + std::to_string(cache->bvh_nodes.size()) +
    " texture_seed=" + std::to_string(cache->texture_seed) +
    " instance_count=1";
  AppendTeapotTrace(counts_trace.c_str());

  std::vector<FloatPack<kTriangleFloats>> packed_triangles;
  packed_triangles.reserve(cache->triangles.size());
  for (const ParsedTriangle& triangle : cache->triangles) {
    packed_triangles.push_back(PackTriangle(triangle));
  }
  WritePackArray(triangle_buffer, packed_triangles);
  AppendTeapotTrace("WriteMeshCacheToContainers.triangles.end");
  WritePackArray(bvh_buffer, cache->bvh_nodes);
  AppendTeapotTrace("WriteMeshCacheToContainers.bvh.end");

  uint32_t tick = ReadUint32(frame_tick);
  WriteUint32(frame_tick, tick);
  AppendTeapotTrace("WriteMeshCacheToContainers.end");
}

class TeapotJobsExecutor final : public agent::IAlgorithmJobsExecutor {
 public:
  bool ExecuteJobsAlgorithm(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    assert(algorithm_container_set);
    std::cerr
      << "teapot.jobs.begin algorithm=" << algorithm_profile.algorithm_name
      << " ptr=" << algorithm_container_set
      << std::endl;

    ParsedMeshCache cache{};
    std::cerr
      << "teapot.jobs.build_cache.begin algorithm=" << algorithm_profile.algorithm_name
      << std::endl;
    if (!BuildMeshCache(algorithm_container_set, &cache)) {
      std::cerr
        << "teapot.jobs.build_cache.failed algorithm=" << algorithm_profile.algorithm_name
        << std::endl;
      return false;
    }
    std::cerr
      << "teapot.jobs.build_cache.end algorithm=" << algorithm_profile.algorithm_name
      << " triangles=" << cache.triangles.size()
      << " bvh_nodes=" << cache.bvh_nodes.size()
      << std::endl;

    algorithm::AlgorithmContainer* frame_tick = FindMutableContainer(algorithm_container_set, "frame_tick");
    assert(frame_tick);
    WriteUint32(frame_tick, ReadUint32(frame_tick) + 1u);
    std::cerr
      << "teapot.jobs.write_back.begin algorithm=" << algorithm_profile.algorithm_name
      << std::endl;
    WriteMeshCacheToContainers(&cache, algorithm_container_set);
    std::cerr
      << "teapot.jobs.write_back.end algorithm=" << algorithm_profile.algorithm_name
      << std::endl;

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algomanager::algoscheduler::AdvancedAlgorithmDebugSignal{
        .name = "v4a10_teapot_pbr_demo.jobs",
        .payload = "triangles=" + std::to_string(cache.triangles.size()) +
          ", nodes=" + std::to_string(cache.bvh_nodes.size()) +
          ", texture_seed=" + std::to_string(cache.texture_seed),
      });
    }
    std::cerr
      << "teapot.jobs.end algorithm=" << algorithm_profile.algorithm_name
      << std::endl;
    return true;
  }
};

inline void DestroyJobsExecutor(agent::IAlgorithmJobsExecutor* executor) {
  delete executor;
}

inline bool CreateBundle(
  const algomanager::support::AlgorithmPluginRequest* request,
  algomanager::support::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->jobs_symbol = true;
  const std::string algorithm_name = request->algorithm_name ? request->algorithm_name : "";
  const bool is_stage1 = algorithm_name.find("_stage1") != std::string::npos;
  out_bundle->vk_symbol = is_stage1;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->jobs_executor = new TeapotJobsExecutor();
  out_bundle->destroy_jobs_executor = &DestroyJobsExecutor;
  return true;
}

inline bool CreateRuntimeReflector(
  const algomanager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algomanager::support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}

}  // namespace detail
}  // namespace v4a10_teapot_pbr_demo
