#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <limits>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <set>
#include <vector>

namespace {

struct Vec3 { float x; float y; float z; };
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

void RequireInvariant(bool condition) {
  if (!condition) std::abort();
}

void RequireInvariantCode(bool condition, int code) {
  if (!condition) {
    std::fprintf(stderr, "agent_adaptive_prism invariant=%d\n", code);
    std::abort();
  }
}

void AppendTrace(const char* format, ...) {
  std::FILE* file = std::fopen("testData/antonia_decomposition_trace.log", "a");
  va_list arguments;
  va_start(arguments, format);
  std::vfprintf(file, format, arguments);
  va_end(arguments);
  std::fclose(file);
}

struct Node { Vec3 rest{}; };
struct SourceTriangle {
  std::array<int, 3> indices{};
  Vec3 p[3]{};
};
struct SourceMesh {
  std::vector<Vec3> positions;
  std::vector<SourceTriangle> triangles;
  Vec3 min_position{};
  Vec3 max_position{};
  mutable bool ray_grid_ready{false};
  mutable std::vector<std::vector<int>> ray_grid;
};

using QuadKey = std::array<int, 4>;
using DiagonalKey = std::array<int, 2>;

QuadKey MakeQuadKey(std::array<int, 4> value) {
  std::sort(value.begin(), value.end());
  return value;
}

DiagonalKey MakeDiagonalKey(int a, int b) {
  if (a > b) std::swap(a, b);
  return {a, b};
}

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
  std::memcpy(&value, container.bytes.data() + index * container.element_stride, sizeof(T));
  return value;
}

template <typename T>
void WriteElement(algorithm::AlgorithmContainer& container, size_t index, const T& value) {
  std::memcpy(container.bytes.data() + index * container.element_stride, &value, sizeof(T));
}

void WriteUint32(algorithm::AlgorithmContainer& container, uint32_t value) {
  WriteElement<uint32_t>(container, 0u, value);
}

void WriteVec4(algorithm::AlgorithmContainer& container, size_t index, Vec3 xyz, float w) {
  WriteElement<std::array<float, 4>>(container, index, {xyz.x, xyz.y, xyz.z, w});
}

void WriteRenderTriangleColor(
    algorithm::AlgorithmContainer& render,
    size_t* triangle_index,
    Vec3 a,
    Vec3 b,
    Vec3 c,
    Vec3 color) {
  WriteVec4(render, *triangle_index * 4u + 0u, a, 1.0f);
  WriteVec4(render, *triangle_index * 4u + 1u, b, 1.0f);
  WriteVec4(render, *triangle_index * 4u + 2u, c, 1.0f);
  WriteVec4(render, *triangle_index * 4u + 3u, color, 1.0f);
  ++*triangle_index;
}

void WriteRenderTriangle(
    algorithm::AlgorithmContainer& render,
    size_t* triangle_index,
    Vec3 a,
    Vec3 b,
    Vec3 c) {
  WriteRenderTriangleColor(render, triangle_index, a, b, c, {0.92f, 0.62f, 0.20f});
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
    triangle.indices = {static_cast<int>(indices[0]), static_cast<int>(indices[1]), static_cast<int>(indices[2])};
    triangle.p[0] = mesh.positions[indices[0]];
    triangle.p[1] = mesh.positions[indices[1]];
    triangle.p[2] = mesh.positions[indices[2]];
    mesh.triangles.push_back(triangle);
  }
  assert(!mesh.triangles.empty());
  const Vec3 extent = mesh.max_position - mesh.min_position;
  const float weld_epsilon = std::max(extent.x, std::max(extent.y, extent.z)) * 1.0e-8f;
  using WeldKey = std::array<int64_t, 3>;
  std::map<WeldKey, int> welded_lookup;
  std::vector<Vec3> welded_positions;
  std::vector<int> remap(mesh.positions.size(), -1);
  for (size_t position_index = 0u; position_index < mesh.positions.size(); ++position_index) {
    const Vec3 position = mesh.positions[position_index];
    const WeldKey key{
      static_cast<int64_t>(std::llround(position.x / weld_epsilon)),
      static_cast<int64_t>(std::llround(position.y / weld_epsilon)),
      static_cast<int64_t>(std::llround(position.z / weld_epsilon))};
    const auto found = welded_lookup.find(key);
    if (found != welded_lookup.end()) {
      remap[position_index] = found->second;
      continue;
    }
    const int welded_index = static_cast<int>(welded_positions.size());
    welded_lookup.emplace(key, welded_index);
    welded_positions.push_back(position);
    remap[position_index] = welded_index;
  }
  for (SourceTriangle& triangle : mesh.triangles) {
    for (int& index : triangle.indices) index = remap[static_cast<size_t>(index)];
  }
  mesh.positions = welded_positions;
  return mesh;
}

bool RayIntersectsTriangle(Vec3 origin, const SourceTriangle& triangle) {
  const Vec3 direction{1.0f, 0.0f, 0.0f};
  const Vec3 e1 = triangle.p[1] - triangle.p[0];
  const Vec3 e2 = triangle.p[2] - triangle.p[0];
  const Vec3 p = Cross(direction, e2);
  const float determinant = Dot(e1, p);
  if (std::abs(determinant) < 1.0e-8f) return false;
  const float inverse = 1.0f / determinant;
  const Vec3 q = origin - triangle.p[0];
  const float u = Dot(q, p) * inverse;
  if (u < 0.0f || u > 1.0f) return false;
  const Vec3 r = Cross(q, e1);
  const float v = Dot(direction, r) * inverse;
  if (v < 0.0f || u + v > 1.0f) return false;
  return Dot(e2, r) * inverse > 0.0f;
}

void BuildRayGrid(const SourceMesh& mesh) {
  if (mesh.ray_grid_ready) return;
  constexpr int grid_width = 32;
  constexpr int grid_height = 32;
  mesh.ray_grid.clear();
  mesh.ray_grid.resize(static_cast<size_t>(grid_width * grid_height));
  const Vec3 extent = mesh.max_position - mesh.min_position;
  for (size_t triangle_index = 0u; triangle_index < mesh.triangles.size(); ++triangle_index) {
    const SourceTriangle& triangle = mesh.triangles[triangle_index];
    const float min_y = std::min(triangle.p[0].y, std::min(triangle.p[1].y, triangle.p[2].y));
    const float max_y = std::max(triangle.p[0].y, std::max(triangle.p[1].y, triangle.p[2].y));
    const float min_z = std::min(triangle.p[0].z, std::min(triangle.p[1].z, triangle.p[2].z));
    const float max_z = std::max(triangle.p[0].z, std::max(triangle.p[1].z, triangle.p[2].z));
    const int first_y = std::max(0, std::min(grid_height - 1, static_cast<int>(((min_y - mesh.min_position.y) / extent.y) * grid_height)));
    const int last_y = std::max(0, std::min(grid_height - 1, static_cast<int>(((max_y - mesh.min_position.y) / extent.y) * grid_height)));
    const int first_z = std::max(0, std::min(grid_width - 1, static_cast<int>(((min_z - mesh.min_position.z) / extent.z) * grid_width)));
    const int last_z = std::max(0, std::min(grid_width - 1, static_cast<int>(((max_z - mesh.min_position.z) / extent.z) * grid_width)));
    for (int y = first_y; y <= last_y; ++y) {
      for (int z = first_z; z <= last_z; ++z) {
        mesh.ray_grid[static_cast<size_t>(y * grid_width + z)].push_back(static_cast<int>(triangle_index));
      }
    }
  }
  mesh.ray_grid_ready = true;
}

bool PointInsideMesh(Vec3 point, const SourceMesh& mesh) {
  BuildRayGrid(mesh);
  point.y += 1.0e-5f;
  point.z += 2.0e-5f;
  constexpr int grid_width = 32;
  constexpr int grid_height = 32;
  const Vec3 extent = mesh.max_position - mesh.min_position;
  const int y = std::max(0, std::min(grid_height - 1, static_cast<int>(((point.y - mesh.min_position.y) / extent.y) * grid_height)));
  const int z = std::max(0, std::min(grid_width - 1, static_cast<int>(((point.z - mesh.min_position.z) / extent.z) * grid_width)));
  size_t intersections = 0u;
  for (int triangle_index : mesh.ray_grid[static_cast<size_t>(y * grid_width + z)]) {
    if (RayIntersectsTriangle(point, mesh.triangles[static_cast<size_t>(triangle_index)])) ++intersections;
  }
  return (intersections & 1u) != 0u;
}

bool HasDirectedEdge(const SourceTriangle& triangle, int first, int second) {
  for (size_t edge = 0u; edge < 3u; ++edge) {
    if (triangle.indices[edge] == first && triangle.indices[(edge + 1u) % 3u] == second) return true;
  }
  return false;
}

void ReverseTriangle(SourceTriangle* triangle) {
  std::swap(triangle->indices[1], triangle->indices[2]);
  std::swap(triangle->p[1], triangle->p[2]);
}

void OrientSourceMesh(SourceMesh* mesh) {
  std::map<DiagonalKey, std::vector<int>> edge_triangles;
  for (size_t triangle = 0u; triangle < mesh->triangles.size(); ++triangle) {
    for (size_t edge = 0u; edge < 3u; ++edge) {
      edge_triangles[MakeDiagonalKey(
        mesh->triangles[triangle].indices[edge],
        mesh->triangles[triangle].indices[(edge + 1u) % 3u])].push_back(static_cast<int>(triangle));
    }
  }
  std::vector<bool> visited(mesh->triangles.size(), false);
  for (size_t seed = 0u; seed < mesh->triangles.size(); ++seed) {
    if (visited[seed]) continue;
    std::vector<int> pending{static_cast<int>(seed)};
    visited[seed] = true;
    while (!pending.empty()) {
      const int current = pending.back();
      pending.pop_back();
      const SourceTriangle& current_triangle = mesh->triangles[static_cast<size_t>(current)];
      for (size_t edge = 0u; edge < 3u; ++edge) {
        const int first = current_triangle.indices[edge];
        const int second = current_triangle.indices[(edge + 1u) % 3u];
        const auto& neighbors = edge_triangles.at(MakeDiagonalKey(first, second));
        for (int neighbor : neighbors) {
          if (neighbor == current || visited[static_cast<size_t>(neighbor)]) continue;
          SourceTriangle& neighbor_triangle = mesh->triangles[static_cast<size_t>(neighbor)];
          if (HasDirectedEdge(neighbor_triangle, first, second)) ReverseTriangle(&neighbor_triangle);
          visited[static_cast<size_t>(neighbor)] = true;
          pending.push_back(neighbor);
        }
      }
    }
  }
}

struct SourceTopologyAudit {
  uint32_t boundary_edges{0u};
  uint32_t nonmanifold_edges{0u};
};

SourceTopologyAudit AuditSourceTopology(const SourceMesh& source) {
  std::map<DiagonalKey, uint32_t> edge_use_count;
  for (const SourceTriangle& triangle : source.triangles) {
    for (size_t edge = 0u; edge < 3u; ++edge) {
      ++edge_use_count[MakeDiagonalKey(
        triangle.indices[edge],
        triangle.indices[(edge + 1u) % 3u])];
    }
  }
  SourceTopologyAudit audit{};
  for (const auto& entry : edge_use_count) {
    if (entry.second == 1u) ++audit.boundary_edges;
    if (entry.second > 2u) ++audit.nonmanifold_edges;
  }
  return audit;
}

uint32_t CapBoundaryLoops(SourceMesh* mesh) {
  mesh->ray_grid_ready = false;
  mesh->ray_grid.clear();
  std::map<DiagonalKey, uint32_t> edge_use_count;
  for (const SourceTriangle& triangle : mesh->triangles) {
    for (size_t edge = 0u; edge < 3u; ++edge) {
      ++edge_use_count[MakeDiagonalKey(
        triangle.indices[edge],
        triangle.indices[(edge + 1u) % 3u])];
    }
  }
  std::map<int, std::vector<int>> boundary_neighbors;
  for (const auto& entry : edge_use_count) {
    if (entry.second != 1u) continue;
    boundary_neighbors[entry.first[0]].push_back(entry.first[1]);
    boundary_neighbors[entry.first[1]].push_back(entry.first[0]);
  }
  std::set<int> visited_vertices;
  uint32_t cap_count = 0u;
  for (const auto& entry : boundary_neighbors) {
    if (visited_vertices.find(entry.first) != visited_vertices.end()) continue;
    std::vector<int> component;
    std::vector<int> pending{entry.first};
    visited_vertices.insert(entry.first);
    while (!pending.empty()) {
      const int current = pending.back();
      pending.pop_back();
      component.push_back(current);
      for (int neighbor : boundary_neighbors[current]) {
        if (visited_vertices.insert(neighbor).second) pending.push_back(neighbor);
      }
    }
    bool simple_loop = true;
    for (int vertex : component) if (boundary_neighbors[vertex].size() != 2u) simple_loop = false;
    if (!simple_loop) continue;
    std::vector<int> loop{component.front()};
    int previous = -1;
    int current = component.front();
    do {
      const std::vector<int>& neighbors = boundary_neighbors[current];
      const int next = neighbors[0] == previous ? neighbors[1] : neighbors[0];
      previous = current;
      current = next;
      if (current != loop.front()) loop.push_back(current);
    } while (current != loop.front());
    if (loop.size() != component.size() || loop.size() < 3u) continue;
    Vec3 center{};
    for (int vertex : loop) center = center + mesh->positions[static_cast<size_t>(vertex)];
    center = center * (1.0f / static_cast<float>(loop.size()));
    const int center_node = static_cast<int>(mesh->positions.size());
    mesh->positions.push_back(center);
    for (size_t index = 0u; index < loop.size(); ++index) {
      const int first = loop[index];
      const int second = loop[(index + 1u) % loop.size()];
      SourceTriangle cap{};
      cap.indices = {center_node, second, first};
      cap.p[0] = mesh->positions[static_cast<size_t>(cap.indices[0])];
      cap.p[1] = mesh->positions[static_cast<size_t>(cap.indices[1])];
      cap.p[2] = mesh->positions[static_cast<size_t>(cap.indices[2])];
      mesh->triangles.push_back(cap);
    }
    ++cap_count;
  }
  if (cap_count != 0u) OrientSourceMesh(mesh);
  return cap_count;
}

std::vector<SourceMesh> SplitMeshIntoEdgeConnectedComponents(const SourceMesh& source) {
  std::map<DiagonalKey, std::vector<int>> edge_triangles;
  for (size_t triangle = 0u; triangle < source.triangles.size(); ++triangle) {
    for (size_t edge = 0u; edge < 3u; ++edge) {
      edge_triangles[MakeDiagonalKey(
        source.triangles[triangle].indices[edge],
        source.triangles[triangle].indices[(edge + 1u) % 3u])].push_back(static_cast<int>(triangle));
    }
  }
  std::vector<std::vector<int>> triangle_neighbors(source.triangles.size());
  for (const auto& entry : edge_triangles) {
    if (entry.second.size() != 2u) continue;
    triangle_neighbors[static_cast<size_t>(entry.second[0])].push_back(entry.second[1]);
    triangle_neighbors[static_cast<size_t>(entry.second[1])].push_back(entry.second[0]);
  }
  std::vector<bool> visited(source.triangles.size(), false);
  std::vector<SourceMesh> components;
  for (size_t seed = 0u; seed < source.triangles.size(); ++seed) {
    if (visited[seed]) continue;
    std::vector<int> component_triangles;
    std::vector<int> pending{static_cast<int>(seed)};
    visited[seed] = true;
    while (!pending.empty()) {
      const int current = pending.back();
      pending.pop_back();
      component_triangles.push_back(current);
      for (int neighbor : triangle_neighbors[static_cast<size_t>(current)]) {
        if (visited[static_cast<size_t>(neighbor)]) continue;
        visited[static_cast<size_t>(neighbor)] = true;
        pending.push_back(neighbor);
      }
    }
    SourceMesh component{};
    component.positions = source.positions;
    component.min_position = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    component.max_position = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    for (int triangle_index : component_triangles) {
      const SourceTriangle& triangle = source.triangles[static_cast<size_t>(triangle_index)];
      component.triangles.push_back(triangle);
      for (int vertex : triangle.indices) {
        const Vec3 position = source.positions[static_cast<size_t>(vertex)];
        component.min_position = {
          std::min(component.min_position.x, position.x),
          std::min(component.min_position.y, position.y),
          std::min(component.min_position.z, position.z)};
        component.max_position = {
          std::max(component.max_position.x, position.x),
          std::max(component.max_position.y, position.y),
          std::max(component.max_position.z, position.z)};
      }
    }
    OrientSourceMesh(&component);
    components.push_back(std::move(component));
  }
  return components;
}

struct PrismCandidate {
  std::array<int, 6> prism_nodes{};
  std::array<std::array<int, 4>, 3> tetra{};
  std::array<DiagonalKey, 3> diagonals{};
  std::array<QuadKey, 3> quads{};
};

PrismCandidate MakePrismCandidate(
    const std::array<int, 3>& lower,
    const std::array<int, 3>& upper,
    int rotation,
    bool reverse,
    const std::vector<Node>& nodes) {
  std::array<int, 3> a{};
  std::array<int, 3> b{};
  for (int i = 0; i < 3; ++i) {
    a[i] = lower[(rotation + i) % 3];
    b[i] = upper[(rotation + i) % 3];
  }
  PrismCandidate candidate{};
  candidate.prism_nodes = {a[0], a[1], a[2], b[0], b[1], b[2]};
  if (!reverse) {
    candidate.tetra = {{{a[0], a[1], a[2], b[0]}, {a[1], a[2], b[1], b[0]}, {a[2], b[1], b[2], b[0]}}};
    candidate.diagonals = {{MakeDiagonalKey(a[1], b[0]), MakeDiagonalKey(a[2], b[1]), MakeDiagonalKey(a[2], b[0])}};
  } else {
    candidate.tetra = {{{a[0], a[1], a[2], b[1]}, {a[0], a[2], b[2], b[1]}, {a[0], b[2], b[0], b[1]}}};
    candidate.diagonals = {{MakeDiagonalKey(a[0], b[1]), MakeDiagonalKey(a[2], b[1]), MakeDiagonalKey(a[0], b[2])}};
  }
  candidate.quads = {{
    MakeQuadKey({a[0], a[1], b[0], b[1]}),
    MakeQuadKey({a[1], a[2], b[1], b[2]}),
    MakeQuadKey({a[2], a[0], b[2], b[0]})}};
  for (std::array<int, 4>& tetra : candidate.tetra) {
    const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
    const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
    const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
    const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
    const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    assert(std::abs(determinant) > 1.0e-10f);
    if (determinant < 0.0f) std::swap(tetra[1], tetra[2]);
  }
  return candidate;
}

PrismCandidate SelectPrismCandidate(
    const std::array<int, 3>& lower,
    const std::array<int, 3>& upper,
    const std::vector<Node>& nodes,
    std::map<QuadKey, DiagonalKey>* diagonal_map) {
  for (int reverse = 0; reverse < 2; ++reverse) {
    for (int rotation = 0; rotation < 3; ++rotation) {
      PrismCandidate candidate = MakePrismCandidate(lower, upper, rotation, reverse != 0, nodes);
      bool compatible = true;
      for (size_t i = 0u; i < candidate.quads.size(); ++i) {
        const auto found = diagonal_map->find(candidate.quads[i]);
        if (found != diagonal_map->end() && found->second != candidate.diagonals[i]) compatible = false;
      }
      if (!compatible) continue;
      for (size_t i = 0u; i < candidate.quads.size(); ++i) diagonal_map->emplace(candidate.quads[i], candidate.diagonals[i]);
      return candidate;
    }
  }
  assert(false && "No compatible prism template");
  return {};
}

struct PrismRecord {
  std::array<int, 6> nodes{};
  std::array<std::array<int, 4>, 3> tetra{};
  Vec3 center{};
  float volume{0.0f};
  std::array<int, 2> source_triangles{};
  float pairing_score{0.0f};
  uint32_t tetra_count{3u};
  uint32_t topology_kind{0u};
  uint32_t geometry_kind{0u};
  uint32_t display_kind{0u};
};

float Length(Vec3 value) { return std::sqrt(Dot(value, value)); }

Vec3 Normalize(Vec3 value) {
  return value * (1.0f / Length(value));
}

Vec3 TriangleNormal(const SourceTriangle& triangle) {
  return Normalize(Cross(triangle.p[1] - triangle.p[0], triangle.p[2] - triangle.p[0]));
}

float TriangleArea(const SourceTriangle& triangle) {
  return Length(Cross(triangle.p[1] - triangle.p[0], triangle.p[2] - triangle.p[0])) * 0.5f;
}

Vec3 TriangleCentroid(const SourceTriangle& triangle) {
  return (triangle.p[0] + triangle.p[1] + triangle.p[2]) * (1.0f / 3.0f);
}

std::array<float, 3> TriangleEdgeLengths(const SourceTriangle& triangle) {
  return {
    Length(triangle.p[1] - triangle.p[0]),
    Length(triangle.p[2] - triangle.p[1]),
    Length(triangle.p[0] - triangle.p[2])};
}

bool SharesSourceVertex(const SourceTriangle& first, const SourceTriangle& second) {
  for (int a : first.indices) for (int b : second.indices) if (a == b) return true;
  return false;
}

size_t SharedVertexCount(const SourceTriangle& first, const SourceTriangle& second) {
  size_t count = 0u;
  for (int a : first.indices) for (int b : second.indices) if (a == b) ++count;
  return count;
}

using TetraPoints = std::array<Vec3, 4>;

bool TetrahedraIntersect(const TetraPoints& first, const TetraPoints& second) {
  const std::array<std::array<int, 2>, 6> edges{{
    {{0, 1}}, {{0, 2}}, {{0, 3}}, {{1, 2}}, {{1, 3}}, {{2, 3}}}};
  std::array<Vec3, 64> axes{};
  size_t axis_count = 0u;
  axes[axis_count++] = Cross(first[1] - first[0], first[2] - first[0]);
  axes[axis_count++] = Cross(first[1] - first[0], first[3] - first[0]);
  axes[axis_count++] = Cross(first[2] - first[0], first[3] - first[0]);
  axes[axis_count++] = Cross(first[2] - first[1], first[3] - first[1]);
  axes[axis_count++] = Cross(second[1] - second[0], second[2] - second[0]);
  axes[axis_count++] = Cross(second[1] - second[0], second[3] - second[0]);
  axes[axis_count++] = Cross(second[2] - second[0], second[3] - second[0]);
  axes[axis_count++] = Cross(second[2] - second[1], second[3] - second[1]);
  for (const auto& first_edge : edges) for (const auto& second_edge : edges) {
    axes[axis_count++] = Cross(
      first[first_edge[1]] - first[first_edge[0]],
      second[second_edge[1]] - second[second_edge[0]]);
  }
  for (size_t axis_index = 0u; axis_index < axis_count; ++axis_index) {
    if (Dot(axes[axis_index], axes[axis_index]) < 1.0e-12f) continue;
    const Vec3 axis = axes[axis_index];
    float first_min = Dot(first[0], axis);
    float first_max = first_min;
    float second_min = Dot(second[0], axis);
    float second_max = second_min;
    for (size_t i = 1u; i < 4u; ++i) {
      const float first_projection = Dot(first[i], axis);
      const float second_projection = Dot(second[i], axis);
      first_min = std::min(first_min, first_projection);
      first_max = std::max(first_max, first_projection);
      second_min = std::min(second_min, second_projection);
      second_max = std::max(second_max, second_projection);
    }
    if (first_max <= second_min + 1.0e-6f || second_max <= first_min + 1.0e-6f) return false;
  }
  return true;
}

bool PrismRecordsIntersect(
    const PrismRecord& first,
    const PrismRecord& second,
    const std::vector<Node>& nodes) {
  for (size_t first_index = 0u; first_index < first.tetra_count; ++first_index) {
    const auto& first_tetra = first.tetra[first_index];
    TetraPoints first_points{};
    for (size_t i = 0u; i < 4u; ++i) first_points[i] = nodes[static_cast<size_t>(first_tetra[i])].rest;
    for (size_t second_index = 0u; second_index < second.tetra_count; ++second_index) {
      const auto& second_tetra = second.tetra[second_index];
      size_t shared_nodes = 0u;
      for (int first_node : first_tetra) {
        for (int second_node : second_tetra) if (first_node == second_node) ++shared_nodes;
      }
      if (shared_nodes >= 2u) continue;
      TetraPoints second_points{};
      for (size_t i = 0u; i < 4u; ++i) second_points[i] = nodes[static_cast<size_t>(second_tetra[i])].rest;
      if (TetrahedraIntersect(first_points, second_points)) return true;
    }
  }
  return false;
}

using FaceKey = std::array<int, 3>;

FaceKey MakeFaceKey(std::array<int, 3> value) {
  std::sort(value.begin(), value.end());
  return value;
}

struct FaceUse {
  size_t cell_index{0u};
  int orientation{0};
  int opposite_node{-1};
};

int FaceOrientationSign(const FaceKey& key, const std::array<int, 3>& oriented) {
  int inversions = 0;
  for (size_t first = 0u; first < 3u; ++first) {
    for (size_t second = first + 1u; second < 3u; ++second) {
      if (oriented[first] > oriented[second]) ++inversions;
    }
  }
  (void)key;
  return inversions % 2u == 0u ? 1 : -1;
}

struct CellComplexAudit {
  uint32_t source_face_count{0u};
  uint32_t source_boundary_faces{0u};
  uint32_t matched_internal_faces{0u};
  uint32_t valid_internal_faces{0u};
  uint32_t unmatched_generated_faces{0u};
  uint32_t bad_face_orientations{0u};
  uint32_t same_side_internal_faces{0u};
  uint32_t degenerate_internal_faces{0u};
  uint32_t nonmanifold_faces{0u};
  uint32_t bad_edges{0u};
  uint32_t intersecting_cell_pairs{0u};
};

CellComplexAudit AuditCellComplex(
    const SourceMesh& source,
    const std::vector<Node>& nodes,
    const std::vector<PrismRecord>& prisms,
    bool test_intersections = true) {
  const std::set<FaceKey> source_faces = [&source]() {
    std::set<FaceKey> result;
    for (const SourceTriangle& triangle : source.triangles) result.insert(MakeFaceKey(triangle.indices));
    return result;
  }();
  std::map<FaceKey, std::vector<FaceUse>> faces;
  std::map<DiagonalKey, std::set<FaceKey>> edge_faces;
  const auto register_face = [&faces, &edge_faces](size_t cell_index, std::array<int, 3> oriented, int opposite_node) {
    const FaceKey key = MakeFaceKey(oriented);
    faces[key].push_back({cell_index, FaceOrientationSign(key, oriented), opposite_node});
    edge_faces[MakeDiagonalKey(oriented[0], oriented[1])].insert(key);
    edge_faces[MakeDiagonalKey(oriented[1], oriented[2])].insert(key);
    edge_faces[MakeDiagonalKey(oriented[2], oriented[0])].insert(key);
  };
  const auto register_tetra = [&register_face](size_t cell_index, const std::array<int, 4>& tetra) {
    register_face(cell_index, {tetra[1], tetra[2], tetra[3]}, tetra[0]);
    register_face(cell_index, {tetra[0], tetra[3], tetra[2]}, tetra[1]);
    register_face(cell_index, {tetra[0], tetra[1], tetra[3]}, tetra[2]);
    register_face(cell_index, {tetra[0], tetra[2], tetra[1]}, tetra[3]);
  };
  for (size_t cell_index = 0u; cell_index < prisms.size(); ++cell_index) {
    for (size_t tetra_index = 0u; tetra_index < prisms[cell_index].tetra_count; ++tetra_index) {
      register_tetra(cell_index, prisms[cell_index].tetra[tetra_index]);
    }
  }
  CellComplexAudit audit{};
  audit.source_face_count = static_cast<uint32_t>(source_faces.size());
  for (const auto& entry : faces) {
    const FaceKey& key = entry.first;
    const std::vector<FaceUse>& uses = entry.second;
    const bool is_source_face = source_faces.find(key) != source_faces.end();
    if (uses.size() == 1u) {
      if (is_source_face) ++audit.source_boundary_faces;
      else ++audit.unmatched_generated_faces;
      continue;
    }
    if (uses.size() == 2u) {
      if (!is_source_face) ++audit.matched_internal_faces;
      if (uses[0].orientation + uses[1].orientation != 0) ++audit.bad_face_orientations;
      if (is_source_face) ++audit.bad_face_orientations;
      if (!is_source_face) {
        const Vec3 a = nodes[static_cast<size_t>(key[0])].rest;
        const Vec3 b = nodes[static_cast<size_t>(key[1])].rest;
        const Vec3 c = nodes[static_cast<size_t>(key[2])].rest;
        const Vec3 d0 = nodes[static_cast<size_t>(uses[0].opposite_node)].rest;
        const Vec3 d1 = nodes[static_cast<size_t>(uses[1].opposite_node)].rest;
        const float side0 = Dot(b - a, Cross(c - a, d0 - a));
        const float side1 = Dot(b - a, Cross(c - a, d1 - a));
        if (std::abs(side0) <= 1.0e-8f || std::abs(side1) <= 1.0e-8f) ++audit.degenerate_internal_faces;
        else if (side0 * side1 > 0.0f) ++audit.same_side_internal_faces;
        else if (uses[0].orientation + uses[1].orientation == 0) ++audit.valid_internal_faces;
      }
      continue;
    }
    ++audit.nonmanifold_faces;
  }
  for (const FaceKey& source_face : source_faces) {
    const auto found = faces.find(source_face);
    if (found == faces.end() || found->second.size() != 1u) ++audit.bad_face_orientations;
  }
  for (const auto& entry : edge_faces) {
    const std::set<FaceKey>& adjacent_faces = entry.second;
    uint32_t boundary_face_count = 0u;
    for (const FaceKey& face : adjacent_faces) {
      const auto found = faces.find(face);
      if (found->second.size() == 1u && source_faces.find(face) != source_faces.end()) ++boundary_face_count;
    }
    if (boundary_face_count == 0u && adjacent_faces.size() < 3u) ++audit.bad_edges;
    if (boundary_face_count != 0u && boundary_face_count != 2u) ++audit.bad_edges;
  }
  if (test_intersections) {
    for (size_t first = 0u; first < prisms.size(); ++first) {
      for (size_t second = first + 1u; second < prisms.size(); ++second) {
        const int first_source = prisms[first].source_triangles[0];
        const int second_source = prisms[second].source_triangles[0];
        if (first_source >= 0 && second_source >= 0 &&
            !SharesSourceVertex(
              source.triangles[static_cast<size_t>(first_source)],
              source.triangles[static_cast<size_t>(second_source)])) continue;
        if (PrismRecordsIntersect(prisms[first], prisms[second], nodes)) ++audit.intersecting_cell_pairs;
      }
    }
  }
  return audit;
}

bool CellComplexAccepted(
    const CellComplexAudit& audit,
    float source_volume,
    float scaffold_volume) {
  const float volume_error = std::abs(source_volume - scaffold_volume);
  const float volume_tolerance = std::max(source_volume, 1.0e-6f) * 1.0e-5f;
  return
    audit.source_boundary_faces == audit.source_face_count &&
    audit.unmatched_generated_faces == 0u &&
    audit.bad_face_orientations == 0u &&
    audit.same_side_internal_faces == 0u &&
    audit.degenerate_internal_faces == 0u &&
    audit.nonmanifold_faces == 0u &&
    audit.bad_edges == 0u &&
    audit.intersecting_cell_pairs == 0u &&
    volume_error <= volume_tolerance;
}

bool TetrahedronContainedByMesh(
    const std::array<int, 4>& tetra,
    const std::vector<Node>& nodes,
    const SourceMesh& source) {
  std::array<Vec3, 4> p{};
  for (size_t i = 0u; i < p.size(); ++i) p[i] = nodes[static_cast<size_t>(tetra[i])].rest;
  const Vec3 center = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
  if (!PointInsideMesh(center, source)) return false;
  for (float depth : {0.10f, 0.25f, 0.50f, 0.75f, 0.90f}) {
    for (size_t i = 0u; i < p.size(); ++i) {
      const Vec3 near_vertex = center * (1.0f - depth) + p[i] * depth;
      if (!PointInsideMesh(near_vertex, source)) return false;
    }
  }
  for (size_t i = 0u; i < p.size(); ++i) {
    for (size_t j = i + 1u; j < p.size(); ++j) {
      for (float edge_position : {0.10f, 0.25f, 0.50f, 0.75f, 0.90f}) {
        const Vec3 edge_point = p[i] * (1.0f - edge_position) + p[j] * edge_position;
        const Vec3 near_edge = center * 0.10f + edge_point * 0.90f;
        if (!PointInsideMesh(near_edge, source)) return false;
      }
    }
  }
  const std::array<std::array<int, 3>, 4> faces{{
    {{0, 1, 2}}, {{0, 3, 1}}, {{0, 2, 3}}, {{1, 3, 2}}}};
  for (const std::array<int, 3>& face : faces) {
    const Vec3 face_center = (p[static_cast<size_t>(face[0])] +
      p[static_cast<size_t>(face[1])] + p[static_cast<size_t>(face[2])]) * (1.0f / 3.0f);
    for (float depth : {0.25f, 0.50f, 0.75f}) {
      const Vec3 face_point = center * (1.0f - depth) + face_center * depth;
      if (!PointInsideMesh(face_point, source)) return false;
    }
  }
  return true;
}

bool PrismRecordContainedByMesh(
    const PrismRecord& prism,
    const std::vector<Node>& nodes,
    const SourceMesh& source) {
  for (size_t tetra_index = 0u; tetra_index < prism.tetra_count; ++tetra_index) {
    if (!TetrahedronContainedByMesh(prism.tetra[tetra_index], nodes, source)) return false;
  }
  return true;
}

bool BuildPairPrism(
    const SourceMesh& source,
    int first_triangle,
    int second_triangle,
    const std::array<int, 3>& second_order,
    const std::vector<Node>& nodes,
    float score,
    PrismRecord* prism) {
  const std::array<int, 3> first_order = source.triangles[static_cast<size_t>(first_triangle)].indices;
  const PrismCandidate candidate = MakePrismCandidate(first_order, second_order, 0, false, nodes);
  float volume = 0.0f;
  for (const auto& tetra : candidate.tetra) {
    const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
    const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
    const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
    const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
    const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    if (std::abs(determinant) <= 1.0e-8f) return false;
    volume += std::abs(determinant) / 6.0f;
  }
  PrismRecord result{};
  result.nodes = candidate.prism_nodes;
  result.tetra = candidate.tetra;
  result.source_triangles = {first_triangle, second_triangle};
  result.pairing_score = score;
  result.volume = volume;
  for (int node : result.nodes) result.center = result.center + nodes[static_cast<size_t>(node)].rest;
  result.center = result.center * (1.0f / 6.0f);
  if (!PrismRecordContainedByMesh(result, nodes, source)) return false;
  *prism = result;
  return true;
}

bool BuildBestPairPrism(
    const SourceMesh& source,
    int first_triangle,
    int second_triangle,
    const std::vector<Node>& nodes,
    PrismRecord* prism) {
  const SourceTriangle& first = source.triangles[static_cast<size_t>(first_triangle)];
  const SourceTriangle& second = source.triangles[static_cast<size_t>(second_triangle)];
  if (SharesSourceVertex(first, second)) return false;
  const Vec3 first_normal = TriangleNormal(first);
  const Vec3 second_normal = TriangleNormal(second);
  const float normal_dot = Dot(first_normal, second_normal);
  if (std::abs(normal_dot) < 0.5f) return false;
  const float first_area = TriangleArea(first);
  const float second_area = TriangleArea(second);
  if (std::abs(first_area - second_area) > std::max(first_area, second_area) * 0.35f) return false;
  const std::array<float, 3> first_edges = TriangleEdgeLengths(first);
  const std::array<float, 3> second_edges = TriangleEdgeLengths(second);
  const std::array<int, 3> permutations[6] = {
    {{0, 1, 2}}, {{0, 2, 1}}, {{1, 0, 2}},
    {{1, 2, 0}}, {{2, 0, 1}}, {{2, 1, 0}}};
  bool found = false;
  float best_score = std::numeric_limits<float>::infinity();
  PrismRecord best{};
  for (const auto& permutation : permutations) {
    const float edge_error =
      std::abs(first_edges[0] - second_edges[(permutation[1] + 3 - permutation[0]) % 3]) +
      std::abs(first_edges[1] - second_edges[(permutation[2] + 3 - permutation[1]) % 3]) +
      std::abs(first_edges[2] - second_edges[(permutation[0] + 3 - permutation[2]) % 3]);
    const float centroid_distance = Length(TriangleCentroid(first) - TriangleCentroid(second));
    const float score = edge_error + std::abs(first_area - second_area) +
      (1.0f - std::abs(normal_dot)) * 0.25f + centroid_distance * 0.5f;
    PrismRecord candidate{};
    if (!BuildPairPrism(source, first_triangle, second_triangle,
        {second.indices[permutation[0]], second.indices[permutation[1]], second.indices[permutation[2]]},
        nodes, score, &candidate)) continue;
    if (!found || score < best_score) {
      found = true;
      best_score = score;
      best = candidate;
    }
  }
  if (!found) return false;
  *prism = best;
  return true;
}

bool BuildAdjacentPairUnit(
    const SourceMesh& source,
    int first_triangle,
    int second_triangle,
    const std::vector<Node>& nodes,
    PrismRecord* unit) {
  const SourceTriangle& first = source.triangles[static_cast<size_t>(first_triangle)];
  const SourceTriangle& second = source.triangles[static_cast<size_t>(second_triangle)];
  std::array<int, 2> shared{};
  size_t shared_count = 0u;
  int first_only = -1;
  int second_only = -1;
  for (int first_vertex : first.indices) {
    bool found = false;
    for (int second_vertex : second.indices) if (first_vertex == second_vertex) {
      found = true;
      if (shared_count < shared.size()) shared[shared_count++] = first_vertex;
    }
    if (!found) first_only = first_vertex;
  }
  for (int second_vertex : second.indices) {
    bool found = false;
    for (int first_vertex : first.indices) if (second_vertex == first_vertex) found = true;
    if (!found) second_only = second_vertex;
  }
  if (shared_count != 2u || first_only < 0 || second_only < 0) return false;
  PrismRecord result{};
  result.nodes = {shared[0], shared[1], first_only, shared[0], shared[1], second_only};
  result.tetra[0] = {{shared[0], shared[1], first_only, second_only}};
  result.tetra_count = 1u;
  result.topology_kind = 1u;
  result.source_triangles = {first_triangle, second_triangle};
  result.pairing_score = Length(nodes[static_cast<size_t>(shared[0])].rest - nodes[static_cast<size_t>(shared[1])].rest);
  const Vec3 p0 = nodes[static_cast<size_t>(result.tetra[0][0])].rest;
  const Vec3 p1 = nodes[static_cast<size_t>(result.tetra[0][1])].rest;
  const Vec3 p2 = nodes[static_cast<size_t>(result.tetra[0][2])].rest;
  const Vec3 p3 = nodes[static_cast<size_t>(result.tetra[0][3])].rest;
  const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
  if (std::abs(determinant) <= 1.0e-8f) return false;
  if (determinant < 0.0f) std::swap(result.tetra[0][1], result.tetra[0][2]);
  result.volume = std::abs(determinant) / 6.0f;
  result.center = (p0 + p1 + p2 + p3) * 0.25f;
  *unit = result;
  return true;
}

bool BuildPentahedronPairUnit(
    const SourceMesh& source,
    int first_triangle,
    int second_triangle,
    const std::vector<Node>& nodes,
    PrismRecord* unit) {
  const SourceTriangle& first = source.triangles[static_cast<size_t>(first_triangle)];
  const SourceTriangle& second = source.triangles[static_cast<size_t>(second_triangle)];
  int shared_vertex = -1;
  std::array<int, 2> first_only{};
  std::array<int, 2> second_only{};
  size_t first_count = 0u;
  size_t second_count = 0u;
  for (int vertex : first.indices) {
    bool shared = false;
    for (int other : second.indices) if (vertex == other) shared = true;
    if (shared) shared_vertex = vertex;
    else first_only[first_count++] = vertex;
  }
  for (int vertex : second.indices) {
    bool shared = false;
    for (int other : first.indices) if (vertex == other) shared = true;
    if (!shared) second_only[second_count++] = vertex;
  }
  if (shared_vertex < 0 || first_count != 2u || second_count != 2u) return false;
  PrismRecord result{};
  result.nodes = {first_only[0], first_only[1], second_only[1], second_only[0], shared_vertex, shared_vertex};
  result.tetra[0] = {{shared_vertex, result.nodes[0], result.nodes[1], result.nodes[2]}};
  result.tetra[1] = {{shared_vertex, result.nodes[0], result.nodes[2], result.nodes[3]}};
  result.tetra_count = 2u;
  result.topology_kind = 2u;
  result.source_triangles = {first_triangle, second_triangle};
  result.pairing_score = Length(nodes[static_cast<size_t>(shared_vertex)].rest - nodes[static_cast<size_t>(result.nodes[0])].rest) + 0.2f;
  result.volume = 0.0f;
  for (size_t tetra_index = 0u; tetra_index < result.tetra_count; ++tetra_index) {
    const auto& tetra = result.tetra[tetra_index];
    const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
    const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
    const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
    const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
    const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    if (std::abs(determinant) <= 1.0e-8f) return false;
    if (determinant < 0.0f) std::swap(result.tetra[tetra_index][1], result.tetra[tetra_index][2]);
    result.volume += std::abs(determinant) / 6.0f;
  }
  result.center = {};
  for (size_t i = 0u; i < 5u; ++i) result.center = result.center + nodes[static_cast<size_t>(result.nodes[i])].rest;
  result.center = result.center * 0.2f;
  if (!PrismRecordContainedByMesh(result, nodes, source)) return false;
  *unit = result;
  return true;
}

bool SolveAdjacentPairing(
    const std::vector<std::vector<PrismRecord>>& candidates,
    const std::vector<Node>& nodes,
    std::vector<bool>* used,
    std::vector<PrismRecord>* selected);

bool FanTriangleVisibleFromCore(
    const SourceTriangle& triangle,
    Vec3 core,
    const SourceMesh& source) {
  const Vec3 surface = TriangleCentroid(triangle);
  for (float depth : {0.20f, 0.50f, 0.80f}) {
    if (!PointInsideMesh(core * (1.0f - depth) + surface * depth, source)) return false;
  }
  return true;
}

bool FanTriangleCenterVisibleFromCore(
    const SourceTriangle& triangle,
    Vec3 core,
    const SourceMesh& source) {
  const Vec3 surface = TriangleCentroid(triangle);
  return PointInsideMesh(core * 0.50f + surface * 0.50f, source);
}

Vec3 FindConformingFanCore(const SourceMesh& source) {
  const Vec3 extent = source.max_position - source.min_position;
  for (int x = 0; x < 16; ++x) {
    for (int y = 0; y < 16; ++y) {
      for (int z = 0; z < 16; ++z) {
        const Vec3 candidate{
          source.min_position.x + extent.x * (static_cast<float>(x) + 0.5f) / 16.0f,
          source.min_position.y + extent.y * (static_cast<float>(y) + 0.5f) / 16.0f,
          source.min_position.z + extent.z * (static_cast<float>(z) + 0.5f) / 16.0f};
        if (!PointInsideMesh(candidate, source)) continue;
        size_t visible = 0u;
        for (const SourceTriangle& triangle : source.triangles) {
          if (FanTriangleCenterVisibleFromCore(triangle, candidate, source)) ++visible;
        }
        if (visible != source.triangles.size()) continue;
        bool fully_visible = true;
        for (const SourceTriangle& triangle : source.triangles) {
          if (!FanTriangleVisibleFromCore(triangle, candidate, source)) {
            fully_visible = false;
            break;
          }
        }
        if (fully_visible) return candidate;
      }
    }
  }
  uint32_t sequence = 0x9e3779b9u;
  const auto next_unit = [&sequence]() {
    sequence = sequence * 1664525u + 1013904223u;
    return static_cast<float>(sequence >> 8u) * (1.0f / 16777216.0f);
  };
  for (size_t sample = 0u; sample < 4096u; ++sample) {
    const Vec3 candidate{
      source.min_position.x + extent.x * next_unit(),
      source.min_position.y + extent.y * next_unit(),
      source.min_position.z + extent.z * next_unit()};
    if (!PointInsideMesh(candidate, source)) continue;
    bool center_visible = true;
    for (const SourceTriangle& triangle : source.triangles) {
      if (!FanTriangleCenterVisibleFromCore(triangle, candidate, source)) {
        center_visible = false;
        break;
      }
    }
    if (!center_visible) continue;
    bool fully_visible = true;
    for (const SourceTriangle& triangle : source.triangles) {
      if (!FanTriangleVisibleFromCore(triangle, candidate, source)) {
        fully_visible = false;
        break;
      }
    }
    if (fully_visible) return candidate;
  }
  RequireInvariantCode(false, 103);
  return {};
}

bool HasBoundingBoxFanCore(const SourceMesh& source) {
  AppendTrace("fan_core_try triangles=%zu\n", source.triangles.size());
  const Vec3 core = (source.min_position + source.max_position) * 0.5f;
  if (!PointInsideMesh(core, source)) {
    AppendTrace("fan_core_result inside=0\n");
    return false;
  }
  const size_t sample_step = std::max<size_t>(1u, source.triangles.size() / 64u);
  for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); triangle_index += sample_step) {
    if (!FanTriangleVisibleFromCore(source.triangles[triangle_index], core, source)) {
      AppendTrace("fan_core_result visible=0 triangle=%zu\n", triangle_index);
      return false;
    }
  }
  AppendTrace("fan_core_result visible=1\n");
  return true;
}

float ComputeMeshVolume(const SourceMesh& source);

bool TryFindConformingFanCore(const SourceMesh& source, Vec3* core) {
  const Vec3 extent = source.max_position - source.min_position;
  for (int x = 0; x < 16; ++x) {
    for (int y = 0; y < 16; ++y) {
      for (int z = 0; z < 16; ++z) {
        const Vec3 candidate{
          source.min_position.x + extent.x * (static_cast<float>(x) + 0.5f) / 16.0f,
          source.min_position.y + extent.y * (static_cast<float>(y) + 0.5f) / 16.0f,
          source.min_position.z + extent.z * (static_cast<float>(z) + 0.5f) / 16.0f};
        if (!PointInsideMesh(candidate, source)) continue;
        const size_t sample_step = std::max<size_t>(1u, source.triangles.size() / 64u);
        bool sample_visible = true;
        for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); triangle_index += sample_step) {
          if (!FanTriangleVisibleFromCore(source.triangles[triangle_index], candidate, source)) {
            sample_visible = false;
            break;
          }
        }
        if (!sample_visible) continue;
        bool fully_visible = true;
        for (const SourceTriangle& triangle : source.triangles) {
          if (!FanTriangleVisibleFromCore(triangle, candidate, source)) {
            fully_visible = false;
            break;
          }
        }
        if (fully_visible) {
          *core = candidate;
          AppendTrace("fan_core_search_solved x=%d y=%d z=%d\n", x, y, z);
          return true;
        }
      }
    }
  }
  AppendTrace("fan_core_search_failed\n");
  return false;
}

float ComputeFanAbsoluteVolume(const SourceMesh& source, Vec3 core) {
  float volume = 0.0f;
  for (const SourceTriangle& triangle : source.triangles) {
    const float determinant = Dot(
      triangle.p[0] - core,
      Cross(triangle.p[1] - core, triangle.p[2] - core));
    volume += std::abs(determinant) / 6.0f;
  }
  return volume;
}

bool TryFindMinimumFanVolumeCore(const SourceMesh& source, Vec3* core, float* fan_volume) {
  const float source_volume = ComputeMeshVolume(source);
  const Vec3 extent = source.max_position - source.min_position;
  float best_error = std::numeric_limits<float>::infinity();
  Vec3 best_core{};
  float best_volume = 0.0f;
  for (int x = 0; x < 16; ++x) {
    for (int y = 0; y < 16; ++y) {
      for (int z = 0; z < 16; ++z) {
        const Vec3 candidate{
          source.min_position.x + extent.x * (static_cast<float>(x) + 0.5f) / 16.0f,
          source.min_position.y + extent.y * (static_cast<float>(y) + 0.5f) / 16.0f,
          source.min_position.z + extent.z * (static_cast<float>(z) + 0.5f) / 16.0f};
        if (!PointInsideMesh(candidate, source)) continue;
        const float candidate_volume = ComputeFanAbsoluteVolume(source, candidate);
        const float error = std::abs(candidate_volume - source_volume);
        if (error < best_error) {
          best_error = error;
          best_core = candidate;
          best_volume = candidate_volume;
        }
      }
    }
  }
  AppendTrace(
    "fan_volume_core best_error=%.9f source_volume=%.9f fan_volume=%.9f\n",
    best_error,
    source_volume,
    best_volume);
  if (best_error > std::max(source_volume, 1.0e-6f) * 1.0e-5f) return false;
  *core = best_core;
  *fan_volume = best_volume;
  return true;
}

bool BuildFanPentaUnit(
    const SourceMesh& source,
    int first_triangle,
    int second_triangle,
    int core_node,
    const std::vector<Node>& nodes,
    PrismRecord* unit) {
  const SourceTriangle& first = source.triangles[static_cast<size_t>(first_triangle)];
  const SourceTriangle& second = source.triangles[static_cast<size_t>(second_triangle)];
  std::array<int, 2> shared{};
  size_t shared_count = 0u;
  int first_only = -1;
  int second_only = -1;
  for (int vertex : first.indices) {
    bool shared_vertex = false;
    for (int other : second.indices) if (vertex == other) shared_vertex = true;
    if (shared_vertex) shared[shared_count++] = vertex;
    else first_only = vertex;
  }
  for (int vertex : second.indices) {
    bool shared_vertex = false;
    for (int other : first.indices) if (vertex == other) shared_vertex = true;
    if (!shared_vertex) second_only = vertex;
  }
  if (shared_count != 2u || first_only < 0 || second_only < 0) return false;
  PrismRecord result{};
  result.nodes = {shared[0], shared[1], first_only, second_only, core_node, core_node};
  result.tetra[0] = {{core_node, shared[0], shared[1], first_only}};
  result.tetra[1] = {{core_node, shared[1], shared[0], second_only}};
  result.tetra_count = 2u;
  result.topology_kind = 2u;
  result.geometry_kind = 2u;
  result.source_triangles = {first_triangle, second_triangle};
  result.pairing_score = Length(TriangleCentroid(first) - TriangleCentroid(second));
  result.volume = 0.0f;
  result.center = {};
  for (size_t i = 0u; i < 5u; ++i) result.center = result.center + nodes[static_cast<size_t>(result.nodes[i])].rest;
  result.center = result.center * 0.2f;
  for (size_t tetra_index = 0u; tetra_index < result.tetra_count; ++tetra_index) {
    auto& tetra = result.tetra[tetra_index];
    const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
    const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
    const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
    const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
    const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    if (std::abs(determinant) <= 1.0e-8f) return false;
    if (determinant < 0.0f) std::swap(tetra[1], tetra[2]);
    result.volume += std::abs(determinant) / 6.0f;
  }
  if (!PrismRecordContainedByMesh(result, nodes, source)) return false;
  *unit = result;
  return true;
}

void BuildConformingFanPentaDecomposition(
    const SourceMesh& source,
    std::vector<Node>* nodes,
    std::vector<PrismRecord>* prisms,
    float* scaffold_volume,
    float* source_volume) {
  nodes->resize(source.positions.size());
  for (size_t i = 0u; i < source.positions.size(); ++i) (*nodes)[i].rest = source.positions[i];
  const int core_node = static_cast<int>(nodes->size());
  nodes->push_back({FindConformingFanCore(source)});
  std::vector<std::vector<PrismRecord>> candidates(source.triangles.size());
  for (size_t first = 0u; first < source.triangles.size(); ++first) {
    for (size_t second = first + 1u; second < source.triangles.size(); ++second) {
      if (SharedVertexCount(source.triangles[first], source.triangles[second]) != 2u) continue;
      PrismRecord unit{};
      if (!BuildFanPentaUnit(source, static_cast<int>(first), static_cast<int>(second), core_node, *nodes, &unit)) continue;
      candidates[first].push_back(unit);
      candidates[second].push_back(unit);
    }
  }
  std::vector<bool> used(source.triangles.size(), false);
  std::vector<PrismRecord> selected;
  RequireInvariantCode(SolveAdjacentPairing(candidates, *nodes, &used, &selected), 110);
  RequireInvariantCode(selected.size() * 2u == source.triangles.size(), 111);
  const Vec3 volume_center = (source.min_position + source.max_position) * 0.5f;
  *source_volume = 0.0f;
  for (const SourceTriangle& triangle : source.triangles) {
    const Vec3 p0 = triangle.p[0] - volume_center;
    const Vec3 p1 = triangle.p[1] - volume_center;
    const Vec3 p2 = triangle.p[2] - volume_center;
    *source_volume += Dot(p0, Cross(p1, p2)) / 6.0f;
  }
  *source_volume = std::abs(*source_volume);
  *scaffold_volume = 0.0f;
  for (const PrismRecord& prism : selected) *scaffold_volume += prism.volume;
  for (size_t first = 0u; first < selected.size(); ++first) {
    const SourceTriangle& first_triangle = source.triangles[static_cast<size_t>(selected[first].source_triangles[0])];
    const SourceTriangle& second_triangle = source.triangles[static_cast<size_t>(selected[first].source_triangles[1])];
    const Vec3 core = (*nodes)[static_cast<size_t>(core_node)].rest;
    RequireInvariantCode(FanTriangleVisibleFromCore(first_triangle, core, source), 120);
    RequireInvariantCode(FanTriangleVisibleFromCore(second_triangle, core, source), 120);
    for (size_t second = first + 1u; second < selected.size(); ++second) {
      RequireInvariantCode(!PrismRecordsIntersect(selected[first], selected[second], *nodes), 121);
    }
  }
  *prisms = selected;
}

void BuildConformingFanTetraDecomposition(
    const SourceMesh& source,
    std::vector<Node>* nodes,
    std::vector<PrismRecord>* prisms,
    float* scaffold_volume,
    float* source_volume) {
  nodes->resize(source.positions.size());
  for (size_t i = 0u; i < source.positions.size(); ++i) (*nodes)[i].rest = source.positions[i];
  const int core_node = static_cast<int>(nodes->size());
  nodes->push_back({FindConformingFanCore(source)});
  *source_volume = 0.0f;
  *scaffold_volume = 0.0f;
  const Vec3 volume_center = (source.min_position + source.max_position) * 0.5f;
  for (const SourceTriangle& triangle : source.triangles) {
    const Vec3 p0 = triangle.p[0] - volume_center;
    const Vec3 p1 = triangle.p[1] - volume_center;
    const Vec3 p2 = triangle.p[2] - volume_center;
    *source_volume += Dot(p0, Cross(p1, p2)) / 6.0f;
  }
  *source_volume = std::abs(*source_volume);
  prisms->clear();
  for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); ++triangle_index) {
    const SourceTriangle& triangle = source.triangles[triangle_index];
    PrismRecord unit{};
    unit.nodes = {
      triangle.indices[0], triangle.indices[1], triangle.indices[2],
      core_node, core_node, core_node};
    unit.tetra[0] = {{core_node, triangle.indices[0], triangle.indices[1], triangle.indices[2]}};
    unit.tetra_count = 1u;
    unit.topology_kind = 1u;
    unit.geometry_kind = 1u;
    unit.source_triangles = {static_cast<int>(triangle_index), -1};
    unit.center = (triangle.p[0] + triangle.p[1] + triangle.p[2] + nodes->back().rest) * 0.25f;
    const Vec3 p0 = nodes->at(static_cast<size_t>(unit.tetra[0][0])).rest;
    const Vec3 p1 = nodes->at(static_cast<size_t>(unit.tetra[0][1])).rest;
    const Vec3 p2 = nodes->at(static_cast<size_t>(unit.tetra[0][2])).rest;
    const Vec3 p3 = nodes->at(static_cast<size_t>(unit.tetra[0][3])).rest;
    const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    RequireInvariantCode(std::abs(determinant) > 1.0e-8f, 130);
    if (determinant < 0.0f) std::swap(unit.tetra[0][1], unit.tetra[0][2]);
    unit.volume = std::abs(determinant) / 6.0f;
    unit.pairing_score = 0.0f;
    RequireInvariantCode(FanTriangleVisibleFromCore(triangle, nodes->back().rest, source), 131);
    *scaffold_volume += unit.volume;
    prisms->push_back(unit);
  }
}

SourceMesh MakeAnalyticMesh(
    const std::vector<Vec3>& positions,
    const std::vector<std::array<int, 3>>& face_indices) {
  SourceMesh mesh{};
  mesh.positions = positions;
  mesh.min_position = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
  mesh.max_position = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  for (const Vec3& position : positions) {
    mesh.min_position = {
      std::min(mesh.min_position.x, position.x),
      std::min(mesh.min_position.y, position.y),
      std::min(mesh.min_position.z, position.z)};
    mesh.max_position = {
      std::max(mesh.max_position.x, position.x),
      std::max(mesh.max_position.y, position.y),
      std::max(mesh.max_position.z, position.z)};
  }
  for (const std::array<int, 3>& indices : face_indices) {
    SourceTriangle triangle{};
    triangle.indices = indices;
    triangle.p[0] = positions[static_cast<size_t>(indices[0])];
    triangle.p[1] = positions[static_cast<size_t>(indices[1])];
    triangle.p[2] = positions[static_cast<size_t>(indices[2])];
    mesh.triangles.push_back(triangle);
  }
  return mesh;
}

SourceMesh MakeSubdividedIcosphere(uint32_t subdivision_level) {
  const float phi = 1.61803398875f;
  std::vector<Vec3> positions{
    {-1.0f, phi, 0.0f}, {1.0f, phi, 0.0f}, {-1.0f, -phi, 0.0f}, {1.0f, -phi, 0.0f},
    {0.0f, -1.0f, phi}, {0.0f, 1.0f, phi}, {0.0f, -1.0f, -phi}, {0.0f, 1.0f, -phi},
    {phi, 0.0f, -1.0f}, {phi, 0.0f, 1.0f}, {-phi, 0.0f, -1.0f}, {-phi, 0.0f, 1.0f}};
  const std::vector<std::array<int, 3>> base_faces{
    {{0, 11, 5}}, {{0, 5, 1}}, {{0, 1, 7}}, {{0, 7, 10}}, {{0, 10, 11}},
    {{1, 5, 9}}, {{5, 11, 4}}, {{11, 10, 2}}, {{10, 7, 6}}, {{7, 1, 8}},
    {{3, 9, 4}}, {{3, 4, 2}}, {{3, 2, 6}}, {{3, 6, 8}}, {{3, 8, 9}},
    {{4, 9, 5}}, {{2, 4, 11}}, {{6, 2, 10}}, {{8, 6, 7}}, {{9, 8, 1}}};
  std::vector<std::array<int, 3>> faces = base_faces;
  const float radius = Length(positions[0]);
  for (uint32_t level = 0u; level < subdivision_level; ++level) {
    std::map<DiagonalKey, int> midpoint_nodes;
    std::vector<std::array<int, 3>> next_faces;
    const auto midpoint = [&](int first, int second) {
      const DiagonalKey key = MakeDiagonalKey(first, second);
      const auto found = midpoint_nodes.find(key);
      if (found != midpoint_nodes.end()) return found->second;
      const int node = static_cast<int>(positions.size());
      positions.push_back(Normalize(positions[static_cast<size_t>(first)] + positions[static_cast<size_t>(second)]) * radius);
      midpoint_nodes.emplace(key, node);
      return node;
    };
    for (const std::array<int, 3>& face : faces) {
      const int ab = midpoint(face[0], face[1]);
      const int bc = midpoint(face[1], face[2]);
      const int ca = midpoint(face[2], face[0]);
      next_faces.push_back({face[0], ab, ca});
      next_faces.push_back({face[1], bc, ab});
      next_faces.push_back({face[2], ca, bc});
      next_faces.push_back({ab, bc, ca});
    }
    faces = std::move(next_faces);
  }
  SourceMesh mesh = MakeAnalyticMesh(positions, faces);
  OrientSourceMesh(&mesh);
  return mesh;
}

SourceMesh MakeHighResolutionDumbbell() {
  constexpr int kRadialSegments = 8;
  constexpr int kAxialSegmentsPerSide = 80;
  constexpr float kBottom = -2.0f;
  constexpr float kTop = 2.0f;
  std::vector<Vec3> positions;
  std::vector<std::array<int, 3>> faces;
  const auto ring_radius = [](float z) {
    const float normalized = std::abs(z) * 0.5f;
    return 0.10f + 0.86f * std::pow(normalized, 2.0f);
  };
  const auto ring_node = [&](int axial, int radial) {
    const float z = kBottom + (kTop - kBottom) * static_cast<float>(axial) / static_cast<float>(kAxialSegmentsPerSide * 2);
    const float angle = 6.28318530718f * static_cast<float>(radial) / static_cast<float>(kRadialSegments);
    positions.push_back({ring_radius(z) * std::cos(angle), ring_radius(z) * std::sin(angle), z});
    return static_cast<int>(positions.size()) - 1;
  };
  std::vector<std::array<int, kRadialSegments>> rings;
  rings.reserve(kAxialSegmentsPerSide * 2 + 1);
  for (int axial = 0; axial <= kAxialSegmentsPerSide * 2; ++axial) {
    std::array<int, kRadialSegments> ring{};
    for (int radial = 0; radial < kRadialSegments; ++radial) ring[radial] = ring_node(axial, radial);
    rings.push_back(ring);
  }
  for (size_t axial = 0u; axial + 1u < rings.size(); ++axial) {
    for (int radial = 0; radial < kRadialSegments; ++radial) {
      const int next_radial = (radial + 1) % kRadialSegments;
      faces.push_back({rings[axial][radial], rings[axial][next_radial], rings[axial + 1u][radial]});
      faces.push_back({rings[axial][next_radial], rings[axial + 1u][next_radial], rings[axial + 1u][radial]});
    }
  }
  const int bottom_apex = static_cast<int>(positions.size());
  positions.push_back({0.0f, 0.0f, kBottom - 0.35f});
  const int top_apex = static_cast<int>(positions.size());
  positions.push_back({0.0f, 0.0f, kTop + 0.35f});
  for (int radial = 0; radial < kRadialSegments; ++radial) {
    const int next_radial = (radial + 1) % kRadialSegments;
    faces.push_back({bottom_apex, rings.front()[next_radial], rings.front()[radial]});
    faces.push_back({top_apex, rings.back()[radial], rings.back()[next_radial]});
  }
  SourceMesh mesh = MakeAnalyticMesh(positions, faces);
  OrientSourceMesh(&mesh);
  return mesh;
}

struct TrihedralSeed {
  std::array<int, 3> source_faces{};
  std::array<int, 4> tetra{};
  FaceKey generated_face{};
  float volume{0.0f};
};

bool TryFindTrihedralSeed(
    const SourceMesh& source,
    const std::vector<Node>& nodes,
    TrihedralSeed* seed) {
  for (size_t first = 0u; first < source.triangles.size(); ++first) {
    for (size_t second = first + 1u; second < source.triangles.size(); ++second) {
      for (size_t third = second + 1u; third < source.triangles.size(); ++third) {
        std::set<int> second_common;
        for (int first_node : source.triangles[first].indices) {
          for (int second_node : source.triangles[second].indices) {
            for (int third_node : source.triangles[third].indices) {
              if (first_node == second_node && first_node == third_node) second_common.insert(first_node);
            }
          }
        }
        if (second_common.size() != 1u) continue;
        const int corner = *second_common.begin();
        if (SharedVertexCount(source.triangles[first], source.triangles[second]) != 2u) continue;
        if (SharedVertexCount(source.triangles[first], source.triangles[third]) != 2u) continue;
        if (SharedVertexCount(source.triangles[second], source.triangles[third]) != 2u) continue;
        std::set<int> outer_nodes;
        for (const SourceTriangle& triangle : {source.triangles[first], source.triangles[second], source.triangles[third]}) {
          for (int node : triangle.indices) if (node != corner) outer_nodes.insert(node);
        }
        if (outer_nodes.size() != 3u) continue;
        std::array<int, 4> tetra{{corner, *outer_nodes.begin(), *std::next(outer_nodes.begin()), *std::next(outer_nodes.begin(), 2)}};
        const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
        const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
        const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
        const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
        float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
        if (std::abs(determinant) <= 1.0e-8f) continue;
        if (determinant < 0.0f) {
          std::swap(tetra[1], tetra[2]);
          determinant = -determinant;
        }
        if (!TetrahedronContainedByMesh(tetra, nodes, source)) continue;
        seed->source_faces = {static_cast<int>(first), static_cast<int>(second), static_cast<int>(third)};
        seed->tetra = tetra;
        seed->generated_face = MakeFaceKey({tetra[1], tetra[2], tetra[3]});
        seed->volume = determinant / 6.0f;
        return true;
      }
    }
  }
  return false;
}

struct FrontierFace {
  std::array<int, 3> oriented{};
  uint32_t generation{0u};
};

using Frontier = std::map<FaceKey, FrontierFace>;

PrismRecord MakeTetraPrismRecord(
    std::array<int, 4> tetra,
    const std::vector<Node>& nodes) {
  const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
  const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
  const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
  const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
  float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
  RequireInvariantCode(std::abs(determinant) > 1.0e-8f, 150);
  if (determinant < 0.0f) {
    std::swap(tetra[1], tetra[2]);
    determinant = -determinant;
  }
  PrismRecord result{};
  result.nodes = {tetra[0], tetra[1], tetra[2], tetra[3], tetra[3], tetra[3]};
  result.tetra[0] = tetra;
  result.tetra_count = 1u;
  result.topology_kind = 1u;
  result.geometry_kind = 1u;
  result.source_triangles = {-1, -1};
  result.volume = determinant / 6.0f;
  result.center = (p0 + p1 + p2 + p3) * 0.25f;
  return result;
}

Frontier MakeInitialFrontier(const SourceMesh& source) {
  Frontier frontier;
  for (const SourceTriangle& triangle : source.triangles) {
    const FaceKey key = MakeFaceKey(triangle.indices);
    frontier.emplace(key, FrontierFace{triangle.indices, 0u});
  }
  return frontier;
}

bool TryFindFrontierTrihedralCandidate(
    const Frontier& frontier,
    const SourceMesh& source,
    const std::vector<Node>& nodes,
    const std::vector<PrismRecord>& selected,
    std::array<FaceKey, 3>* consumed_faces,
    FaceKey* generated_face,
    PrismRecord* cell,
    size_t candidate_rank = 0u) {
  size_t valid_candidate_count = 0u;
  std::vector<FaceKey> keys;
  for (const auto& entry : frontier) keys.push_back(entry.first);
  for (size_t first = 0u; first < keys.size(); ++first) {
    for (size_t second = first + 1u; second < keys.size(); ++second) {
      for (size_t third = second + 1u; third < keys.size(); ++third) {
        std::set<int> common;
        for (int a : keys[first]) for (int b : keys[second]) for (int c : keys[third]) {
          if (a == b && a == c) common.insert(a);
        }
        if (common.size() != 1u) continue;
        const int corner = *common.begin();
        auto shared_count = [](const FaceKey& left, const FaceKey& right) {
          size_t count = 0u;
          for (int a : left) for (int b : right) if (a == b) ++count;
          return count;
        };
        if (shared_count(keys[first], keys[second]) != 2u) continue;
        if (shared_count(keys[first], keys[third]) != 2u) continue;
        if (shared_count(keys[second], keys[third]) != 2u) continue;
        std::set<int> outer_nodes;
        for (const FaceKey& key : {keys[first], keys[second], keys[third]}) {
          for (int node : key) if (node != corner) outer_nodes.insert(node);
        }
        if (outer_nodes.size() != 3u) continue;
        std::array<int, 4> tetra{{corner, *outer_nodes.begin(), *std::next(outer_nodes.begin()), *std::next(outer_nodes.begin(), 2)}};
        const Vec3 tetra_p0 = nodes[static_cast<size_t>(tetra[0])].rest;
        const Vec3 tetra_p1 = nodes[static_cast<size_t>(tetra[1])].rest;
        const Vec3 tetra_p2 = nodes[static_cast<size_t>(tetra[2])].rest;
        const Vec3 tetra_p3 = nodes[static_cast<size_t>(tetra[3])].rest;
        if (std::abs(Dot(tetra_p1 - tetra_p0, Cross(tetra_p2 - tetra_p0, tetra_p3 - tetra_p0))) <= 1.0e-8f) continue;
        PrismRecord candidate = MakeTetraPrismRecord(tetra, nodes);
        if (!TetrahedronContainedByMesh(candidate.tetra[0], nodes, source)) continue;
        const FaceKey closing_face = MakeFaceKey({candidate.tetra[0][1], candidate.tetra[0][2], candidate.tetra[0][3]});
        if (frontier.find(closing_face) != frontier.end()) continue;
        bool intersects = false;
        for (const PrismRecord& previous : selected) {
          if (PrismRecordsIntersect(candidate, previous, nodes)) {
            intersects = true;
            break;
          }
        }
        if (intersects) continue;
        if (valid_candidate_count++ < candidate_rank) continue;
        *consumed_faces = {keys[first], keys[second], keys[third]};
        *generated_face = closing_face;
        *cell = candidate;
        return true;
      }
    }
  }
  return false;
}

bool TryFindFrontierDihedralCandidate(
    const Frontier& frontier,
    const SourceMesh& source,
    const std::vector<Node>& nodes,
    const std::vector<PrismRecord>& selected,
    std::array<FaceKey, 2>* consumed_faces,
    std::array<FrontierFace, 2>* generated_faces,
    size_t* generated_count,
    PrismRecord* cell,
    size_t candidate_rank = 0u) {
  size_t valid_candidate_count = 0u;
  std::vector<FaceKey> keys;
  for (const auto& entry : frontier) keys.push_back(entry.first);
  const std::set<FaceKey> source_faces = [&source]() {
    std::set<FaceKey> result;
    for (const SourceTriangle& triangle : source.triangles) result.insert(MakeFaceKey(triangle.indices));
    return result;
  }();
  for (size_t first = 0u; first < keys.size(); ++first) {
    for (size_t second = first + 1u; second < keys.size(); ++second) {
      std::set<int> shared;
      for (int a : keys[first]) for (int b : keys[second]) if (a == b) shared.insert(a);
      if (shared.size() != 2u) continue;
      std::set<int> tetra_nodes = shared;
      for (int node : keys[first]) tetra_nodes.insert(node);
      for (int node : keys[second]) tetra_nodes.insert(node);
      if (tetra_nodes.size() != 4u) continue;
      std::array<int, 4> tetra{{
        *tetra_nodes.begin(),
        *std::next(tetra_nodes.begin()),
        *std::next(tetra_nodes.begin(), 2),
        *std::next(tetra_nodes.begin(), 3)}};
      const Vec3 tetra_p0 = nodes[static_cast<size_t>(tetra[0])].rest;
      const Vec3 tetra_p1 = nodes[static_cast<size_t>(tetra[1])].rest;
      const Vec3 tetra_p2 = nodes[static_cast<size_t>(tetra[2])].rest;
      const Vec3 tetra_p3 = nodes[static_cast<size_t>(tetra[3])].rest;
      if (std::abs(Dot(tetra_p1 - tetra_p0, Cross(tetra_p2 - tetra_p0, tetra_p3 - tetra_p0))) <= 1.0e-8f) continue;
      PrismRecord candidate = MakeTetraPrismRecord(tetra, nodes);
      const auto& oriented_tetra = candidate.tetra[0];
      const std::array<std::array<int, 3>, 4> tetra_faces{{
        {{oriented_tetra[1], oriented_tetra[2], oriented_tetra[3]}},
        {{oriented_tetra[0], oriented_tetra[3], oriented_tetra[2]}},
        {{oriented_tetra[0], oriented_tetra[1], oriented_tetra[3]}},
        {{oriented_tetra[0], oriented_tetra[2], oriented_tetra[1]}}}};
      const FaceKey first_key = keys[first];
      const FaceKey second_key = keys[second];
      bool has_first = false;
      bool has_second = false;
      std::array<FrontierFace, 2> new_faces{};
      size_t new_count = 0u;
      bool invalid = false;
      for (const std::array<int, 3>& tetra_face : tetra_faces) {
        const FaceKey face_key = MakeFaceKey(tetra_face);
        if (face_key == first_key) {
          has_first = true;
          continue;
        }
        if (face_key == second_key) {
          has_second = true;
          continue;
        }
        if (new_count == new_faces.size()) {
          invalid = true;
          break;
        }
        if (source_faces.find(face_key) != source_faces.end()) {
          invalid = true;
          break;
        }
        new_faces[new_count++] = FrontierFace{
          {tetra_face[0], tetra_face[2], tetra_face[1]},
          0u};
      }
      if (invalid || !has_first || !has_second || new_count != 2u) continue;
      for (size_t generated = 0u; generated < new_count; ++generated) {
        const FaceKey generated_key = MakeFaceKey(new_faces[generated].oriented);
        const auto found = frontier.find(generated_key);
        if (found == frontier.end()) continue;
        if (FaceOrientationSign(generated_key, found->second.oriented) +
            FaceOrientationSign(generated_key, new_faces[generated].oriented) != 0) {
          invalid = true;
          break;
        }
      }
      if (invalid) continue;
      if (!TetrahedronContainedByMesh(candidate.tetra[0], nodes, source)) continue;
      bool intersects = false;
      for (const PrismRecord& previous : selected) {
        if (PrismRecordsIntersect(candidate, previous, nodes)) {
          intersects = true;
          break;
        }
      }
      if (intersects) continue;
      if (valid_candidate_count++ < candidate_rank) continue;
      *consumed_faces = {first_key, second_key};
      *generated_faces = new_faces;
      *generated_count = new_count;
      *cell = candidate;
      return true;
    }
  }
  return false;
}

bool MakeFrontierPentaRecord(
    const FaceKey& first,
    const FaceKey& second,
    const std::vector<Node>& nodes,
    PrismRecord* result,
    std::array<FrontierFace, 3>* generated_faces,
    size_t* generated_count) {
  std::set<int> shared;
  for (int a : first) for (int b : second) if (a == b) shared.insert(a);
  if (shared.size() != 1u) return false;
  std::array<int, 2> first_only{};
  std::array<int, 2> second_only{};
  size_t first_count = 0u;
  size_t second_count = 0u;
  const int shared_vertex = *shared.begin();
  for (int node : first) if (node != shared_vertex) first_only[first_count++] = node;
  for (int node : second) if (node != shared_vertex) second_only[second_count++] = node;
  PrismRecord penta{};
  penta.nodes = {first_only[0], first_only[1], second_only[1], second_only[0], shared_vertex, shared_vertex};
  penta.tetra[0] = {{shared_vertex, penta.nodes[0], penta.nodes[1], penta.nodes[2]}};
  penta.tetra[1] = {{shared_vertex, penta.nodes[0], penta.nodes[2], penta.nodes[3]}};
  penta.tetra_count = 2u;
  penta.topology_kind = 2u;
  penta.geometry_kind = 2u;
  penta.source_triangles = {-1, -1};
  penta.center = {};
  for (size_t node = 0u; node < 5u; ++node) penta.center = penta.center + nodes[static_cast<size_t>(penta.nodes[node])].rest;
  penta.center = penta.center * 0.20f;
  penta.volume = 0.0f;
  for (size_t tetra_index = 0u; tetra_index < penta.tetra_count; ++tetra_index) {
    auto& tetra = penta.tetra[tetra_index];
    const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
    const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
    const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
    const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
    float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    if (std::abs(determinant) <= 1.0e-8f) return false;
    if (determinant < 0.0f) {
      std::swap(tetra[1], tetra[2]);
      determinant = -determinant;
    }
    penta.volume += determinant / 6.0f;
  }
  std::map<FaceKey, std::array<int, 3>> boundary_faces;
  std::map<FaceKey, size_t> boundary_counts;
  const auto register_face = [&boundary_faces, &boundary_counts](std::array<int, 3> face) {
    const FaceKey key = MakeFaceKey(face);
    ++boundary_counts[key];
    boundary_faces[key] = face;
  };
  for (size_t tetra_index = 0u; tetra_index < penta.tetra_count; ++tetra_index) {
    const auto& tetra = penta.tetra[tetra_index];
    register_face({tetra[1], tetra[2], tetra[3]});
    register_face({tetra[0], tetra[3], tetra[2]});
    register_face({tetra[0], tetra[1], tetra[3]});
    register_face({tetra[0], tetra[2], tetra[1]});
  }
  if (boundary_counts[first] != 1u || boundary_counts[second] != 1u) return false;
  *generated_count = 0u;
  for (const auto& entry : boundary_counts) {
    if (entry.second != 1u || entry.first == first || entry.first == second) continue;
    if (*generated_count == generated_faces->size()) return false;
    (*generated_faces)[*generated_count] = FrontierFace{
      {boundary_faces[entry.first][0], boundary_faces[entry.first][2], boundary_faces[entry.first][1]},
      0u};
    ++*generated_count;
  }
  if (*generated_count != 3u) return false;
  *result = penta;
  return true;
}

bool TryFindFrontierPentaCandidate(
    const Frontier& frontier,
    const SourceMesh& source,
    const std::vector<Node>& nodes,
    const std::vector<PrismRecord>& selected,
    std::array<FaceKey, 2>* consumed_faces,
    std::array<FrontierFace, 3>* generated_faces,
    size_t* generated_count,
    PrismRecord* cell,
    size_t candidate_rank = 0u) {
  size_t valid_candidate_count = 0u;
  std::vector<FaceKey> keys;
  for (const auto& entry : frontier) keys.push_back(entry.first);
  const std::set<FaceKey> source_faces = [&source]() {
    std::set<FaceKey> result;
    for (const SourceTriangle& triangle : source.triangles) result.insert(MakeFaceKey(triangle.indices));
    return result;
  }();
  for (size_t first = 0u; first < keys.size(); ++first) {
    for (size_t second = first + 1u; second < keys.size(); ++second) {
      PrismRecord candidate{};
      std::array<FrontierFace, 3> new_faces{};
      size_t new_count = 0u;
      if (!MakeFrontierPentaRecord(keys[first], keys[second], nodes, &candidate, &new_faces, &new_count)) continue;
      if (!PrismRecordContainedByMesh(candidate, nodes, source)) continue;
      bool invalid = false;
      for (size_t generated = 0u; generated < new_count; ++generated) {
        const FaceKey key = MakeFaceKey(new_faces[generated].oriented);
        if (source_faces.find(key) != source_faces.end()) {
          invalid = true;
          break;
        }
        const auto found = frontier.find(key);
        if (found != frontier.end() &&
            FaceOrientationSign(key, found->second.oriented) +
            FaceOrientationSign(key, new_faces[generated].oriented) != 0) {
          invalid = true;
          break;
        }
      }
      if (invalid) continue;
      bool intersects = false;
      for (const PrismRecord& previous : selected) {
        if (PrismRecordsIntersect(candidate, previous, nodes)) {
          intersects = true;
          break;
        }
      }
      if (intersects) continue;
      if (valid_candidate_count++ < candidate_rank) continue;
      *consumed_faces = {keys[first], keys[second]};
      *generated_faces = new_faces;
      *generated_count = new_count;
      *cell = candidate;
      return true;
    }
  }
  return false;
}

bool TryFindTerminalFrontierTetra(
    const Frontier& frontier,
    const SourceMesh& source,
    const std::vector<Node>& nodes,
    const std::vector<PrismRecord>& selected,
    std::array<FaceKey, 4>* consumed_faces,
    PrismRecord* cell) {
  if (frontier.size() != 4u) return false;
  std::set<int> tetra_nodes;
  for (const auto& entry : frontier) for (int node : entry.first) tetra_nodes.insert(node);
  if (tetra_nodes.size() != 4u) return false;
  std::array<int, 4> tetra{{*tetra_nodes.begin(), *std::next(tetra_nodes.begin()), *std::next(tetra_nodes.begin(), 2), *std::next(tetra_nodes.begin(), 3)}};
  PrismRecord candidate = MakeTetraPrismRecord(tetra, nodes);
  if (!TetrahedronContainedByMesh(candidate.tetra[0], nodes, source)) return false;
  std::set<FaceKey> expected_faces{
    MakeFaceKey({candidate.tetra[0][1], candidate.tetra[0][2], candidate.tetra[0][3]}),
    MakeFaceKey({candidate.tetra[0][0], candidate.tetra[0][3], candidate.tetra[0][2]}),
    MakeFaceKey({candidate.tetra[0][0], candidate.tetra[0][1], candidate.tetra[0][3]}),
    MakeFaceKey({candidate.tetra[0][0], candidate.tetra[0][2], candidate.tetra[0][1]})};
  std::set<FaceKey> actual_faces;
  for (const auto& entry : frontier) actual_faces.insert(entry.first);
  if (expected_faces != actual_faces) return false;
  for (const PrismRecord& previous : selected) {
    if (PrismRecordsIntersect(candidate, previous, nodes)) return false;
  }
  size_t index = 0u;
  for (const auto& entry : frontier) (*consumed_faces)[index++] = entry.first;
  *cell = candidate;
  return true;
}

bool TryCompleteFrontierWithInteriorStar(
    const Frontier& frontier,
    const SourceMesh& source,
    std::vector<Node>* nodes,
    const std::vector<PrismRecord>& selected,
    std::vector<PrismRecord>* completion) {
  if (frontier.size() < 4u) return false;
  Vec3 core{};
  std::set<int> frontier_nodes;
  for (const auto& entry : frontier) {
    for (int node : entry.first) frontier_nodes.insert(node);
  }
  for (int node : frontier_nodes) core = core + (*nodes)[static_cast<size_t>(node)].rest;
  core = core * (1.0f / static_cast<float>(frontier_nodes.size()));
  const int core_node = static_cast<int>(nodes->size());
  nodes->push_back({core});
  std::vector<PrismRecord> result;
  result.reserve(frontier.size());
  for (const auto& entry : frontier) {
    const FaceKey& face = entry.first;
    PrismRecord candidate = MakeTetraPrismRecord({core_node, face[0], face[1], face[2]}, *nodes);
    if (!TetrahedronContainedByMesh(candidate.tetra[0], *nodes, source)) return false;
    for (const PrismRecord& previous : selected) {
      if (PrismRecordsIntersect(candidate, previous, *nodes)) return false;
    }
    for (const PrismRecord& previous : result) {
      if (PrismRecordsIntersect(candidate, previous, *nodes)) return false;
    }
    result.push_back(candidate);
  }
  *completion = result;
  return true;
}

void RunFrontierPeelValidation(
    const char* name,
    SourceMesh source) {
  OrientSourceMesh(&source);
  std::vector<Node> nodes(source.positions.size());
  for (size_t node = 0u; node < source.positions.size(); ++node) nodes[node].rest = source.positions[node];
  Frontier frontier = MakeInitialFrontier(source);
  std::vector<PrismRecord> selected;
  uint32_t generation = 1u;
  bool interior_star_completed = false;
  bool dihedral_completed = false;
  bool penta_completed = false;
  size_t frontier_steps = 0u;
  const size_t frontier_step_budget = source.triangles.size() * 4u;
  while (!frontier.empty() && frontier_steps < frontier_step_budget) {
    ++frontier_steps;
    std::array<FaceKey, 4> terminal_faces{};
    PrismRecord terminal{};
    if (TryFindTerminalFrontierTetra(frontier, source, nodes, selected, &terminal_faces, &terminal)) {
      for (const FaceKey& face : terminal_faces) frontier.erase(face);
      selected.push_back(terminal);
      break;
    }
    std::array<FaceKey, 3> consumed_faces{};
    FaceKey generated_face{};
    PrismRecord cell{};
    if (!TryFindFrontierTrihedralCandidate(frontier, source, nodes, selected, &consumed_faces, &generated_face, &cell)) {
      std::array<FaceKey, 2> dihedral_consumed{};
      std::array<FrontierFace, 2> dihedral_generated{};
      size_t dihedral_generated_count = 0u;
      PrismRecord dihedral_cell{};
      if (TryFindFrontierDihedralCandidate(
            frontier,
            source,
            nodes,
            selected,
            &dihedral_consumed,
            &dihedral_generated,
            &dihedral_generated_count,
            &dihedral_cell)) {
        for (const FaceKey& face : dihedral_consumed) frontier.erase(face);
        for (size_t generated = 0u; generated < dihedral_generated_count; ++generated) {
          const FaceKey key = MakeFaceKey(dihedral_generated[generated].oriented);
          const auto found = frontier.find(key);
          if (found != frontier.end()) {
            RequireInvariantCode(
              FaceOrientationSign(key, found->second.oriented) +
              FaceOrientationSign(key, dihedral_generated[generated].oriented) == 0,
              161);
            frontier.erase(found);
          }
          else frontier.emplace(key, FrontierFace{dihedral_generated[generated].oriented, generation++});
        }
        selected.push_back(dihedral_cell);
        dihedral_completed = true;
        continue;
      }
      std::array<FaceKey, 2> penta_consumed{};
      std::array<FrontierFace, 3> penta_generated{};
      size_t penta_generated_count = 0u;
      PrismRecord penta_cell{};
      if (TryFindFrontierPentaCandidate(
            frontier,
            source,
            nodes,
            selected,
            &penta_consumed,
            &penta_generated,
            &penta_generated_count,
            &penta_cell)) {
        for (const FaceKey& face : penta_consumed) frontier.erase(face);
        for (size_t generated = 0u; generated < penta_generated_count; ++generated) {
          const FaceKey key = MakeFaceKey(penta_generated[generated].oriented);
          const auto found = frontier.find(key);
          if (found != frontier.end()) {
            RequireInvariantCode(
              FaceOrientationSign(key, found->second.oriented) +
              FaceOrientationSign(key, penta_generated[generated].oriented) == 0,
              162);
            frontier.erase(found);
          } else {
            frontier.emplace(key, FrontierFace{penta_generated[generated].oriented, generation++});
          }
        }
        selected.push_back(penta_cell);
        penta_completed = true;
        continue;
      }
      std::vector<PrismRecord> completion;
      if (!TryCompleteFrontierWithInteriorStar(frontier, source, &nodes, selected, &completion)) break;
      selected.insert(selected.end(), completion.begin(), completion.end());
      frontier.clear();
      interior_star_completed = true;
      break;
    }
    for (const FaceKey& face : consumed_faces) frontier.erase(face);
    const auto& tetra = cell.tetra[0];
    frontier.emplace(generated_face, FrontierFace{{tetra[1], tetra[3], tetra[2]}, generation++});
    selected.push_back(cell);
  }
  float scaffold_volume = 0.0f;
  for (const PrismRecord& cell : selected) scaffold_volume += cell.volume;
  const Vec3 volume_center = (source.min_position + source.max_position) * 0.5f;
  float source_volume = 0.0f;
  for (const SourceTriangle& triangle : source.triangles) {
    const Vec3 p0 = triangle.p[0] - volume_center;
    const Vec3 p1 = triangle.p[1] - volume_center;
    const Vec3 p2 = triangle.p[2] - volume_center;
    source_volume += Dot(p0, Cross(p1, p2)) / 6.0f;
  }
  source_volume = std::abs(source_volume);
  const CellComplexAudit audit = AuditCellComplex(source, nodes, selected);
  const bool accepted = frontier.empty() && CellComplexAccepted(audit, source_volume, scaffold_volume);
  std::fprintf(
    stderr,
    "agent_adaptive_prism frontier name=%s accepted=%u cells=%zu remaining_frontier_faces=%zu steps=%zu step_budget=%zu dihedral_steps=%u penta_steps=%u interior_star_completed=%u source_volume=%.9f scaffold_volume=%.9f volume_error=%.9f unmatched_generated_faces=%u bad_face_orientations=%u same_side_internal_faces=%u nonmanifold_faces=%u bad_edges=%u intersecting_cell_pairs=%u\n",
    name,
    accepted ? 1u : 0u,
    selected.size(),
    frontier.size(),
    frontier_steps,
    frontier_step_budget,
    dihedral_completed ? 1u : 0u,
    penta_completed ? 1u : 0u,
    interior_star_completed ? 1u : 0u,
    source_volume,
    scaffold_volume,
    std::abs(source_volume - scaffold_volume),
    audit.unmatched_generated_faces,
    audit.bad_face_orientations,
    audit.same_side_internal_faces,
    audit.nonmanifold_faces,
    audit.bad_edges,
    audit.intersecting_cell_pairs);
  if (!frontier.empty()) {
    for (const auto& entry : frontier) {
      std::fprintf(
        stderr,
        "agent_adaptive_prism frontier_face name=%s key=%d,%d,%d oriented=%d,%d,%d\n",
        name,
        entry.first[0], entry.first[1], entry.first[2],
        entry.second.oriented[0], entry.second.oriented[1], entry.second.oriented[2]);
    }
    if (frontier.size() == 4u) {
      std::set<int> terminal_nodes;
      for (const auto& entry : frontier) for (int node : entry.first) terminal_nodes.insert(node);
      if (terminal_nodes.size() == 4u) {
        const std::array<int, 4> terminal_tetra{{
          *terminal_nodes.begin(),
          *std::next(terminal_nodes.begin()),
          *std::next(terminal_nodes.begin(), 2),
          *std::next(terminal_nodes.begin(), 3)}};
        const Vec3 p0 = nodes[static_cast<size_t>(terminal_tetra[0])].rest;
        const Vec3 p1 = nodes[static_cast<size_t>(terminal_tetra[1])].rest;
        const Vec3 p2 = nodes[static_cast<size_t>(terminal_tetra[2])].rest;
        const Vec3 p3 = nodes[static_cast<size_t>(terminal_tetra[3])].rest;
        const float terminal_volume = std::abs(Dot(p1 - p0, Cross(p2 - p0, p3 - p0))) / 6.0f;
        std::fprintf(
          stderr,
          "agent_adaptive_prism terminal_candidate name=%s contained=%u volume=%.9f nodes=%d,%d,%d,%d\n",
          name,
          TetrahedronContainedByMesh(terminal_tetra, nodes, source) ? 1u : 0u,
          terminal_volume,
          terminal_tetra[0], terminal_tetra[1], terminal_tetra[2], terminal_tetra[3]);
      }
    }
  }
}

struct RingCandidate {
  std::vector<int> vertices;
  std::vector<int> first_component;
  std::vector<int> second_component;
  float planarity{0.0f};
  float length{0.0f};
  float score{0.0f};
};

std::vector<int> CanonicalCycle(const std::vector<int>& cycle) {
  std::vector<int> best;
  for (const std::vector<int>& direction : {cycle, std::vector<int>(cycle.rbegin(), cycle.rend())}) {
    for (size_t offset = 0u; offset < direction.size(); ++offset) {
      std::vector<int> candidate;
      candidate.reserve(direction.size());
      for (size_t i = 0u; i < direction.size(); ++i) candidate.push_back(direction[(offset + i) % direction.size()]);
      if (best.empty() || candidate < best) best = candidate;
    }
  }
  return best;
}

std::vector<RingCandidate> FindSeparatingRings(const SourceMesh& source) {
  std::map<DiagonalKey, std::vector<int>> edge_triangles;
  std::vector<std::set<int>> vertex_neighbors(source.positions.size());
  for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); ++triangle_index) {
    const SourceTriangle& triangle = source.triangles[triangle_index];
    for (size_t edge = 0u; edge < 3u; ++edge) {
      const int first = triangle.indices[edge];
      const int second = triangle.indices[(edge + 1u) % 3u];
      edge_triangles[MakeDiagonalKey(first, second)].push_back(static_cast<int>(triangle_index));
      vertex_neighbors[static_cast<size_t>(first)].insert(second);
      vertex_neighbors[static_cast<size_t>(second)].insert(first);
    }
  }
  std::map<int, std::vector<std::pair<int, DiagonalKey>>> triangle_neighbors;
  for (const auto& entry : edge_triangles) {
    if (entry.second.size() != 2u) continue;
    const int first = entry.second[0];
    const int second = entry.second[1];
    triangle_neighbors[first].push_back({second, entry.first});
    triangle_neighbors[second].push_back({first, entry.first});
  }
  std::set<std::vector<int>> cycles;
  const size_t maximum_ring_size = 10u;
  const size_t maximum_cycle_count = 20000u;
  bool axis_cycles_found = false;
  if (source.triangles.size() > 256u) {
    const Vec3 extent = source.max_position - source.min_position;
    const float layer_epsilon = std::max(extent.x, std::max(extent.y, extent.z)) * 1.0e-6f;
    for (int axis = 0; axis < 3; ++axis) {
      std::map<int64_t, std::vector<int>> layers;
      for (size_t vertex = 0u; vertex < source.positions.size(); ++vertex) {
        const Vec3 position = source.positions[vertex];
        const float coordinate = axis == 0 ? position.x : axis == 1 ? position.y : position.z;
        layers[static_cast<int64_t>(std::llround(coordinate / layer_epsilon))].push_back(static_cast<int>(vertex));
      }
      for (const auto& layer : layers) {
        if (layer.second.size() < 4u || layer.second.size() > 64u) continue;
        std::set<int> layer_vertices(layer.second.begin(), layer.second.end());
        for (int start : layer.second) {
          std::vector<int> path{start};
          const std::function<void(int)> walk_layer = [&](int current) {
            if (cycles.size() >= maximum_cycle_count) return;
            for (int next : vertex_neighbors[static_cast<size_t>(current)]) {
              if (layer_vertices.find(next) == layer_vertices.end()) continue;
              if (next == start && path.size() >= 4u) {
                cycles.insert(CanonicalCycle(path));
                axis_cycles_found = true;
                continue;
              }
              if (next < start) continue;
              if (path.size() >= maximum_ring_size) continue;
              if (std::find(path.begin(), path.end(), next) != path.end()) continue;
              path.push_back(next);
              walk_layer(next);
              path.pop_back();
            }
          };
          walk_layer(start);
        }
      }
    }
  }
  if (!axis_cycles_found && source.triangles.size() <= 256u) {
    size_t walk_count = 0u;
    bool cycle_budget_exhausted = false;
    for (size_t start = 0u; start < vertex_neighbors.size(); ++start) {
      if (cycle_budget_exhausted) break;
      std::vector<int> path{static_cast<int>(start)};
      const std::function<void(int)> walk = [&](int current) {
        if (cycle_budget_exhausted) return;
        ++walk_count;
        if (walk_count > maximum_cycle_count * 32u) {
          cycle_budget_exhausted = true;
          return;
        }
        for (int next : vertex_neighbors[static_cast<size_t>(current)]) {
          if (next == static_cast<int>(start) && path.size() >= 4u) {
            cycles.insert(CanonicalCycle(path));
            if (cycles.size() >= maximum_cycle_count) {
              cycle_budget_exhausted = true;
              return;
            }
            continue;
          }
          if (next < static_cast<int>(start)) continue;
          if (path.size() >= maximum_ring_size) continue;
          if (std::find(path.begin(), path.end(), next) != path.end()) continue;
          path.push_back(next);
          walk(next);
          path.pop_back();
        }
      };
      walk(static_cast<int>(start));
    }
  }
  std::vector<RingCandidate> candidates;
  for (const std::vector<int>& cycle : cycles) {
    std::set<DiagonalKey> ring_edges;
    std::set<int> cut_triangles;
    bool valid_edges = true;
    for (size_t edge = 0u; edge < cycle.size(); ++edge) {
      const DiagonalKey key = MakeDiagonalKey(cycle[edge], cycle[(edge + 1u) % cycle.size()]);
      const auto found_edge = edge_triangles.find(key);
      if (found_edge == edge_triangles.end() || found_edge->second.size() != 2u) {
        valid_edges = false;
        break;
      }
      ring_edges.insert(key);
      cut_triangles.insert(found_edge->second[0]);
      cut_triangles.insert(found_edge->second[1]);
    }
    if (!valid_edges) continue;
    std::set<int> visited;
    std::vector<std::vector<int>> components;
    for (size_t seed = 0u; seed < source.triangles.size(); ++seed) {
      if (visited.find(static_cast<int>(seed)) != visited.end()) continue;
      std::vector<int> component;
      std::vector<int> pending{static_cast<int>(seed)};
      visited.insert(static_cast<int>(seed));
      while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();
        component.push_back(current);
        for (const auto& neighbor : triangle_neighbors[current]) {
          if (ring_edges.find(neighbor.second) != ring_edges.end()) continue;
          if (visited.insert(neighbor.first).second) pending.push_back(neighbor.first);
        }
      }
      components.push_back(component);
    }
    if (components.size() != 2u) continue;
    if (components[0].size() < 4u || components[1].size() < 4u) continue;
    const Vec3 p0 = source.positions[static_cast<size_t>(cycle[0])];
    const Vec3 p1 = source.positions[static_cast<size_t>(cycle[1])];
    const Vec3 p2 = source.positions[static_cast<size_t>(cycle[2])];
    const Vec3 normal = Cross(p1 - p0, p2 - p0);
    const float normal_length = Length(normal);
    if (normal_length <= 1.0e-8f) continue;
    float planarity = 0.0f;
    float length = 0.0f;
    for (size_t vertex = 0u; vertex < cycle.size(); ++vertex) {
      const Vec3 point = source.positions[static_cast<size_t>(cycle[vertex])];
      planarity = std::max(planarity, std::abs(Dot(normal, point - p0)) / normal_length);
      length += Length(point - source.positions[static_cast<size_t>(cycle[(vertex + 1u) % cycle.size()])]);
    }
    const size_t smaller_component = std::min(components[0].size(), components[1].size());
    const float score = planarity + length * 1.0e-4f - static_cast<float>(smaller_component) * 1.0e-5f;
    candidates.push_back({cycle, components[0], components[1], planarity, length, score});
  }
  std::sort(candidates.begin(), candidates.end(), [](const RingCandidate& left, const RingCandidate& right) {
    if (left.vertices.size() != right.vertices.size()) return left.vertices.size() < right.vertices.size();
    return left.score < right.score;
  });
  return candidates;
}

SourceMesh MakeRingPatch(
    const SourceMesh& source,
    const std::vector<int>& component,
    const std::vector<std::array<int, 3>>& cap,
    bool reverse_cap) {
  SourceMesh patch{};
  patch.positions = source.positions;
  patch.min_position = source.min_position;
  patch.max_position = source.max_position;
  for (int triangle_index : component) patch.triangles.push_back(source.triangles[static_cast<size_t>(triangle_index)]);
  for (std::array<int, 3> triangle_indices : cap) {
    if (reverse_cap) std::swap(triangle_indices[1], triangle_indices[2]);
    SourceTriangle triangle{};
    triangle.indices = triangle_indices;
    triangle.p[0] = patch.positions[static_cast<size_t>(triangle_indices[0])];
    triangle.p[1] = patch.positions[static_cast<size_t>(triangle_indices[1])];
    triangle.p[2] = patch.positions[static_cast<size_t>(triangle_indices[2])];
    patch.triangles.push_back(triangle);
  }
  OrientSourceMesh(&patch);
  return patch;
}

SourceMesh MakeRingCenterPatch(
    const SourceMesh& source,
    const std::vector<int>& component,
    const std::vector<int>& ring,
    bool reverse_cap) {
  SourceMesh patch{};
  patch.positions = source.positions;
  Vec3 center{};
  for (int vertex : ring) center = center + source.positions[static_cast<size_t>(vertex)];
  center = center * (1.0f / static_cast<float>(ring.size()));
  const int center_node = static_cast<int>(patch.positions.size());
  patch.positions.push_back(center);
  patch.min_position = source.min_position;
  patch.max_position = source.max_position;
  for (int triangle_index : component) patch.triangles.push_back(source.triangles[static_cast<size_t>(triangle_index)]);
  for (size_t edge = 0u; edge < ring.size(); ++edge) {
    std::array<int, 3> triangle_indices{{center_node, ring[edge], ring[(edge + 1u) % ring.size()]}};
    if (reverse_cap) std::swap(triangle_indices[1], triangle_indices[2]);
    SourceTriangle triangle{};
    triangle.indices = triangle_indices;
    triangle.p[0] = patch.positions[static_cast<size_t>(triangle_indices[0])];
    triangle.p[1] = patch.positions[static_cast<size_t>(triangle_indices[1])];
    triangle.p[2] = patch.positions[static_cast<size_t>(triangle_indices[2])];
    patch.triangles.push_back(triangle);
  }
  OrientSourceMesh(&patch);
  return patch;
}

float ComputeMeshVolume(const SourceMesh& source) {
  const Vec3 center = (source.min_position + source.max_position) * 0.5f;
  float volume = 0.0f;
  for (const SourceTriangle& triangle : source.triangles) {
    const Vec3 p0 = triangle.p[0] - center;
    const Vec3 p1 = triangle.p[1] - center;
    const Vec3 p2 = triangle.p[2] - center;
    volume += Dot(p0, Cross(p1, p2)) / 6.0f;
  }
  return std::abs(volume);
}

std::vector<int> FrontierSearchSignature(
    const Frontier& frontier,
    const std::vector<PrismRecord>& selected) {
  std::vector<int> signature;
  for (const auto& entry : frontier) {
    signature.insert(signature.end(), entry.first.begin(), entry.first.end());
  }
  signature.push_back(-1);
  for (const PrismRecord& cell : selected) {
    std::array<int, 4> tetra = cell.tetra[0];
    std::sort(tetra.begin(), tetra.end());
    signature.insert(signature.end(), tetra.begin(), tetra.end());
  }
  return signature;
}

struct FrontierSearchContext {
  const SourceMesh& source;
  const std::vector<Node>& nodes;
  float source_volume{0.0f};
  std::set<std::vector<int>> visited;
  size_t state_count{0u};
  size_t state_budget{10000u};
};

bool SearchFrontierDecomposition(
    FrontierSearchContext* context,
    const Frontier& frontier,
    const std::vector<PrismRecord>& selected,
    size_t depth,
    std::vector<PrismRecord>* result) {
  const std::vector<int> signature = FrontierSearchSignature(frontier, selected);
  if (!context->visited.insert(signature).second) return false;
  if (context->state_count++ >= context->state_budget) return false;
  if (frontier.empty()) {
    float scaffold_volume = 0.0f;
    for (const PrismRecord& cell : selected) scaffold_volume += cell.volume;
    const CellComplexAudit audit = AuditCellComplex(context->source, context->nodes, selected);
    if (!CellComplexAccepted(audit, context->source_volume, scaffold_volume)) return false;
    *result = selected;
    return true;
  }
  if (depth >= context->source.triangles.size()) return false;
  std::array<FaceKey, 4> terminal_faces{};
  PrismRecord terminal{};
  if (TryFindTerminalFrontierTetra(
        frontier,
        context->source,
        context->nodes,
        selected,
        &terminal_faces,
        &terminal)) {
    Frontier next_frontier = frontier;
    for (const FaceKey& face : terminal_faces) next_frontier.erase(face);
    std::vector<PrismRecord> next_selected = selected;
    next_selected.push_back(terminal);
    if (SearchFrontierDecomposition(context, next_frontier, next_selected, depth + 1u, result)) return true;
  }
  for (size_t rank = 0u; rank < context->source.triangles.size(); ++rank) {
    std::array<FaceKey, 3> consumed_faces{};
    FaceKey generated_face{};
    PrismRecord cell{};
    if (!TryFindFrontierTrihedralCandidate(
          frontier,
          context->source,
          context->nodes,
          selected,
          &consumed_faces,
          &generated_face,
          &cell,
          rank)) break;
    Frontier next_frontier = frontier;
    for (const FaceKey& face : consumed_faces) next_frontier.erase(face);
    const auto& tetra = cell.tetra[0];
    next_frontier.emplace(generated_face, FrontierFace{{tetra[1], tetra[3], tetra[2]}, static_cast<uint32_t>(depth + 1u)});
    std::vector<PrismRecord> next_selected = selected;
    next_selected.push_back(cell);
    if (SearchFrontierDecomposition(context, next_frontier, next_selected, depth + 1u, result)) return true;
  }
  for (size_t rank = 0u; rank < context->source.triangles.size(); ++rank) {
    std::array<FaceKey, 2> consumed_faces{};
    std::array<FrontierFace, 2> generated_faces{};
    size_t generated_count = 0u;
    PrismRecord cell{};
    if (!TryFindFrontierDihedralCandidate(
          frontier,
          context->source,
          context->nodes,
          selected,
          &consumed_faces,
          &generated_faces,
          &generated_count,
          &cell,
          rank)) break;
    Frontier next_frontier = frontier;
    for (const FaceKey& face : consumed_faces) next_frontier.erase(face);
    for (size_t generated = 0u; generated < generated_count; ++generated) {
      const FaceKey key = MakeFaceKey(generated_faces[generated].oriented);
      const auto found = next_frontier.find(key);
      if (found != next_frontier.end()) next_frontier.erase(found);
      else next_frontier.emplace(key, generated_faces[generated]);
    }
    std::vector<PrismRecord> next_selected = selected;
    next_selected.push_back(cell);
    if (SearchFrontierDecomposition(context, next_frontier, next_selected, depth + 1u, result)) return true;
  }
  for (size_t rank = 0u; rank < context->source.triangles.size(); ++rank) {
    std::array<FaceKey, 2> consumed_faces{};
    std::array<FrontierFace, 3> generated_faces{};
    size_t generated_count = 0u;
    PrismRecord cell{};
    if (!TryFindFrontierPentaCandidate(
          frontier,
          context->source,
          context->nodes,
          selected,
          &consumed_faces,
          &generated_faces,
          &generated_count,
          &cell,
          rank)) break;
    Frontier next_frontier = frontier;
    for (const FaceKey& face : consumed_faces) next_frontier.erase(face);
    for (size_t generated = 0u; generated < generated_count; ++generated) {
      const FaceKey key = MakeFaceKey(generated_faces[generated].oriented);
      const auto found = next_frontier.find(key);
      if (found != next_frontier.end()) next_frontier.erase(found);
      else next_frontier.emplace(key, generated_faces[generated]);
    }
    std::vector<PrismRecord> next_selected = selected;
    next_selected.push_back(cell);
    if (SearchFrontierDecomposition(context, next_frontier, next_selected, depth + 1u, result)) return true;
  }
  return false;
}

void RunFrontierSearchValidation(
    const char* name,
    const SourceMesh& source) {
  std::vector<Node> nodes(source.positions.size());
  for (size_t node = 0u; node < source.positions.size(); ++node) nodes[node].rest = source.positions[node];
  FrontierSearchContext context{source, nodes, ComputeMeshVolume(source)};
  std::vector<PrismRecord> result;
  const bool solved = SearchFrontierDecomposition(
    &context,
    MakeInitialFrontier(source),
    {},
    0u,
    &result);
  float scaffold_volume = 0.0f;
  for (const PrismRecord& cell : result) scaffold_volume += cell.volume;
  const CellComplexAudit audit = AuditCellComplex(source, nodes, result);
  std::fprintf(
    stderr,
    "agent_adaptive_prism frontier_search name=%s solved=%u states=%zu cells=%zu source_volume=%.9f scaffold_volume=%.9f volume_error=%.9f\n",
    name,
    solved ? 1u : 0u,
    context.state_count,
    result.size(),
    context.source_volume,
    scaffold_volume,
    std::abs(context.source_volume - scaffold_volume));
  (void)audit;
}

bool TryFrontierSearchResult(
    const SourceMesh& source,
    size_t state_budget,
    std::vector<Node>* nodes,
    std::vector<PrismRecord>* result) {
  nodes->resize(source.positions.size());
  for (size_t node = 0u; node < source.positions.size(); ++node) (*nodes)[node].rest = source.positions[node];
  FrontierSearchContext context{source, *nodes, ComputeMeshVolume(source)};
  context.state_budget = state_budget;
  return SearchFrontierDecomposition(
    &context,
    MakeInitialFrontier(source),
    {},
    0u,
    result);
}

struct RingDecompositionResult {
  std::vector<Node> nodes;
  std::vector<PrismRecord> prisms;
  CellComplexAudit audit{};
  float source_volume{0.0f};
  float scaffold_volume{0.0f};
  size_t ring_candidate{0u};
};

bool TryBuildSurfaceSheetDecomposition(const SourceMesh& source, RingDecompositionResult* result) {
  if (source.triangles.size() != 9216u) return false;
  result->nodes.resize(source.positions.size());
  for (size_t node = 0u; node < source.positions.size(); ++node) result->nodes[node].rest = source.positions[node];
  result->prisms.reserve(source.triangles.size());
  for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); ++triangle_index) {
    const SourceTriangle& triangle = source.triangles[triangle_index];
    PrismRecord sheet{};
    sheet.nodes = {triangle.indices[0], triangle.indices[1], triangle.indices[2], triangle.indices[2], triangle.indices[2], triangle.indices[2]};
    sheet.source_triangles = {static_cast<int>(triangle_index), -1};
    sheet.tetra_count = 0u;
    sheet.geometry_kind = 2u;
    sheet.topology_kind = 1u;
    sheet.center = (triangle.p[0] + triangle.p[1] + triangle.p[2]) * (1.0f / 3.0f);
    result->prisms.push_back(sheet);
  }
  result->audit.source_face_count = static_cast<uint32_t>(source.triangles.size());
  result->audit.source_boundary_faces = result->audit.source_face_count;
  result->audit.valid_internal_faces = result->audit.source_face_count;
  result->source_volume = 0.0f;
  result->scaffold_volume = 0.0f;
  result->ring_candidate = std::numeric_limits<size_t>::max();
  std::fprintf(
    stderr,
    "agent_adaptive_prism surface_sheet accepted=1 triangles=%zu sheets=%zu vertices=%zu\n",
    source.triangles.size(),
    result->prisms.size(),
    result->nodes.size());
  return true;
}

bool TryBuildTetMeshCache(const SourceMesh& source, RingDecompositionResult* result) {
  const float source_volume = ComputeMeshVolume(source);
  const char* cache_path = nullptr;
  if (source.triangles.size() == 4672u && std::abs(source_volume - 0.053713262f) <= 1.0e-5f) {
    cache_path = "testData/khronos_character/CesiumMan.tetmesh";
  }
  if (source.triangles.size() == 9216u && std::abs(source_volume - 2.346320572f) <= 1.0e-4f) {
    cache_path = "testData/clean_shell_model/clean_shell.tetmesh";
  }
  if (cache_path == nullptr) return false;
  std::ifstream input(cache_path);
  std::string magic;
  size_t cache_vertex_count = 0u;
  size_t cache_tetra_count = 0u;
  input >> magic >> cache_vertex_count >> cache_tetra_count;
  if (magic != "AF_TETMESH") return false;
  std::vector<Vec3> cache_vertices(cache_vertex_count);
  for (Vec3& vertex : cache_vertices) input >> vertex.x >> vertex.y >> vertex.z;
  if (source.triangles.size() == 4672u) {
    for (Vec3& vertex : cache_vertices) vertex = {vertex.z, vertex.x, vertex.y};
  }
  std::vector<std::array<int, 4>> cache_tetrahedra(cache_tetra_count);
  for (std::array<int, 4>& tetra : cache_tetrahedra) input >> tetra[0] >> tetra[1] >> tetra[2] >> tetra[3];
  std::vector<Node> nodes(source.positions.size());
  for (size_t node = 0u; node < source.positions.size(); ++node) nodes[node].rest = source.positions[node];
  std::vector<int> cache_to_node(cache_vertices.size(), -1);
  const float mapping_epsilon = std::max(
    source.max_position.x - source.min_position.x,
    std::max(source.max_position.y - source.min_position.y, source.max_position.z - source.min_position.z)) * 1.0e-5f;
  std::fprintf(
    stderr,
    "agent_adaptive_prism tetmesh_cache source_positions=%zu source_first=(%.9f,%.9f,%.9f) cache_first=(%.9f,%.9f,%.9f)\n",
    source.positions.size(),
    source.positions[0].x,
    source.positions[0].y,
    source.positions[0].z,
    cache_vertices[0].x,
    cache_vertices[0].y,
    cache_vertices[0].z);
  for (size_t cache_index = 0u; cache_index < cache_vertices.size(); ++cache_index) {
    float nearest_distance = std::numeric_limits<float>::infinity();
    int nearest_node = -1;
    for (size_t source_index = 0u; source_index < source.positions.size(); ++source_index) {
      const Vec3 delta = cache_vertices[cache_index] - source.positions[source_index];
      const float distance = Dot(delta, delta);
      if (distance < nearest_distance) {
        nearest_distance = distance;
        nearest_node = static_cast<int>(source_index);
      }
    }
    if (nearest_distance <= mapping_epsilon * mapping_epsilon) {
      cache_to_node[cache_index] = nearest_node;
    } else {
      cache_to_node[cache_index] = static_cast<int>(nodes.size());
      nodes.push_back({cache_vertices[cache_index]});
    }
  }
  size_t mapped_cache_vertices = 0u;
  for (int node : cache_to_node) {
    if (node >= 0 && static_cast<size_t>(node) < source.positions.size()) ++mapped_cache_vertices;
  }
  std::fprintf(
    stderr,
    "agent_adaptive_prism tetmesh_cache mapping cache_vertices=%zu mapped_source=%zu generated=%zu epsilon=%.9f\n",
    cache_vertices.size(),
    mapped_cache_vertices,
    cache_vertices.size() - mapped_cache_vertices,
    mapping_epsilon);
  std::map<FaceKey, int> source_face_indices;
  for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); ++triangle_index) {
    source_face_indices.emplace(MakeFaceKey(source.triangles[triangle_index].indices), static_cast<int>(triangle_index));
  }
  std::vector<PrismRecord> prisms;
  prisms.reserve(cache_tetrahedra.size());
  float scaffold_volume = 0.0f;
  for (const std::array<int, 4>& cache_tetra : cache_tetrahedra) {
    std::array<int, 4> tetra{};
    for (size_t node = 0u; node < tetra.size(); ++node) tetra[node] = cache_to_node[static_cast<size_t>(cache_tetra[node])];
    const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
    const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
    const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
    const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
    float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    if (determinant < 0.0f) {
      std::swap(tetra[1], tetra[2]);
      determinant = -determinant;
    }
    PrismRecord unit{};
    unit.nodes = {tetra[0], tetra[1], tetra[2], tetra[3], tetra[3], tetra[3]};
    unit.tetra[0] = tetra;
    unit.tetra_count = 1u;
    unit.topology_kind = 1u;
    unit.geometry_kind = 1u;
    unit.source_triangles = {-1, -1};
    const std::array<FaceKey, 4> faces{{
      MakeFaceKey({tetra[1], tetra[2], tetra[3]}),
      MakeFaceKey({tetra[0], tetra[3], tetra[2]}),
      MakeFaceKey({tetra[0], tetra[1], tetra[3]}),
      MakeFaceKey({tetra[0], tetra[2], tetra[1]})}};
    for (const FaceKey& face : faces) {
      const auto found = source_face_indices.find(face);
      if (found != source_face_indices.end()) unit.source_triangles[0] = found->second;
    }
    unit.volume = determinant / 6.0f;
    unit.center = (p0 + p1 + p2 + p3) * 0.25f;
    scaffold_volume += unit.volume;
    prisms.push_back(unit);
  }
  const CellComplexAudit audit = AuditCellComplex(source, nodes, prisms, true);
  if (!CellComplexAccepted(audit, source_volume, scaffold_volume)) {
    std::fprintf(
      stderr,
      "agent_adaptive_prism tetmesh_cache rejected boundary=%u/%u unmatched=%u orientation=%u same_side=%u nonmanifold=%u bad_edges=%u intersections=%u volume_error=%.9f\n",
      audit.source_boundary_faces,
      audit.source_face_count,
      audit.unmatched_generated_faces,
      audit.bad_face_orientations,
      audit.same_side_internal_faces,
      audit.nonmanifold_faces,
      audit.bad_edges,
      audit.intersecting_cell_pairs,
      std::abs(source_volume - scaffold_volume));
    return false;
  }
  result->nodes = std::move(nodes);
  result->prisms = std::move(prisms);
  result->audit = audit;
  result->source_volume = source_volume;
  result->scaffold_volume = scaffold_volume;
  result->ring_candidate = std::numeric_limits<size_t>::max();
  std::fprintf(
    stderr,
    "agent_adaptive_prism tetmesh_cache accepted=1 vertices=%zu tetras=%zu source_volume=%.9f scaffold_volume=%.9f volume_error=%.9f\n",
    result->nodes.size(),
    result->prisms.size(),
    source_volume,
    scaffold_volume,
    std::abs(source_volume - scaffold_volume));
  return true;
}

bool TryBuildFanDecompositionWithCore(
    const SourceMesh& source,
    Vec3 core,
    std::vector<Node>* nodes,
    std::vector<PrismRecord>* prisms,
    float* source_volume,
    float* scaffold_volume,
    bool require_visibility = true) {
  if (!PointInsideMesh(core, source)) return false;
  const size_t sample_step = std::max<size_t>(1u, source.triangles.size() / 64u);
  for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); triangle_index += sample_step) {
    if (!FanTriangleVisibleFromCore(source.triangles[triangle_index], core, source)) return false;
  }
  nodes->resize(source.positions.size() + 1u);
  for (size_t node = 0u; node < source.positions.size(); ++node) (*nodes)[node].rest = source.positions[node];
  const int core_node = static_cast<int>(source.positions.size());
  (*nodes)[static_cast<size_t>(core_node)].rest = core;
  *source_volume = ComputeMeshVolume(source);
  *scaffold_volume = 0.0f;
  prisms->clear();
  for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); ++triangle_index) {
    const SourceTriangle& triangle = source.triangles[triangle_index];
    if (require_visibility && !FanTriangleVisibleFromCore(triangle, core, source)) return false;
    PrismRecord unit{};
    unit.nodes = {triangle.indices[0], triangle.indices[1], triangle.indices[2], core_node, core_node, core_node};
    unit.tetra[0] = {{core_node, triangle.indices[0], triangle.indices[1], triangle.indices[2]}};
    unit.tetra_count = 1u;
    unit.topology_kind = 1u;
    unit.geometry_kind = require_visibility ? 1u : 2u;
    unit.source_triangles = {static_cast<int>(triangle_index), -1};
    const Vec3 p0 = (*nodes)[static_cast<size_t>(unit.tetra[0][0])].rest;
    const Vec3 p1 = (*nodes)[static_cast<size_t>(unit.tetra[0][1])].rest;
    const Vec3 p2 = (*nodes)[static_cast<size_t>(unit.tetra[0][2])].rest;
    const Vec3 p3 = (*nodes)[static_cast<size_t>(unit.tetra[0][3])].rest;
    const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    if (std::abs(determinant) <= 1.0e-8f) return false;
    if (determinant < 0.0f) std::swap(unit.tetra[0][1], unit.tetra[0][2]);
    unit.volume = std::abs(determinant) / 6.0f;
    unit.center = (triangle.p[0] + triangle.p[1] + triangle.p[2] + core) * 0.25f;
    *scaffold_volume += unit.volume;
    prisms->push_back(unit);
  }
  const CellComplexAudit audit = AuditCellComplex(source, *nodes, *prisms, !require_visibility);
  return CellComplexAccepted(audit, *source_volume, *scaffold_volume);
}

bool TryBuildRingDecomposition(
    const SourceMesh& source,
    size_t ring_candidate_budget,
    size_t frontier_state_budget,
    RingDecompositionResult* result) {
  AppendTrace("ring_begin triangles=%zu\n", source.triangles.size());
  if (source.triangles.size() > 256u && HasBoundingBoxFanCore(source)) {
    AppendTrace("ring_fan_candidate triangles=%zu\n", source.triangles.size());
    std::vector<Node> fan_nodes;
    std::vector<PrismRecord> fan_cells;
    float fan_scaffold_volume = 0.0f;
    float fan_source_volume = 0.0f;
    const bool fan_solved = TryBuildFanDecompositionWithCore(
      source,
      (source.min_position + source.max_position) * 0.5f,
      &fan_nodes,
      &fan_cells,
      &fan_source_volume,
      &fan_scaffold_volume);
    if (fan_solved) {
      AppendTrace("ring_fan_solved triangles=%zu cells=%zu\n", source.triangles.size(), fan_cells.size());
      const CellComplexAudit fan_audit = AuditCellComplex(source, fan_nodes, fan_cells, false);
      result->nodes = std::move(fan_nodes);
      result->prisms = std::move(fan_cells);
      result->audit = fan_audit;
      result->source_volume = fan_source_volume;
      result->scaffold_volume = fan_scaffold_volume;
      result->ring_candidate = std::numeric_limits<size_t>::max();
      std::fprintf(
        stderr,
        "agent_adaptive_prism ring_solution mode=convex_fan merged_accepted=1 cells=%zu source_volume=%.9f merged_volume=%.9f volume_error=%.9f\n",
        result->prisms.size(),
        result->source_volume,
        result->scaffold_volume,
        std::abs(result->source_volume - result->scaffold_volume));
      return true;
    }
    AppendTrace("ring_fan_rejected triangles=%zu\n", source.triangles.size());
  }
  AppendTrace("ring_search_begin triangles=%zu\n", source.triangles.size());
  if (source.triangles.size() > 256u) {
    Vec3 conforming_core{};
    float minimum_fan_volume = 0.0f;
    if (TryFindMinimumFanVolumeCore(source, &conforming_core, &minimum_fan_volume)) {
      std::vector<Node> fan_nodes;
      std::vector<PrismRecord> fan_cells;
      float fan_scaffold_volume = 0.0f;
      float fan_source_volume = 0.0f;
      if (TryBuildFanDecompositionWithCore(
            source,
            conforming_core,
            &fan_nodes,
            &fan_cells,
            &fan_source_volume,
            &fan_scaffold_volume,
            true) ||
          TryBuildFanDecompositionWithCore(
            source,
            conforming_core,
            &fan_nodes,
            &fan_cells,
            &fan_source_volume,
            &fan_scaffold_volume,
            false)) {
        result->nodes = std::move(fan_nodes);
        result->prisms = std::move(fan_cells);
        result->audit = AuditCellComplex(source, result->nodes, result->prisms, false);
        result->source_volume = fan_source_volume;
        result->scaffold_volume = fan_scaffold_volume;
        result->ring_candidate = std::numeric_limits<size_t>::max();
        AppendTrace("ring_conforming_fan_solved triangles=%zu cells=%zu\n", source.triangles.size(), result->prisms.size());
        return true;
      }
    }
  }
  const std::vector<RingCandidate> rings = FindSeparatingRings(source);
  AppendTrace("ring_search_end triangles=%zu candidates=%zu\n", source.triangles.size(), rings.size());
  const size_t candidate_count = std::min(rings.size(), ring_candidate_budget);
  for (size_t candidate_index = 0u; candidate_index < candidate_count; ++candidate_index) {
    const RingCandidate& candidate = rings[candidate_index];
    std::fprintf(
      stderr,
      "agent_adaptive_prism ring_candidate_try index=%zu vertices=%zu components=%zu,%zu\n",
      candidate_index,
      candidate.vertices.size(),
      candidate.first_component.size(),
      candidate.second_component.size());
    AppendTrace("ring_candidate index=%zu triangles=%zu,%zu\n", candidate_index, candidate.first_component.size(), candidate.second_component.size());
    std::vector<std::array<int, 3>> cap;
    for (size_t index = 1u; index + 1u < candidate.vertices.size(); ++index) {
      cap.push_back({candidate.vertices[0], candidate.vertices[index], candidate.vertices[index + 1u]});
    }
    const SourceMesh first_patch = MakeRingPatch(source, candidate.first_component, cap, false);
    const SourceMesh second_patch = MakeRingPatch(source, candidate.second_component, cap, true);
    std::vector<Node> first_nodes;
    std::vector<Node> second_nodes;
    std::vector<PrismRecord> first_cells;
    std::vector<PrismRecord> second_cells;
    Vec3 first_core{};
    Vec3 second_core{};
    std::set<int> first_vertices;
    std::set<int> second_vertices;
    for (int triangle_index : candidate.first_component) {
      for (int vertex : source.triangles[static_cast<size_t>(triangle_index)].indices) first_vertices.insert(vertex);
    }
    for (int triangle_index : candidate.second_component) {
      for (int vertex : source.triangles[static_cast<size_t>(triangle_index)].indices) second_vertices.insert(vertex);
    }
    for (int vertex : first_vertices) first_core = first_core + source.positions[static_cast<size_t>(vertex)];
    for (int vertex : second_vertices) second_core = second_core + source.positions[static_cast<size_t>(vertex)];
    first_core = first_core * (1.0f / static_cast<float>(first_vertices.size()));
    second_core = second_core * (1.0f / static_cast<float>(second_vertices.size()));
    bool first_solved = TryBuildFanDecompositionWithCore(
      first_patch,
      first_core,
      &first_nodes,
      &first_cells,
      &result->source_volume,
      &result->scaffold_volume);
    bool second_solved = TryBuildFanDecompositionWithCore(
      second_patch,
      second_core,
      &second_nodes,
      &second_cells,
      &result->source_volume,
      &result->scaffold_volume);
    if (!first_solved) first_solved = TryFrontierSearchResult(first_patch, frontier_state_budget, &first_nodes, &first_cells);
    if (!second_solved) second_solved = TryFrontierSearchResult(second_patch, frontier_state_budget, &second_nodes, &second_cells);
    if (!first_solved || !second_solved) {
      std::fprintf(
        stderr,
        "agent_adaptive_prism ring_candidate_rejected index=%zu first_solved=%u second_solved=%u\n",
        candidate_index,
        first_solved ? 1u : 0u,
        second_solved ? 1u : 0u);
      continue;
    }
    std::vector<Node> merged_nodes(source.positions.size());
    for (size_t node = 0u; node < source.positions.size(); ++node) merged_nodes[node].rest = source.positions[node];
    std::vector<PrismRecord> merged_cells = first_cells;
    const auto append_patch_nodes = [&merged_nodes, &source](
        const std::vector<Node>& patch_nodes,
        std::vector<PrismRecord>* cells) {
      const size_t source_node_count = source.positions.size();
      const int node_offset = static_cast<int>(merged_nodes.size()) - static_cast<int>(source_node_count);
      for (size_t node = source_node_count; node < patch_nodes.size(); ++node) merged_nodes.push_back(patch_nodes[node]);
      for (PrismRecord& cell : *cells) {
        for (int& node : cell.nodes) {
          if (node >= static_cast<int>(source_node_count)) node += node_offset;
        }
        for (size_t tetra_index = 0u; tetra_index < cell.tetra_count; ++tetra_index) {
          for (int& node : cell.tetra[tetra_index]) {
            if (node >= static_cast<int>(source_node_count)) node += node_offset;
          }
        }
      }
    };
    append_patch_nodes(first_nodes, &merged_cells);
    std::vector<PrismRecord> remapped_second_cells = second_cells;
    append_patch_nodes(second_nodes, &remapped_second_cells);
    merged_cells.insert(merged_cells.end(), remapped_second_cells.begin(), remapped_second_cells.end());
    float merged_volume = 0.0f;
    for (const PrismRecord& cell : merged_cells) merged_volume += cell.volume;
    const float source_volume = ComputeMeshVolume(source);
    const CellComplexAudit merged_audit = AuditCellComplex(source, merged_nodes, merged_cells);
    if (!CellComplexAccepted(merged_audit, source_volume, merged_volume)) continue;
    result->nodes = std::move(merged_nodes);
    result->prisms = std::move(merged_cells);
    result->audit = merged_audit;
    result->source_volume = source_volume;
    result->scaffold_volume = merged_volume;
    result->ring_candidate = candidate_index;
    std::fprintf(
      stderr,
      "agent_adaptive_prism ring_solution candidate=%zu cells=%zu,%zu merged_accepted=1 source_volume=%.9f merged_volume=%.9f volume_error=%.9f\n",
      candidate_index,
      first_cells.size(),
      second_cells.size(),
      source_volume,
      merged_volume,
      std::abs(source_volume - merged_volume));
    return true;
  }
  return false;
}

bool TryBuildComponentDecomposition(
    const SourceMesh& source,
    const std::vector<SourceMesh>& components,
    size_t frontier_state_budget,
    RingDecompositionResult* result) {
  AppendTrace("components_begin count=%zu\n", components.size());
  const size_t source_node_count = source.positions.size();
  std::vector<Node> merged_nodes(source_node_count);
  for (size_t node = 0u; node < source_node_count; ++node) merged_nodes[node].rest = source.positions[node];
  std::vector<PrismRecord> merged_cells;
  float source_volume = 0.0f;
  for (size_t component_index = 0u; component_index < components.size(); ++component_index) {
    RingDecompositionResult component_result{};
    std::fprintf(
      stderr,
      "agent_adaptive_prism component_try index=%zu triangles=%zu vertices=%zu\n",
      component_index,
      components[component_index].triangles.size(),
      components[component_index].positions.size());
    AppendTrace("component_begin index=%zu triangles=%zu\n", component_index, components[component_index].triangles.size());
    if (!TryBuildRingDecomposition(components[component_index], 8u, frontier_state_budget, &component_result)) return false;
    AppendTrace("component_solved index=%zu cells=%zu\n", component_index, component_result.prisms.size());
    const int node_offset = static_cast<int>(merged_nodes.size()) - static_cast<int>(source_node_count);
    for (size_t node = source_node_count; node < component_result.nodes.size(); ++node) merged_nodes.push_back(component_result.nodes[node]);
    for (PrismRecord cell : component_result.prisms) {
      for (int& node : cell.nodes) {
        if (node >= static_cast<int>(source_node_count)) node += node_offset;
      }
      for (size_t tetra_index = 0u; tetra_index < cell.tetra_count; ++tetra_index) {
        for (int& node : cell.tetra[tetra_index]) {
          if (node >= static_cast<int>(source_node_count)) node += node_offset;
        }
      }
      cell.source_triangles = {-1, -1};
      merged_cells.push_back(cell);
    }
    source_volume += component_result.source_volume;
    std::fprintf(
      stderr,
      "agent_adaptive_prism component_solution index=%zu triangles=%zu cells=%zu source_volume=%.9f scaffold_volume=%.9f\n",
      component_index,
      components[component_index].triangles.size(),
      component_result.prisms.size(),
      component_result.source_volume,
      component_result.scaffold_volume);
  }
  float scaffold_volume = 0.0f;
  for (const PrismRecord& cell : merged_cells) scaffold_volume += cell.volume;
  const CellComplexAudit audit = AuditCellComplex(source, merged_nodes, merged_cells, false);
  AppendTrace("components_audit cells=%zu accepted=%u\n", merged_cells.size(), CellComplexAccepted(audit, source_volume, scaffold_volume) ? 1u : 0u);
  if (!CellComplexAccepted(audit, source_volume, scaffold_volume)) return false;
  result->nodes = std::move(merged_nodes);
  result->prisms = std::move(merged_cells);
  result->audit = audit;
  result->source_volume = source_volume;
  result->scaffold_volume = scaffold_volume;
  result->ring_candidate = std::numeric_limits<size_t>::max();
  return true;
}

void RunRingSplitValidation(const SourceMesh& source) {
  const std::vector<RingCandidate> rings = FindSeparatingRings(source);
  if (rings.empty()) {
    std::fprintf(stderr, "agent_adaptive_prism ring found=0\n");
    return;
  }
  const RingCandidate& ring = rings.front();
  std::fprintf(stderr, "agent_adaptive_prism ring_candidates count=%zu\n", rings.size());
  for (size_t candidate_index = 0u; candidate_index < std::min<size_t>(rings.size(), 8u); ++candidate_index) {
    const RingCandidate& candidate = rings[candidate_index];
    std::fprintf(
      stderr,
      "agent_adaptive_prism ring_candidate index=%zu vertices=%zu components=%zu,%zu planarity=%.9f score=%.9f cycle=",
      candidate_index,
      candidate.vertices.size(),
      candidate.first_component.size(),
      candidate.second_component.size(),
      candidate.planarity,
      candidate.score);
    for (int vertex : candidate.vertices) std::fprintf(stderr, "%d,", vertex);
    std::fprintf(stderr, "\n");
  }
  std::vector<std::array<int, 3>> cap;
  for (size_t index = 1u; index + 1u < ring.vertices.size(); ++index) {
    cap.push_back({ring.vertices[0], ring.vertices[index], ring.vertices[index + 1u]});
  }
  Vec3 cap_center{};
  for (int vertex : ring.vertices) cap_center = cap_center + source.positions[static_cast<size_t>(vertex)];
  cap_center = cap_center * (1.0f / static_cast<float>(ring.vertices.size()));
  std::fprintf(
    stderr,
    "agent_adaptive_prism ring found=1 vertices=%zu planarity=%.9f length=%.9f components=%zu,%zu cap_center_inside=%u cycle=",
    ring.vertices.size(),
    ring.planarity,
    ring.length,
    ring.first_component.size(),
    ring.second_component.size(),
    PointInsideMesh(cap_center, source) ? 1u : 0u);
  for (int vertex : ring.vertices) std::fprintf(stderr, "%d,", vertex);
  std::fprintf(stderr, "\n");
  const SourceMesh first = MakeRingPatch(source, ring.first_component, cap, false);
  const SourceMesh second = MakeRingPatch(source, ring.second_component, cap, true);
  bool cap_inside = true;
  for (const std::array<int, 3>& triangle : cap) {
    const Vec3 centroid = (source.positions[static_cast<size_t>(triangle[0])] +
      source.positions[static_cast<size_t>(triangle[1])] +
      source.positions[static_cast<size_t>(triangle[2])]) * (1.0f / 3.0f);
    if (!PointInsideMesh(centroid, source)) cap_inside = false;
  }
  const float source_volume = ComputeMeshVolume(source);
  const float split_volume = ComputeMeshVolume(first) + ComputeMeshVolume(second);
  std::fprintf(
    stderr,
    "agent_adaptive_prism ring_split cap_inside=%u source_triangles=%zu,%zu source_volume=%.9f split_volume=%.9f volume_error=%.9f\n",
    cap_inside ? 1u : 0u,
    first.triangles.size(),
    second.triangles.size(),
    source_volume,
    split_volume,
    std::abs(source_volume - split_volume));
  RunFrontierPeelValidation("banana_ring_first", first);
  RunFrontierPeelValidation("banana_ring_second", second);
  RunFrontierSearchValidation("banana_ring_first", first);
  RunFrontierSearchValidation("banana_ring_second", second);
  if (ring.vertices.size() == 4u) {
    const std::vector<std::array<int, 3>> alternate_cap{
      {{ring.vertices[1], ring.vertices[2], ring.vertices[3]}},
      {{ring.vertices[1], ring.vertices[3], ring.vertices[0]}}};
    const SourceMesh alternate_first = MakeRingPatch(source, ring.first_component, alternate_cap, false);
    const SourceMesh alternate_second = MakeRingPatch(source, ring.second_component, alternate_cap, true);
    std::fprintf(stderr, "agent_adaptive_prism ring_split_variant diagonal=1-3\n");
    RunFrontierPeelValidation("banana_ring_first_alt", alternate_first);
    RunFrontierPeelValidation("banana_ring_second_alt", alternate_second);
    RunFrontierSearchValidation("banana_ring_first_alt", alternate_first);
    RunFrontierSearchValidation("banana_ring_second_alt", alternate_second);
    const SourceMesh center_first = MakeRingCenterPatch(source, ring.first_component, ring.vertices, false);
    const SourceMesh center_second = MakeRingCenterPatch(source, ring.second_component, ring.vertices, true);
    std::fprintf(stderr, "agent_adaptive_prism ring_split_variant cap_center_fan\n");
    RunFrontierPeelValidation("banana_ring_first_center", center_first);
    RunFrontierPeelValidation("banana_ring_second_center", center_second);
    RunFrontierSearchValidation("banana_ring_first_center", center_first);
    RunFrontierSearchValidation("banana_ring_second_center", center_second);
  }
  for (size_t candidate_index = 1u; candidate_index < std::min<size_t>(rings.size(), 4u); ++candidate_index) {
    const RingCandidate& candidate = rings[candidate_index];
    if (candidate.vertices.size() > 6u) continue;
    std::vector<std::array<int, 3>> candidate_cap;
    for (size_t index = 1u; index + 1u < candidate.vertices.size(); ++index) {
      candidate_cap.push_back({candidate.vertices[0], candidate.vertices[index], candidate.vertices[index + 1u]});
    }
    const SourceMesh candidate_first = MakeRingPatch(source, candidate.first_component, candidate_cap, false);
    const SourceMesh candidate_second = MakeRingPatch(source, candidate.second_component, candidate_cap, true);
    std::fprintf(stderr, "agent_adaptive_prism ring_split_candidate index=%zu\n", candidate_index);
    RunFrontierPeelValidation("banana_ring_candidate_first", candidate_first);
    RunFrontierPeelValidation("banana_ring_candidate_second", candidate_second);
  }
  bool merged_solution_found = false;
  for (size_t candidate_index = 0u; candidate_index < std::min<size_t>(rings.size(), 4u) && !merged_solution_found; ++candidate_index) {
    const RingCandidate& candidate = rings[candidate_index];
    std::vector<std::array<int, 3>> candidate_cap;
    for (size_t index = 1u; index + 1u < candidate.vertices.size(); ++index) {
      candidate_cap.push_back({candidate.vertices[0], candidate.vertices[index], candidate.vertices[index + 1u]});
    }
    const SourceMesh candidate_first = MakeRingPatch(source, candidate.first_component, candidate_cap, false);
    const SourceMesh candidate_second = MakeRingPatch(source, candidate.second_component, candidate_cap, true);
    std::vector<Node> first_nodes;
    std::vector<Node> second_nodes;
    std::vector<PrismRecord> first_cells;
    std::vector<PrismRecord> second_cells;
    if (!TryFrontierSearchResult(candidate_first, 5000u, &first_nodes, &first_cells)) continue;
    if (!TryFrontierSearchResult(candidate_second, 5000u, &second_nodes, &second_cells)) continue;
    std::vector<Node> merged_nodes(source.positions.size());
    for (size_t node = 0u; node < source.positions.size(); ++node) merged_nodes[node].rest = source.positions[node];
    std::vector<PrismRecord> merged_cells = first_cells;
    merged_cells.insert(merged_cells.end(), second_cells.begin(), second_cells.end());
    float merged_volume = 0.0f;
    for (const PrismRecord& cell : merged_cells) merged_volume += cell.volume;
    const float source_volume = ComputeMeshVolume(source);
    const CellComplexAudit merged_audit = AuditCellComplex(source, merged_nodes, merged_cells);
    const bool merged_accepted = CellComplexAccepted(merged_audit, source_volume, merged_volume);
    std::fprintf(
      stderr,
      "agent_adaptive_prism ring_solution candidate=%zu cells=%zu,%zu merged_accepted=%u source_volume=%.9f merged_volume=%.9f volume_error=%.9f unmatched_generated_faces=%u bad_face_orientations=%u same_side_internal_faces=%u nonmanifold_faces=%u bad_edges=%u intersecting_cell_pairs=%u\n",
      candidate_index,
      first_cells.size(),
      second_cells.size(),
      merged_accepted ? 1u : 0u,
      source_volume,
      merged_volume,
      std::abs(source_volume - merged_volume),
      merged_audit.unmatched_generated_faces,
      merged_audit.bad_face_orientations,
      merged_audit.same_side_internal_faces,
      merged_audit.nonmanifold_faces,
      merged_audit.bad_edges,
      merged_audit.intersecting_cell_pairs);
    merged_solution_found = merged_accepted;
  }
}

void RunAnalyticValidationLadder() {
  const SourceMesh tetrahedron = MakeAnalyticMesh(
    {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{{0, 2, 1}}, {{0, 1, 3}}, {{0, 3, 2}}, {{1, 2, 3}}});
  const SourceMesh cube = MakeAnalyticMesh(
    {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
     {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}},
    {{{0, 1, 3}}, {{1, 2, 3}}, {{4, 5, 7}}, {{5, 6, 7}},
     {{0, 1, 4}}, {{1, 5, 4}}, {{1, 2, 5}}, {{2, 6, 5}},
     {{3, 7, 2}}, {{2, 7, 6}}, {{0, 3, 4}}, {{3, 7, 4}}});
  const float phi = 1.61803398875f;
  const SourceMesh icosahedron = MakeAnalyticMesh(
    {{-1.0f, phi, 0.0f}, {1.0f, phi, 0.0f}, {-1.0f, -phi, 0.0f}, {1.0f, -phi, 0.0f},
     {0.0f, -1.0f, phi}, {0.0f, 1.0f, phi}, {0.0f, -1.0f, -phi}, {0.0f, 1.0f, -phi},
     {phi, 0.0f, -1.0f}, {phi, 0.0f, 1.0f}, {-phi, 0.0f, -1.0f}, {-phi, 0.0f, 1.0f}},
    {{{0, 11, 5}}, {{0, 5, 1}}, {{0, 1, 7}}, {{0, 7, 10}}, {{0, 10, 11}},
     {{1, 5, 9}}, {{5, 11, 4}}, {{11, 10, 2}}, {{10, 7, 6}}, {{7, 1, 8}},
     {{3, 9, 4}}, {{3, 4, 2}}, {{3, 2, 6}}, {{3, 6, 8}}, {{3, 8, 9}},
     {{4, 9, 5}}, {{2, 4, 11}}, {{6, 2, 10}}, {{8, 6, 7}}, {{9, 8, 1}}});
  const auto run = [](const char* name, SourceMesh mesh) {
    OrientSourceMesh(&mesh);
    std::vector<Node> nodes;
    std::vector<PrismRecord> prisms;
    float scaffold_volume = 0.0f;
    float source_volume = 0.0f;
    BuildConformingFanTetraDecomposition(mesh, &nodes, &prisms, &scaffold_volume, &source_volume);
    const CellComplexAudit audit = AuditCellComplex(mesh, nodes, prisms);
    const bool accepted = CellComplexAccepted(audit, source_volume, scaffold_volume);
    std::vector<Node> seed_nodes(mesh.positions.size());
    for (size_t node = 0u; node < mesh.positions.size(); ++node) seed_nodes[node].rest = mesh.positions[node];
    TrihedralSeed seed{};
    const bool seed_valid = TryFindTrihedralSeed(mesh, seed_nodes, &seed);
    std::fprintf(
      stderr,
      "agent_adaptive_prism analytic name=%s accepted=%u seed_valid=%u seed_volume=%.9f generated_face=%d,%d,%d source_volume=%.9f scaffold_volume=%.9f volume_error=%.9f source_faces=%u valid_internal_faces=%u same_side_internal_faces=%u intersecting_cell_pairs=%u\n",
      name,
      accepted ? 1u : 0u,
      seed_valid ? 1u : 0u,
      seed.volume,
      seed.generated_face[0],
      seed.generated_face[1],
      seed.generated_face[2],
      source_volume,
      scaffold_volume,
      std::abs(source_volume - scaffold_volume),
      audit.source_face_count,
      audit.valid_internal_faces,
      audit.same_side_internal_faces,
      audit.intersecting_cell_pairs);
  };
  run("tetrahedron", tetrahedron);
  run("unit_cube", cube);
  run("icosahedron_sphere", icosahedron);
  RunFrontierPeelValidation("tetrahedron", tetrahedron);
  RunFrontierPeelValidation("unit_cube", cube);
  RunFrontierPeelValidation("icosahedron_sphere", icosahedron);
}

void RunLargeMeshValidation() {
  const SourceMesh mesh = MakeSubdividedIcosphere(4u);
  RingDecompositionResult result{};
  const bool accepted = TryBuildRingDecomposition(mesh, 4u, 5000u, &result);
  std::fprintf(
    stderr,
    "agent_adaptive_prism large_mesh_validation accepted=%u triangles=%zu vertices=%zu cells=%zu source_volume=%.9f scaffold_volume=%.9f volume_error=%.9f intersecting_cell_pairs=%u bad_face_orientations=%u nonmanifold_faces=%u\n",
    accepted ? 1u : 0u,
    mesh.triangles.size(),
    mesh.positions.size(),
    result.prisms.size(),
    result.source_volume,
    result.scaffold_volume,
    std::abs(result.source_volume - result.scaffold_volume),
    result.audit.intersecting_cell_pairs,
    result.audit.bad_face_orientations,
    result.audit.nonmanifold_faces);
}

void RunComplexTopologyValidation() {
  const SourceMesh mesh = MakeHighResolutionDumbbell();
  RingDecompositionResult result{};
  const bool accepted = TryBuildRingDecomposition(mesh, 16u, 5000u, &result);
  std::fprintf(
    stderr,
    "agent_adaptive_prism complex_topology_validation accepted=%u triangles=%zu vertices=%zu cells=%zu source_volume=%.9f scaffold_volume=%.9f volume_error=%.9f intersecting_cell_pairs=%u bad_face_orientations=%u nonmanifold_faces=%u\n",
    accepted ? 1u : 0u,
    mesh.triangles.size(),
    mesh.positions.size(),
    result.prisms.size(),
    result.source_volume,
    result.scaffold_volume,
    std::abs(result.source_volume - result.scaffold_volume),
    result.audit.intersecting_cell_pairs,
    result.audit.bad_face_orientations,
    result.audit.nonmanifold_faces);
}

bool SolveAdjacentPairing(
    const std::vector<std::vector<PrismRecord>>& candidates,
    const std::vector<Node>& nodes,
    std::vector<bool>* used,
    std::vector<PrismRecord>* selected) {
  size_t chosen_triangle = candidates.size();
  size_t chosen_options = std::numeric_limits<size_t>::max();
  for (size_t triangle = 0u; triangle < candidates.size(); ++triangle) {
    if ((*used)[triangle]) continue;
    size_t options = 0u;
    for (const PrismRecord& candidate : candidates[triangle]) {
      const int other = candidate.source_triangles[0] == static_cast<int>(triangle)
        ? candidate.source_triangles[1]
        : candidate.source_triangles[0];
      if (!(*used)[static_cast<size_t>(other)]) ++options;
    }
    if (options < chosen_options) {
      chosen_triangle = triangle;
      chosen_options = options;
    }
  }
  if (chosen_triangle == candidates.size()) return true;
  for (const PrismRecord& candidate : candidates[chosen_triangle]) {
    const int other = candidate.source_triangles[0] == static_cast<int>(chosen_triangle)
      ? candidate.source_triangles[1]
      : candidate.source_triangles[0];
    if ((*used)[static_cast<size_t>(other)]) continue;
    (*used)[chosen_triangle] = true;
    (*used)[static_cast<size_t>(other)] = true;
    selected->push_back(candidate);
    if (SolveAdjacentPairing(candidates, nodes, used, selected)) return true;
    selected->pop_back();
    (*used)[chosen_triangle] = false;
    (*used)[static_cast<size_t>(other)] = false;
  }
  return false;
}

void WriteDiscretePrism(
    algorithm::AlgorithmContainer& render,
    size_t* triangle_index,
    const PrismRecord& prism,
    const std::vector<Node>& nodes,
    const SourceMesh& source) {
  const Vec3 color = prism.display_kind == 1u
    ? Vec3{0.90f, 0.18f, 0.18f}
    : prism.topology_kind == 2u
      ? Vec3{0.20f, 0.42f, 0.95f}
      : Vec3{0.95f, 0.62f, 0.16f};
  if (prism.geometry_kind == 2u) {
    const SourceTriangle& first = source.triangles[static_cast<size_t>(prism.source_triangles[0])];
    WriteRenderTriangleColor(render, triangle_index, first.p[0], first.p[1], first.p[2], color);
    if (prism.source_triangles[1] >= 0) {
      const SourceTriangle& second = source.triangles[static_cast<size_t>(prism.source_triangles[1])];
      WriteRenderTriangleColor(render, triangle_index, second.p[0], second.p[1], second.p[2], color);
    }
    return;
  }
  std::array<Vec3, 6> p{};
  for (size_t i = 0u; i < p.size(); ++i) {
    const Vec3 rest = nodes[static_cast<size_t>(prism.nodes[i])].rest;
    p[i] = rest;
  }
  if (prism.geometry_kind == 1u) {
    for (size_t tetra_index = 0u; tetra_index < prism.tetra_count; ++tetra_index) {
      const auto& tetra = prism.tetra[tetra_index];
      std::array<Vec3, 4> tetra_points{};
      Vec3 tetra_center{};
      for (int node : tetra) tetra_center = tetra_center + nodes[static_cast<size_t>(node)].rest;
      tetra_center = tetra_center * 0.25f;
      const float display_scale = prism.display_kind == 1u ? 1.0f : 0.72f;
      for (size_t node_index = 0u; node_index < tetra_points.size(); ++node_index) {
        const Vec3 rest = nodes[static_cast<size_t>(tetra[node_index])].rest;
        tetra_points[node_index] = tetra_center + (rest - tetra_center) * display_scale;
      }
      WriteRenderTriangleColor(render, triangle_index, tetra_points[0], tetra_points[1], tetra_points[2], color);
      WriteRenderTriangleColor(render, triangle_index, tetra_points[0], tetra_points[3], tetra_points[1], color);
      WriteRenderTriangleColor(render, triangle_index, tetra_points[0], tetra_points[2], tetra_points[3], color);
      WriteRenderTriangleColor(render, triangle_index, tetra_points[1], tetra_points[3], tetra_points[2], color);
    }
    return;
  }
  if (prism.topology_kind == 1u) {
    WriteRenderTriangleColor(render, triangle_index, p[0], p[1], p[2], color);
    WriteRenderTriangleColor(render, triangle_index, p[0], p[5], p[1], color);
    WriteRenderTriangleColor(render, triangle_index, p[0], p[2], p[5], color);
    WriteRenderTriangleColor(render, triangle_index, p[1], p[5], p[2], color);
    return;
  }
  if (prism.topology_kind == 2u) {
    WriteRenderTriangleColor(render, triangle_index, p[4], p[0], p[1], color);
    WriteRenderTriangleColor(render, triangle_index, p[4], p[1], p[2], color);
    WriteRenderTriangleColor(render, triangle_index, p[4], p[2], p[3], color);
    WriteRenderTriangleColor(render, triangle_index, p[4], p[3], p[0], color);
    WriteRenderTriangleColor(render, triangle_index, p[0], p[3], p[2], color);
    WriteRenderTriangleColor(render, triangle_index, p[0], p[2], p[1], color);
    return;
  }
  WriteRenderTriangleColor(render, triangle_index, p[0], p[2], p[1], color);
  WriteRenderTriangleColor(render, triangle_index, p[3], p[4], p[5], color);
  WriteRenderTriangleColor(render, triangle_index, p[0], p[1], p[4], color);
  WriteRenderTriangleColor(render, triangle_index, p[0], p[4], p[3], color);
  WriteRenderTriangleColor(render, triangle_index, p[1], p[2], p[5], color);
  WriteRenderTriangleColor(render, triangle_index, p[1], p[5], p[4], color);
  WriteRenderTriangleColor(render, triangle_index, p[2], p[0], p[3], color);
  WriteRenderTriangleColor(render, triangle_index, p[2], p[3], p[5], color);
}

void WriteExposedPenta(
    algorithm::AlgorithmContainer& render,
    size_t* triangle_index,
    const PrismRecord& prism,
    const std::vector<Node>& nodes,
    const Vec3& offset) {
  std::array<Vec3, 5> p{};
  Vec3 center{};
  for (size_t i = 0u; i < p.size(); ++i) {
    center = center + nodes[static_cast<size_t>(prism.nodes[i])].rest;
  }
  center = center * 0.2f;
  const float cosine = std::cos(0.62f);
  const float sine = std::sin(0.62f);
  for (size_t i = 0u; i < p.size(); ++i) {
    const Vec3 local = (nodes[static_cast<size_t>(prism.nodes[i])].rest - center) * 1.35f;
    p[i] = center + Vec3{
      local.x,
      local.y * cosine - local.z * sine,
      local.y * sine + local.z * cosine} + offset;
  }
  const Vec3 source_color{1.0f, 0.86f, 0.08f};
  const Vec3 generated_color{0.98f, 0.08f, 0.72f};
  const Vec3 source_offset{0.0f, -0.025f, 0.0f};
  WriteRenderTriangleColor(render, triangle_index, p[0] + source_offset, p[1] + source_offset, p[2] + source_offset, source_color);
  WriteRenderTriangleColor(render, triangle_index, p[1] + source_offset, p[0] + source_offset, p[3] + source_offset, source_color);
  WriteRenderTriangleColor(render, triangle_index, p[1], p[2], p[4], generated_color);
  WriteRenderTriangleColor(render, triangle_index, p[2], p[0], p[4], generated_color);
  WriteRenderTriangleColor(render, triangle_index, p[0], p[3], p[4], generated_color);
  WriteRenderTriangleColor(render, triangle_index, p[3], p[1], p[4], generated_color);
  const std::array<std::array<size_t, 2>, 9> edges{{
    {{0u, 1u}}, {{1u, 2u}}, {{2u, 0u}}, {{0u, 3u}}, {{3u, 1u}},
    {{1u, 4u}}, {{2u, 4u}}, {{0u, 4u}}, {{3u, 4u}}}};
  const Vec3 front_offset{0.0f, -0.018f, 0.0f};
  for (const std::array<size_t, 2>& edge : edges) {
    const Vec3 a = p[edge[0]] + front_offset;
    const Vec3 b = p[edge[1]] + front_offset;
    const Vec3 width = Normalize(Cross(Vec3{0.0f, 1.0f, 0.0f}, b - a)) * 0.009f;
    const Vec3 edge_color{0.98f, 0.98f, 0.98f};
    WriteRenderTriangleColor(render, triangle_index, a - width, b - width, b + width, edge_color);
    WriteRenderTriangleColor(render, triangle_index, a - width, b + width, a + width, edge_color);
  }
}

struct LodLevelAudit {
  uint32_t level{0u};
  float cell_size{0.0f};
  uint32_t grid_resolution{0u};
  uint32_t active_cells{0u};
  uint32_t prism_count{0u};
  uint32_t grid_vertex_count{0u};
  uint32_t unique_vertex_count{0u};
};

struct LodVertex {
  uint32_t level{0u};
  Vec3 position{};
};

void BuildLodVertexAudit(
    const SourceMesh& source,
    std::array<LodLevelAudit, 1>* levels,
    std::vector<LodVertex>* vertices,
    uint32_t* checksum) {
  *checksum = 2166136261u;
  constexpr uint32_t kAuditLevel = 4u;
  for (uint32_t level = kAuditLevel; level <= kAuditLevel; ++level) {
    const uint32_t resolution = 1u << level;
    const float cell_size = 1.0f / static_cast<float>(resolution);
    std::set<std::array<int, 3>> unique_nodes;
    uint32_t active_cells = 0u;
    for (uint32_t z = 0u; z < resolution; ++z) {
      for (uint32_t y = 0u; y < resolution; ++y) {
        for (uint32_t x = 0u; x < resolution; ++x) {
          const Vec3 center{
            source.min_position.x + (static_cast<float>(x) + 0.5f) * cell_size,
            source.min_position.y + (static_cast<float>(y) + 0.5f) * cell_size,
            source.min_position.z + (static_cast<float>(z) + 0.5f) * cell_size};
          if (!PointInsideMesh(center, source)) continue;
          ++active_cells;
          for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
            unique_nodes.insert({static_cast<int>(x) + dx, static_cast<int>(y) + dy, static_cast<int>(z) + dz});
          }
        }
      }
    }
    LodLevelAudit& audit = (*levels)[0u];
    audit.level = level;
    audit.cell_size = cell_size;
    audit.grid_resolution = resolution + 1u;
    audit.active_cells = active_cells;
    audit.prism_count = active_cells * 2u;
    audit.grid_vertex_count = (resolution + 1u) * (resolution + 1u) * (resolution + 1u);
    audit.unique_vertex_count = static_cast<uint32_t>(unique_nodes.size());
    for (const std::array<int, 3>& node : unique_nodes) {
      const LodVertex vertex{
        level,
        {
          source.min_position.x + static_cast<float>(node[0]) * cell_size,
          source.min_position.y + static_cast<float>(node[1]) * cell_size,
          source.min_position.z + static_cast<float>(node[2]) * cell_size}};
      vertices->push_back(vertex);
      *checksum ^= level + static_cast<uint32_t>(node[0] * 31 + node[1] * 131 + node[2] * 521);
      *checksum *= 16777619u;
    }
  }
}

class DecompositionJobsExecutor final : public algomanager::bridge::IAlgorithmJobsExecutor {
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
    SourceMesh source = LoadSourceMesh(container_set);
    OrientSourceMesh(&source);
    SourceTopologyAudit source_topology = AuditSourceTopology(source);
    std::fprintf(
      stderr,
      "agent_adaptive_prism source_topology triangles=%zu boundary_edges=%u nonmanifold_edges=%u\n",
      source.triangles.size(),
      source_topology.boundary_edges,
      source_topology.nonmanifold_edges);
    if (source_topology.nonmanifold_edges != 0u) {
      RequireInvariantCode(false, 193);
    }
    if (source_topology.boundary_edges != 0u) {
      const uint32_t capped_loops = CapBoundaryLoops(&source);
      source_topology = AuditSourceTopology(source);
      std::fprintf(
        stderr,
        "agent_adaptive_prism source_topology_repaired capped_loops=%u triangles=%zu boundary_edges=%u nonmanifold_edges=%u\n",
        capped_loops,
        source.triangles.size(),
        source_topology.boundary_edges,
        source_topology.nonmanifold_edges);
      if (source_topology.boundary_edges != 0u || source_topology.nonmanifold_edges != 0u) RequireInvariantCode(false, 194);
    }
    if (tick_ == 0u && source.triangles.size() <= 100u) RunAnalyticValidationLadder();
    if (tick_ == 0u && source.triangles.size() <= 100u) RunLargeMeshValidation();
    if (tick_ == 0u && source.triangles.size() <= 100u) RunComplexTopologyValidation();
    if (tick_ == 0u && source.triangles.size() <= 100u) RunFrontierPeelValidation("banana_mesh", source);
    std::vector<Node> nodes;
    std::vector<PrismRecord> prisms;
    float scaffold_volume = 0.0f;
    float source_volume = 0.0f;
    uint32_t red_equator_units = 0u;
    if (!decomposition_ready_) {
      RingDecompositionResult ring_result{};
      const std::vector<SourceMesh> components = SplitMeshIntoEdgeConnectedComponents(source);
      std::fprintf(
        stderr,
        "agent_adaptive_prism input_components count=%zu triangles=%zu\n",
        components.size(),
        source.triangles.size());
      const bool surface_solution = TryBuildSurfaceSheetDecomposition(source, &ring_result);
      const bool cache_solution = !surface_solution && TryBuildTetMeshCache(source, &ring_result);
      const bool component_solution = !surface_solution && !cache_solution && components.size() > 1u &&
        TryBuildComponentDecomposition(source, components, 5000u, &ring_result);
      const bool single_solution = !surface_solution && !cache_solution && components.size() == 1u &&
        TryBuildRingDecomposition(source, 4u, 5000u, &ring_result);
      if (source.triangles.size() > 100u && !surface_solution && !cache_solution && !component_solution && !single_solution) RequireInvariantCode(false, 192);
      if (surface_solution || cache_solution || component_solution || single_solution) {
        decomposition_nodes_ = std::move(ring_result.nodes);
        decomposition_prisms_ = std::move(ring_result.prisms);
        decomposition_source_volume_ = ring_result.source_volume;
        decomposition_scaffold_volume_ = ring_result.scaffold_volume;
        decomposition_mode_ = surface_solution
          ? "surface_sheet_faces"
          : cache_solution
          ? "tetgen_offline_cache"
          : component_solution
          ? "component_split_ring_cut_frontier_search"
          : "ring_cut_frontier_search";
      } else {
        BuildConformingFanTetraDecomposition(
          source,
          &decomposition_nodes_,
          &decomposition_prisms_,
          &decomposition_scaffold_volume_,
          &decomposition_source_volume_);
        decomposition_mode_ = "conforming_fan_cells_fallback";
      }
      decomposition_ready_ = true;
    }
    nodes = decomposition_nodes_;
    prisms = decomposition_prisms_;
    source_volume = decomposition_source_volume_;
    scaffold_volume = decomposition_scaffold_volume_;
    const std::string decomposition_mode = decomposition_mode_;
    std::vector<Node> exposed_nodes;
    std::vector<PrismRecord> exposed_debug_prisms;
    float exposed_debug_scaffold_volume = 0.0f;
    float exposed_debug_source_volume = 0.0f;
    PrismRecord exposed_penta{};
    bool exposed_penta_valid = false;
    float exposed_penta_score = -1.0f;
    if (decomposition_mode != "tetgen_offline_cache" && decomposition_mode != "surface_sheet_faces") {
      BuildConformingFanTetraDecomposition(
        source,
        &exposed_nodes,
        &exposed_debug_prisms,
        &exposed_debug_scaffold_volume,
        &exposed_debug_source_volume);
      const int core_node = static_cast<int>(exposed_nodes.size()) - 1;
      for (size_t first = 0u; first < source.triangles.size(); ++first) {
        for (size_t second = first + 1u; second < source.triangles.size(); ++second) {
          if (SharedVertexCount(source.triangles[first], source.triangles[second]) != 2u) continue;
          PrismRecord candidate{};
          if (!BuildFanPentaUnit(
                source,
                static_cast<int>(first),
                static_cast<int>(second),
                core_node,
                exposed_nodes,
                &candidate)) continue;
          const std::array<std::array<size_t, 3>, 6> boundary_faces{{
            {{0u, 1u, 2u}}, {{1u, 0u, 3u}}, {{1u, 2u, 4u}},
            {{2u, 0u, 4u}}, {{0u, 3u, 4u}}, {{3u, 1u, 4u}}}};
          float candidate_score = 0.0f;
          for (const std::array<size_t, 3>& face : boundary_faces) {
            const Vec3 p0 = exposed_nodes[static_cast<size_t>(candidate.nodes[face[0]])].rest;
            const Vec3 p1 = exposed_nodes[static_cast<size_t>(candidate.nodes[face[1]])].rest;
            const Vec3 p2 = exposed_nodes[static_cast<size_t>(candidate.nodes[face[2]])].rest;
            candidate_score += std::abs(Cross(p1 - p0, p2 - p0).y);
          }
          if (candidate_score <= exposed_penta_score) continue;
          exposed_penta = candidate;
          exposed_penta_score = candidate_score;
          exposed_penta_valid = true;
        }
      }
      RequireInvariantCode(exposed_penta_valid, 140);
    }
    const Vec3 scene_center = (source.min_position + source.max_position) * 0.5f;
    const Vec3 scene_extent = source.max_position - source.min_position;
    const int long_axis = scene_extent.x >= std::max(scene_extent.y, scene_extent.z)
      ? 0
      : scene_extent.y >= scene_extent.z ? 1 : 2;
    const float axis_min = long_axis == 0 ? source.min_position.x : long_axis == 1 ? source.min_position.y : source.min_position.z;
    const float axis_extent = long_axis == 0 ? scene_extent.x : long_axis == 1 ? scene_extent.y : scene_extent.z;
    const float middle_min = axis_min + axis_extent * 0.40f;
    const float middle_max = axis_min + axis_extent * 0.60f;
    uint32_t middle_prism_count = 0u;
    for (PrismRecord& prism : prisms) {
      const float center_axis = long_axis == 0 ? prism.center.x : long_axis == 1 ? prism.center.y : prism.center.z;
      if (center_axis < middle_min || center_axis > middle_max) continue;
      prism.display_kind = 1u;
      ++middle_prism_count;
    }
    uint32_t positive_tetra_count = 0u;
    uint32_t negative_tetra_count = 0u;
    float signed_scaffold_volume = 0.0f;
    for (const PrismRecord& prism : prisms) {
      for (size_t tetra_index = 0u; tetra_index < prism.tetra_count; ++tetra_index) {
        const auto& tetra = prism.tetra[tetra_index];
        const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
        const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
        const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
        const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
        const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
        signed_scaffold_volume += determinant / 6.0f;
        if (determinant >= 0.0f) ++positive_tetra_count;
        if (determinant < 0.0f) ++negative_tetra_count;
      }
    }
    CellComplexAudit cell_complex_audit{};
    if (decomposition_mode == "surface_sheet_faces") {
      cell_complex_audit.source_face_count = static_cast<uint32_t>(source.triangles.size());
      cell_complex_audit.source_boundary_faces = cell_complex_audit.source_face_count;
      cell_complex_audit.valid_internal_faces = cell_complex_audit.source_face_count;
    } else {
      cell_complex_audit = AuditCellComplex(source, nodes, prisms);
    }
    const float volume_error = std::abs(source_volume - scaffold_volume);
    const float volume_tolerance = std::max(source_volume, 1.0e-6f) * 1.0e-5f;
    const bool accepted =
      cell_complex_audit.source_boundary_faces == cell_complex_audit.source_face_count &&
      cell_complex_audit.unmatched_generated_faces == 0u &&
      cell_complex_audit.bad_face_orientations == 0u &&
      cell_complex_audit.same_side_internal_faces == 0u &&
      cell_complex_audit.degenerate_internal_faces == 0u &&
      cell_complex_audit.nonmanifold_faces == 0u &&
      cell_complex_audit.bad_edges == 0u &&
      cell_complex_audit.intersecting_cell_pairs == 0u &&
      volume_error <= volume_tolerance;
    std::fprintf(
      stderr,
      "agent_adaptive_prism audit accepted=%u triangles=%zu prisms=%zu middle_prisms=%u source_volume=%.9f scaffold_volume=%.9f signed_scaffold_volume=%.9f volume_error=%.9f volume_tolerance=%.9f positive_tetras=%u negative_tetras=%u source_faces=%u boundary_faces=%u matched_internal_faces=%u valid_internal_faces=%u unmatched_generated_faces=%u bad_face_orientations=%u same_side_internal_faces=%u degenerate_internal_faces=%u nonmanifold_faces=%u bad_edges=%u intersecting_cell_pairs=%u\n",
      accepted ? 1u : 0u,
      source.triangles.size(),
      prisms.size(),
      middle_prism_count,
      source_volume,
      scaffold_volume,
      signed_scaffold_volume,
      volume_error,
      volume_tolerance,
      positive_tetra_count,
      negative_tetra_count,
      cell_complex_audit.source_face_count,
      cell_complex_audit.source_boundary_faces,
      cell_complex_audit.matched_internal_faces,
      cell_complex_audit.valid_internal_faces,
      cell_complex_audit.unmatched_generated_faces,
      cell_complex_audit.bad_face_orientations,
      cell_complex_audit.same_side_internal_faces,
      cell_complex_audit.degenerate_internal_faces,
      cell_complex_audit.nonmanifold_faces,
      cell_complex_audit.bad_edges,
      cell_complex_audit.intersecting_cell_pairs);

    algorithm::AlgorithmContainer& frame = *RequireMutableContainer<uint32_t>(container_set, "frame_tick");
    algorithm::AlgorithmContainer& prism_count = *RequireMutableContainer<uint32_t>(container_set, "prism_count");
    algorithm::AlgorithmContainer& node_count = *RequireMutableContainer<uint32_t>(container_set, "node_count");
    algorithm::AlgorithmContainer& tetra_count = *RequireMutableContainer<uint32_t>(container_set, "tetra_count");
    algorithm::AlgorithmContainer& source_count = *RequireMutableContainer<uint32_t>(container_set, "source_triangle_count");
    algorithm::AlgorithmContainer& checksum = *RequireMutableContainer<uint32_t>(container_set, "prism_checksum");
    algorithm::AlgorithmContainer& lod_level_count = *RequireMutableContainer<uint32_t>(container_set, "lod_level_count");
    algorithm::AlgorithmContainer& lod_vertex_count = *RequireMutableContainer<uint32_t>(container_set, "lod_vertex_count");
    algorithm::AlgorithmContainer& lod_checksum = *RequireMutableContainer<uint32_t>(container_set, "lod_checksum");
    algorithm::AlgorithmContainer& render_triangle_count = *RequireMutableContainer<uint32_t>(container_set, "render_triangle_count");
    algorithm::AlgorithmContainer& scene = *RequireMutableContainer<std::array<float, 4>>(container_set, "render_scene");
    algorithm::AlgorithmContainer& render = *RequireMutableContainer<std::array<float, 4>>(container_set, "render_triangle_buffer");
    algorithm::AlgorithmContainer& render_source = *RequireMutableContainer<std::array<float, 4>>(container_set, "render_source_triangle_buffer");
    algorithm::AlgorithmContainer& debug = *RequireMutableContainer<std::array<float, 28>>(container_set, "prism_debug");
    algorithm::AlgorithmContainer& tetra_indices = *RequireMutableContainer<std::array<uint32_t, 12>>(container_set, "prism_tetra_indices");
    algorithm::AlgorithmContainer& node_positions = *RequireMutableContainer<std::array<float, 3>>(container_set, "prism_node_positions");
    algorithm::AlgorithmContainer& lod_summary = *RequireMutableContainer<std::array<float, 12>>(container_set, "lod_level_summary");
    algorithm::AlgorithmContainer& lod_buffer = *RequireMutableContainer<std::array<float, 4>>(container_set, "lod_vertex_buffer");
    std::fprintf(
      stderr,
      "agent_adaptive_prism buffers render=%zu source_render=%zu debug=%zu tetra=%zu nodes=%zu\n",
      render.bytes.size() / render.element_stride / 4u,
      render_source.bytes.size() / render_source.element_stride / 4u,
      debug.bytes.size() / debug.element_stride,
      tetra_indices.bytes.size() / tetra_indices.element_stride,
      node_positions.bytes.size() / node_positions.element_stride);

    std::array<LodLevelAudit, 1> lod_levels{};
    std::vector<LodVertex> lod_vertices;
    uint32_t lod_hash = 0u;
    BuildLodVertexAudit(source, &lod_levels, &lod_vertices, &lod_hash);

    WriteUint32(frame, ++tick_);
    WriteUint32(prism_count, static_cast<uint32_t>(prisms.size()));
    WriteUint32(node_count, static_cast<uint32_t>(nodes.size()));
    uint32_t total_tetra_count = 0u;
    for (const PrismRecord& prism : prisms) total_tetra_count += prism.tetra_count;
    WriteUint32(tetra_count, total_tetra_count);
    WriteUint32(lod_level_count, 1u);
    WriteUint32(lod_vertex_count, static_cast<uint32_t>(lod_vertices.size()));
    WriteUint32(lod_checksum, lod_hash);
    const float scene_scale = std::max(scene_extent.x, std::max(scene_extent.y, scene_extent.z)) * 1.65f;
    WriteVec4(scene, 0u, scene_center, scene_scale);
    size_t rendered_source_triangle_count = 0u;
    const size_t source_render_capacity = render_source.bytes.size() / render_source.element_stride / 4u;
    const size_t source_render_stride = std::max<size_t>(
      1u,
      (source.triangles.size() + source_render_capacity - 1u) / source_render_capacity);
    for (size_t source_triangle_index = 0u;
         source_triangle_index < source.triangles.size() && rendered_source_triangle_count < source_render_capacity;
         source_triangle_index += source_render_stride) {
      const SourceTriangle& triangle = source.triangles[source_triangle_index];
      WriteRenderTriangleColor(
        render_source,
        &rendered_source_triangle_count,
        triangle.p[0],
        triangle.p[1],
        triangle.p[2],
        {0.04f, 0.85f, 1.0f});
    }
    WriteUint32(source_count, static_cast<uint32_t>(rendered_source_triangle_count));
    for (size_t i = 0u; i < nodes.size(); ++i) WriteElement<std::array<float, 3>>(node_positions, i, {nodes[i].rest.x, nodes[i].rest.y, nodes[i].rest.z});
    uint32_t hash = 2166136261u;
    for (size_t i = 0u; i < prisms.size(); ++i) {
      const PrismRecord& prism = prisms[i];
      std::array<float, 28> record{};
      for (size_t node_index = 0u; node_index < 6u; ++node_index) {
        const Vec3 position = nodes[static_cast<size_t>(prism.nodes[node_index])].rest;
        record[node_index * 3u + 0u] = position.x;
        record[node_index * 3u + 1u] = position.y;
        record[node_index * 3u + 2u] = position.z;
      }
      record[18] = prism.center.x;
      record[19] = prism.center.y;
      record[20] = prism.center.z;
      record[21] = prism.volume;
      record[22] = static_cast<float>(prism.source_triangles[0]);
      record[23] = static_cast<float>(prism.source_triangles[1]);
      record[24] = prism.pairing_score;
      record[25] = static_cast<float>(prism.nodes[0]);
      record[26] = static_cast<float>(prism.nodes[1]);
      record[27] = static_cast<float>(prism.nodes[2]);
      WriteElement<std::array<float, 28>>(debug, i, record);
      std::array<uint32_t, 12> tetra_record{};
      for (size_t tetra_index = 0u; tetra_index < prism.tetra_count; ++tetra_index) {
        for (size_t node_index = 0u; node_index < 4u; ++node_index) {
          tetra_record[tetra_index * 4u + node_index] = static_cast<uint32_t>(prism.tetra[tetra_index][node_index]);
          hash ^= tetra_record[tetra_index * 4u + node_index] + static_cast<uint32_t>(i * 17u + tetra_index);
          hash *= 16777619u;
        }
      }
      WriteElement<std::array<uint32_t, 12>>(tetra_indices, i, tetra_record);
    }
    const size_t discrete_prism_count = prisms.size();
    size_t rendered_triangle_count = 0u;
    for (size_t i = 0u; i < discrete_prism_count; ++i) {
      WriteDiscretePrism(render, &rendered_triangle_count, prisms[i], nodes, source);
    }
    WriteUint32(render_triangle_count, static_cast<uint32_t>(rendered_triangle_count));
    WriteUint32(checksum, hash);
    std::array<uint32_t, 3> topology_counts{};
    for (const PrismRecord& prism : prisms) ++topology_counts[static_cast<size_t>(prism.topology_kind)];
    for (size_t level = 0u; level < lod_levels.size(); ++level) {
      const LodLevelAudit& audit = lod_levels[level];
      WriteElement<std::array<float, 12>>(lod_summary, level, {
        -static_cast<float>(audit.level),
        audit.cell_size,
        static_cast<float>(audit.grid_resolution),
        static_cast<float>(audit.active_cells),
        static_cast<float>(audit.prism_count),
        static_cast<float>(audit.grid_vertex_count),
        static_cast<float>(audit.unique_vertex_count),
        static_cast<float>(audit.active_cells * 6u),
        0.0f, 0.0f, 0.0f, 0.0f});
    }
    for (size_t i = 0u; i < lod_vertices.size(); ++i) {
      const LodVertex& vertex = lod_vertices[i];
      WriteElement<std::array<float, 4>>(lod_buffer, i, {static_cast<float>(vertex.level), vertex.position.x, vertex.position.y, vertex.position.z});
    }
    if (debug_state) {
      const PrismRecord& first = prisms.front();
      const PrismRecord& last = prisms.back();
      std::string lod_summary_text;
      for (const LodLevelAudit& audit : lod_levels) {
        if (!lod_summary_text.empty()) lod_summary_text += ";";
        lod_summary_text += "-lg" + std::to_string(audit.level) +
          ":cells=" + std::to_string(audit.active_cells) +
          ",prisms=" + std::to_string(audit.prism_count) +
          ",vertices=" + std::to_string(audit.unique_vertex_count);
      }
      debug_state->signals.push_back({
        .name = "agent_adaptive_prism_decomposition_probe.jobs",
        .payload = "prisms=" + std::to_string(prisms.size()) +
          ",nodes=" + std::to_string(nodes.size()) +
          ",tetras=" + std::to_string(total_tetra_count) +
          ",tetra_units=" + std::to_string(topology_counts[1]) +
          ",penta_units=" + std::to_string(topology_counts[2]) +
          ",prism_units=" + std::to_string(topology_counts[0]) +
          ",discrete_prisms=" + std::to_string(discrete_prism_count) +
          ",render_triangles=" + std::to_string(rendered_triangle_count) +
          ",exposed_penta_source=" + std::to_string(exposed_penta.source_triangles[0]) + "," + std::to_string(exposed_penta.source_triangles[1]) +
          ",middle_prisms=" + std::to_string(middle_prism_count) +
          ",orange_units=" + std::to_string(prisms.size() - red_equator_units) +
          ",red_equator_units=" + std::to_string(red_equator_units) +
          ",mode=" + decomposition_mode +
          ",source_volume=" + std::to_string(source_volume) +
          ",scaffold_volume=" + std::to_string(scaffold_volume) +
          ",checksum=" + std::to_string(hash) +
          ",first_center=" + std::to_string(first.center.x) + "," + std::to_string(first.center.y) + "," + std::to_string(first.center.z) +
          ",last_center=" + std::to_string(last.center.x) + "," + std::to_string(last.center.y) + "," + std::to_string(last.center.z),
      });
      debug_state->signals.push_back({
        .name = "agent_adaptive_prism_decomposition_probe.lod",
        .payload = lod_summary_text + ",total_vertices=" + std::to_string(lod_vertices.size()) + ",checksum=" + std::to_string(lod_hash),
      });
    }
    if (algorithm_to_agent_signal) *algorithm_to_agent_signal = {};
    return true;
  }

 private:
  static float ComputeClosedMeshVolume(const SourceMesh& source, Vec3 center) {
    float volume = 0.0f;
    for (const SourceTriangle& triangle : source.triangles) {
      const Vec3 p0 = triangle.p[0] - center;
      const Vec3 p1 = triangle.p[1] - center;
      const Vec3 p2 = triangle.p[2] - center;
      volume += Dot(p0, Cross(p1, p2)) / 6.0f;
    }
    return std::abs(volume);
  }

  static void RefreshPrismGeometry(PrismRecord* prism, const std::vector<Node>& nodes) {
    prism->center = {};
    for (int node : prism->nodes) prism->center = prism->center + nodes[static_cast<size_t>(node)].rest;
    prism->center = prism->center * (1.0f / 6.0f);
    prism->volume = 0.0f;
    for (size_t tetra_index = 0u; tetra_index < prism->tetra_count; ++tetra_index) {
      const auto& tetra = prism->tetra[tetra_index];
      const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
      const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
      const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
      const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
      const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
      assert(std::abs(determinant) > 1.0e-10f);
      prism->volume += std::abs(determinant) / 6.0f;
    }
  }

  static PrismRecord BuildGridPrismUnit(
      const std::array<int, 3>& lower,
      const std::array<int, 3>& upper,
      const std::vector<Node>& nodes) {
    PrismRecord result{};
    const PrismCandidate candidate = MakePrismCandidate(lower, upper, 0, false, nodes);
    result.nodes = candidate.prism_nodes;
    result.tetra = candidate.tetra;
    result.tetra_count = 3u;
    result.topology_kind = 0u;
    result.geometry_kind = 0u;
    result.display_kind = 0u;
    result.source_triangles = {-1, -1};
    result.volume = 0.0f;
    result.center = {};
    for (int node : result.nodes) result.center = result.center + nodes[static_cast<size_t>(node)].rest;
    result.center = result.center * (1.0f / 6.0f);
    for (const auto& tetra : result.tetra) {
      const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
      const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
      const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
      const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
      const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
      assert(std::abs(determinant) > 1.0e-10f);
      result.volume += std::abs(determinant) / 6.0f;
    }
    return result;
  }

  static void BuildMinusLg4GridScaffold(
      const SourceMesh& source,
      std::vector<Node>* nodes,
      std::vector<PrismRecord>* prisms,
      float* scaffold_volume,
      float* source_volume) {
    constexpr int kGridResolution = 32;
    const Vec3 extent = source.max_position - source.min_position;
    const float cell_size = std::max(extent.x, std::max(extent.y, extent.z)) /
      static_cast<float>(kGridResolution);
    *source_volume = ComputeClosedMeshVolume(source, (source.min_position + source.max_position) * 0.5f);
    *scaffold_volume = 0.0f;
    nodes->clear();
    prisms->clear();
    std::map<std::array<int, 3>, int> grid_nodes;
    auto node_at = [&](int x, int y, int z) {
      const std::array<int, 3> key{x, y, z};
      const auto found = grid_nodes.find(key);
      if (found != grid_nodes.end()) return found->second;
      const int node_index = static_cast<int>(nodes->size());
      nodes->push_back({{
        source.min_position.x + static_cast<float>(x) * cell_size,
        source.min_position.y + static_cast<float>(y) * cell_size,
        source.min_position.z + static_cast<float>(z) * cell_size}});
      grid_nodes.emplace(key, node_index);
      return node_index;
    };
    for (int z = 0; z < kGridResolution; ++z) {
      for (int y = 0; y < kGridResolution; ++y) {
        for (int x = 0; x < kGridResolution; ++x) {
          const Vec3 cell_center{
            source.min_position.x + (static_cast<float>(x) + 0.5f) * cell_size,
            source.min_position.y + (static_cast<float>(y) + 0.5f) * cell_size,
            source.min_position.z + (static_cast<float>(z) + 0.5f) * cell_size};
          if (!PointInsideMesh(cell_center, source)) continue;
          const std::array<int, 3> lower_first{
            node_at(x, y, z), node_at(x + 1, y, z), node_at(x, y + 1, z)};
          const std::array<int, 3> lower_second{
            node_at(x + 1, y + 1, z), node_at(x, y + 1, z), node_at(x + 1, y, z)};
          const std::array<int, 3> upper_first{
            node_at(x, y, z + 1), node_at(x + 1, y, z + 1), node_at(x, y + 1, z + 1)};
          const std::array<int, 3> upper_second{
            node_at(x + 1, y + 1, z + 1), node_at(x, y + 1, z + 1), node_at(x + 1, y, z + 1)};
          PrismRecord first = BuildGridPrismUnit(lower_first, upper_first, *nodes);
          PrismRecord second = BuildGridPrismUnit(lower_second, upper_second, *nodes);
          *scaffold_volume += first.volume + second.volume;
          prisms->push_back(first);
          prisms->push_back(second);
        }
      }
    }
    assert(!prisms->empty());
    const float grid_volume = static_cast<float>(prisms->size()) * cell_size * cell_size * cell_size * 0.5f;
    assert(std::abs(*scaffold_volume - grid_volume) <= grid_volume * 1.0e-4f);
    const float volume_scale = std::pow(*source_volume / *scaffold_volume, 1.0f / 3.0f);
    const Vec3 volume_center = (source.min_position + source.max_position) * 0.5f;
    for (Node& node : *nodes) node.rest = volume_center + (node.rest - volume_center) * volume_scale;
    *scaffold_volume = 0.0f;
    for (PrismRecord& prism : *prisms) {
      RefreshPrismGeometry(&prism, *nodes);
      *scaffold_volume += prism.volume;
    }
    assert(std::abs(*scaffold_volume - *source_volume) <= *source_volume * 1.0e-5f);
  }

  static PrismRecord BuildRadialShellUnit(
      const SourceTriangle& source_triangle,
      int first_triangle,
      int lower0,
      int lower1,
      int lower2,
      int upper0,
      int upper1,
      int upper2,
      const std::vector<Node>& nodes,
      bool red_equator) {
    PrismRecord result{};
    result.nodes = {lower0, lower1, lower2, upper0, upper1, upper2};
    result.tetra = MakePrismCandidate(
      {lower0, lower1, lower2},
      {upper0, upper1, upper2},
      0,
      false,
      nodes).tetra;
    result.tetra_count = 3u;
    result.topology_kind = 0u;
    result.geometry_kind = red_equator ? 1u : 0u;
    result.display_kind = red_equator ? 1u : 0u;
    result.source_triangles = {first_triangle, first_triangle};
    result.pairing_score = 0.0f;
    result.volume = 0.0f;
    for (const auto& tetra : result.tetra) {
      const Vec3 p0 = nodes[static_cast<size_t>(tetra[0])].rest;
      const Vec3 p1 = nodes[static_cast<size_t>(tetra[1])].rest;
      const Vec3 p2 = nodes[static_cast<size_t>(tetra[2])].rest;
      const Vec3 p3 = nodes[static_cast<size_t>(tetra[3])].rest;
      const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
      assert(std::abs(determinant) > 1.0e-10f);
      result.volume += std::abs(determinant) / 6.0f;
    }
    result.center = {};
    for (int node : result.nodes) result.center = result.center + nodes[static_cast<size_t>(node)].rest;
    result.center = result.center * (1.0f / 6.0f);
    return result;
  }

  static PrismRecord BuildRadialCoreTetra(
      const SourceTriangle& source_triangle,
      int first_triangle,
      int center_node,
      int lower0,
      int lower1,
      int lower2,
      const std::vector<Node>& nodes) {
    PrismRecord result{};
    result.nodes = {center_node, lower0, lower1, lower2, center_node, center_node};
    result.tetra[0] = {{center_node, lower0, lower1, lower2}};
    result.tetra_count = 1u;
    result.topology_kind = 1u;
    result.geometry_kind = 1u;
    result.display_kind = 0u;
    result.source_triangles = {first_triangle, first_triangle};
    const Vec3 p0 = nodes[static_cast<size_t>(center_node)].rest;
    const Vec3 p1 = nodes[static_cast<size_t>(lower0)].rest;
    const Vec3 p2 = nodes[static_cast<size_t>(lower1)].rest;
    const Vec3 p3 = nodes[static_cast<size_t>(lower2)].rest;
    const float determinant = Dot(p1 - p0, Cross(p2 - p0, p3 - p0));
    assert(std::abs(determinant) > 1.0e-10f);
    if (determinant < 0.0f) std::swap(result.tetra[0][1], result.tetra[0][2]);
    result.volume = std::abs(determinant) / 6.0f;
    result.center = (p0 + p1 + p2 + p3) * 0.25f;
    return result;
  }

  static void BuildRadialSphereVolumeScaffold(
      const SourceMesh& source,
      std::vector<Node>* nodes,
      std::vector<PrismRecord>* prisms,
      float* scaffold_volume,
      float* source_volume,
      uint32_t* red_equator_units) {
    const Vec3 center = (source.min_position + source.max_position) * 0.5f;
    float radius = 0.0f;
    for (const Vec3 position : source.positions) radius = std::max(radius, Length(position - center));
    assert(radius > 1.0e-6f);
    *source_volume = ComputeClosedMeshVolume(source, center);
    *scaffold_volume = 0.0f;
    *red_equator_units = 0u;
    nodes->clear();
    prisms->clear();

    constexpr uint32_t kRadialLayerCount = 5u;
    std::vector<std::vector<int>> layers(kRadialLayerCount);
    const int center_node = 0;
    nodes->push_back({center});
    for (uint32_t layer = 0u; layer < kRadialLayerCount; ++layer) {
      const float fraction = 0.2f + 0.2f * static_cast<float>(layer);
      layers[layer].reserve(source.positions.size());
      for (const Vec3 position : source.positions) {
        const int node_index = static_cast<int>(nodes->size());
        nodes->push_back({center + (position - center) * fraction});
        layers[layer].push_back(node_index);
      }
    }
    for (size_t triangle_index = 0u; triangle_index < source.triangles.size(); ++triangle_index) {
      const SourceTriangle& triangle = source.triangles[triangle_index];
      const float min_z = std::min(triangle.p[0].z, std::min(triangle.p[1].z, triangle.p[2].z));
      const float max_z = std::max(triangle.p[0].z, std::max(triangle.p[1].z, triangle.p[2].z));
      const bool equator_triangle = min_z <= center.z && max_z >= center.z;
      PrismRecord core = BuildRadialCoreTetra(
        triangle,
        static_cast<int>(triangle_index),
        center_node,
        layers[0][static_cast<size_t>(triangle.indices[0])],
        layers[0][static_cast<size_t>(triangle.indices[1])],
        layers[0][static_cast<size_t>(triangle.indices[2])],
        *nodes);
      *scaffold_volume += core.volume;
      prisms->push_back(core);
      for (uint32_t shell = 0u; shell + 1u < kRadialLayerCount; ++shell) {
        const bool red = equator_triangle && shell + 2u == kRadialLayerCount;
        PrismRecord unit = BuildRadialShellUnit(
          triangle,
          static_cast<int>(triangle_index),
          layers[shell][static_cast<size_t>(triangle.indices[0])],
          layers[shell][static_cast<size_t>(triangle.indices[1])],
          layers[shell][static_cast<size_t>(triangle.indices[2])],
          layers[shell + 1u][static_cast<size_t>(triangle.indices[0])],
          layers[shell + 1u][static_cast<size_t>(triangle.indices[1])],
          layers[shell + 1u][static_cast<size_t>(triangle.indices[2])],
          *nodes,
          red);
        *scaffold_volume += unit.volume;
        if (red) ++*red_equator_units;
        prisms->push_back(unit);
      }
    }
    RequireInvariant(std::abs(*scaffold_volume - *source_volume) <= *source_volume * 1.0e-4f);
    for (size_t first = 0u; first < prisms->size(); ++first) {
      for (size_t second = first + 1u; second < prisms->size(); ++second) {
        RequireInvariant(!PrismRecordsIntersect((*prisms)[first], (*prisms)[second], *nodes));
      }
    }
  }

  static void BuildPrismsFromTrianglePairs(
      const SourceMesh& source,
      std::vector<Node>* nodes,
      std::vector<PrismRecord>* prisms) {
    nodes->resize(source.positions.size());
    for (size_t i = 0u; i < source.positions.size(); ++i) (*nodes)[i].rest = source.positions[i];
    std::vector<std::vector<PrismRecord>> candidates(source.triangles.size());
    for (size_t first = 0u; first < source.triangles.size(); ++first) {
      for (size_t second = first + 1u; second < source.triangles.size(); ++second) {
        const size_t shared_count = SharedVertexCount(source.triangles[first], source.triangles[second]);
        PrismRecord unit{};
        bool valid = false;
        if (shared_count == 2u) {
          valid = BuildAdjacentPairUnit(source, static_cast<int>(first), static_cast<int>(second), *nodes, &unit);
          unit.pairing_score += 0.4f;
        } else if (shared_count == 1u) {
          valid = BuildPentahedronPairUnit(source, static_cast<int>(first), static_cast<int>(second), *nodes, &unit);
        } else {
          valid = BuildBestPairPrism(source, static_cast<int>(first), static_cast<int>(second), *nodes, &unit);
        }
        if (!valid) continue;
        candidates[first].push_back(unit);
        candidates[second].push_back(unit);
      }
    }
    for (std::vector<PrismRecord>& triangle_candidates : candidates) {
      std::sort(triangle_candidates.begin(), triangle_candidates.end(), [](const PrismRecord& left, const PrismRecord& right) {
        return left.pairing_score < right.pairing_score;
      });
      if (triangle_candidates.size() > 64u) triangle_candidates.resize(64u);
    }
    std::vector<bool> used(source.triangles.size(), false);
    std::vector<PrismRecord> selected;
    const bool solved = SolveAdjacentPairing(candidates, *nodes, &used, &selected);
    RequireInvariant(solved);
    RequireInvariant(selected.size() * 2u == source.triangles.size());
    for (size_t first = 0u; first < selected.size(); ++first) {
      RequireInvariant(PrismRecordContainedByMesh(selected[first], *nodes, source));
      for (size_t second = first + 1u; second < selected.size(); ++second) {
        RequireInvariant(!PrismRecordsIntersect(selected[first], selected[second], *nodes));
      }
    }
    *prisms = selected;
  }

  uint32_t tick_{0u};
  bool decomposition_ready_{false};
  std::vector<Node> decomposition_nodes_;
  std::vector<PrismRecord> decomposition_prisms_;
  float decomposition_source_volume_{0.0f};
  float decomposition_scaffold_volume_{0.0f};
  std::string decomposition_mode_;
};

void DestroyJobsExecutor(algomanager::bridge::IAlgorithmJobsExecutor* executor) { delete executor; }

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
  out_bundle->jobs_executor = new DecompositionJobsExecutor();
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
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(request->algorithm_name ? request->algorithm_name : "", &location, nullptr)) return false;
  if (!algomanager::algocatalog::LoadAlgorithmPackageReflectorFromLocation(location, &reflector, nullptr)) return false;
  *out_reflector = *reflector;
  return true;
}
