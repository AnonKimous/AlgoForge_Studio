#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kCellSize = 0.0625f;
constexpr float kDensity = 1000.0f;
constexpr float kYoungModulus = 2.0e5f;
constexpr float kPoissonRatio = 0.22f;
constexpr float kTensileStrength = 1.5e4f;
constexpr float kShearStrength = 2.5e4f;
constexpr float kFractureTime = 0.002f;
constexpr float kDamageExponent = 1.0f;
constexpr float kContactStiffness = 2.0e7f;
constexpr float kContactDamping = 1.0e3f;
constexpr float kProjectileRadius = 0.12f;
constexpr float kProjectileMass = 56.0f;
constexpr float kInitialProjectileX = -1.10f;
constexpr float kInitialProjectileSpeed = 6.0f;
constexpr float kBackingPlaneX = 0.56f;
constexpr float kFractureCenterBand = 1.5f;
constexpr size_t kMaxRenderTriangles = 25000u;

struct Vec3 {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 operator*(float s, Vec3 a) { return a * s; }
Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }

float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) {
  return {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}
float LengthSquared(Vec3 a) { return Dot(a, a); }
float Length(Vec3 a) { return std::sqrt(LengthSquared(a)); }
Vec3 Normalize(Vec3 a) { return a * (1.0f / Length(a)); }
Vec3 Min(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
Vec3 Max(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct Mat3 {
  Vec3 c[3]{};
};

Mat3 Identity() {
  return {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
}

Mat3 Transpose(Mat3 a) {
  return {{
    {a.c[0].x, a.c[1].x, a.c[2].x},
    {a.c[0].y, a.c[1].y, a.c[2].y},
    {a.c[0].z, a.c[1].z, a.c[2].z},
  }};
}

Vec3 Mul(Mat3 a, Vec3 v) { return a.c[0] * v.x + a.c[1] * v.y + a.c[2] * v.z; }

Mat3 Mul(Mat3 a, Mat3 b) {
  return {{Mul(a, b.c[0]), Mul(a, b.c[1]), Mul(a, b.c[2])}};
}

Mat3 Add(Mat3 a, Mat3 b) { return {{a.c[0] + b.c[0], a.c[1] + b.c[1], a.c[2] + b.c[2]}}; }
Mat3 Sub(Mat3 a, Mat3 b) { return {{a.c[0] - b.c[0], a.c[1] - b.c[1], a.c[2] - b.c[2]}}; }
Mat3 Scale(Mat3 a, float s) { return {{a.c[0] * s, a.c[1] * s, a.c[2] * s}}; }

float Trace(Mat3 a) { return a.c[0].x + a.c[1].y + a.c[2].z; }
float Determinant(Mat3 a) { return Dot(a.c[0], Cross(a.c[1], a.c[2])); }

Mat3 Inverse(Mat3 a) {
  const Vec3 r0 = Cross(a.c[1], a.c[2]);
  const Vec3 r1 = Cross(a.c[2], a.c[0]);
  const Vec3 r2 = Cross(a.c[0], a.c[1]);
  const float inv_det = 1.0f / Dot(a.c[0], r0);
  return Scale({{r0, r1, r2}}, inv_det);
}

struct SourceTriangle {
  Vec3 p[3]{};
};

struct SourceMesh {
  std::vector<Vec3> positions{};
  std::vector<SourceTriangle> triangles{};
  Vec3 min_position{};
  Vec3 max_position{};
};

struct Node {
  Vec3 rest{};
  Vec3 position{};
  Vec3 velocity{};
  Vec3 force{};
  float mass{0.0f};
};

struct Tetra {
  std::array<int, 4> node{};
  Mat3 inverse_dm{};
  float volume{0.0f};
  Mat3 stress{};
  float stress_score{0.0f};
  int component{-1};
  std::array<Vec3, 4> fragment_local{};
};

struct FaceOwner {
  int tetra{-1};
  std::array<int, 3> oriented{};
};

struct InterfaceFace {
  int left{-1};
  int right{-1};
  std::array<int, 3> oriented{};
  Vec3 normal{};
  float area{0.0f};
  float damage{0.0f};
  bool broken{false};
};

struct BoundaryFace {
  int tetra{-1};
  std::array<int, 3> oriented{};
};

struct Fragment {
  Vec3 center{};
  Vec3 velocity{};
  float mass{0.0f};
};

struct RenderTriangle {
  Vec3 p[3]{};
  Vec3 color{};
};

struct RenderBvhNode {
  Vec3 min_position{};
  Vec3 max_position{};
  int left{-1};
  int right{-1};
  int triangle{-1};
};

using FaceKey = std::array<int, 3>;
using QuadKey = std::array<int, 4>;
using DiagonalKey = std::array<int, 2>;

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

uint32_t ReadUint32(const algorithm::AlgorithmContainer& container) {
  return ReadElement<uint32_t>(container, 0u);
}

void WriteVec4(algorithm::AlgorithmContainer& container, size_t index, Vec3 xyz, float w) {
  const std::array<float, 4> value{xyz.x, xyz.y, xyz.z, w};
  WriteElement<std::array<float, 4>>(container, index, value);
}

FaceKey MakeFaceKey(std::array<int, 3> face) {
  std::sort(face.begin(), face.end());
  return face;
}

QuadKey MakeQuadKey(std::array<int, 4> quad) {
  std::sort(quad.begin(), quad.end());
  return quad;
}

DiagonalKey MakeDiagonalKey(int a, int b) {
  if (a > b) std::swap(a, b);
  return {a, b};
}

Vec3 FaceNormal(const std::vector<Node>& nodes, std::array<int, 3> face) {
  return Normalize(Cross(
    nodes[static_cast<size_t>(face[1])].position - nodes[static_cast<size_t>(face[0])].position,
    nodes[static_cast<size_t>(face[2])].position - nodes[static_cast<size_t>(face[0])].position));
}

float FaceArea(const std::vector<Node>& nodes, std::array<int, 3> face) {
  return 0.5f * Length(Cross(
    nodes[static_cast<size_t>(face[1])].position - nodes[static_cast<size_t>(face[0])].position,
    nodes[static_cast<size_t>(face[2])].position - nodes[static_cast<size_t>(face[0])].position));
}

std::array<std::array<int, 3>, 4> TetraFaces(const Tetra& tetra) {
  return {{
    {tetra.node[1], tetra.node[2], tetra.node[3]},
    {tetra.node[0], tetra.node[3], tetra.node[2]},
    {tetra.node[0], tetra.node[1], tetra.node[3]},
    {tetra.node[0], tetra.node[2], tetra.node[1]},
  }};
}

bool RayIntersectsTriangle(Vec3 origin, const SourceTriangle& triangle) {
  const Vec3 direction{1.0f, 0.0f, 0.0f};
  const Vec3 e1 = triangle.p[1] - triangle.p[0];
  const Vec3 e2 = triangle.p[2] - triangle.p[0];
  const Vec3 p = Cross(direction, e2);
  const float determinant = Dot(e1, p);
  if (std::abs(determinant) < 1.0e-8f) return false;
  const float inv_det = 1.0f / determinant;
  const Vec3 t = origin - triangle.p[0];
  const float u = Dot(t, p) * inv_det;
  if (u < 0.0f || u > 1.0f) return false;
  const Vec3 q = Cross(t, e1);
  const float v = Dot(direction, q) * inv_det;
  if (v < 0.0f || u + v > 1.0f) return false;
  return Dot(e2, q) * inv_det > 0.0f;
}

bool PointInsideMesh(Vec3 point, const SourceMesh& mesh) {
  point.y += 1.0e-5f;
  point.z += 2.0e-5f;
  size_t intersections = 0u;
  for (const SourceTriangle& triangle : mesh.triangles) {
    if (RayIntersectsTriangle(point, triangle)) ++intersections;
  }
  return (intersections & 1u) != 0u;
}

SourceMesh LoadSourceMesh(const algorithm::AlgorithmContainerSet* set) {
  const algorithm::AlgorithmContainer* positions = RequireContainer<float>(set, "vertex_mesh_0");
  const algorithm::AlgorithmContainer* triangles = RequireContainer<uint32_t>(set, "triangle_mesh_0");
  SourceMesh mesh{};
  const size_t position_count = positions->bytes.size() / positions->element_stride;
  mesh.positions.resize(position_count);
  mesh.min_position = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
  mesh.max_position = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  for (size_t i = 0u; i < position_count; ++i) {
    const std::array<float, 3> p = ReadElement<std::array<float, 3>>(*positions, i);
    mesh.positions[i] = {p[0], p[1], p[2]};
    mesh.min_position = Min(mesh.min_position, mesh.positions[i]);
    mesh.max_position = Max(mesh.max_position, mesh.positions[i]);
  }
  const size_t triangle_count = triangles->bytes.size() / triangles->element_stride;
  for (size_t i = 0u; i < triangle_count; ++i) {
    const std::array<uint32_t, 3> indices = ReadElement<std::array<uint32_t, 3>>(*triangles, i);
    if (indices == std::array<uint32_t, 3>{0u, 0u, 0u}) continue;
    assert(indices[0] < mesh.positions.size());
    assert(indices[1] < mesh.positions.size());
    assert(indices[2] < mesh.positions.size());
    mesh.triangles.push_back({{
      mesh.positions[indices[0]],
      mesh.positions[indices[1]],
      mesh.positions[indices[2]],
    }});
  }
  assert(!mesh.triangles.empty());
  return mesh;
}

struct PrismCandidate {
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
  if (!reverse) {
    candidate.tetra = {{
      {a[0], a[1], a[2], b[0]},
      {a[1], a[2], b[1], b[0]},
      {a[2], b[1], b[2], b[0]},
    }};
    candidate.diagonals = {{
      MakeDiagonalKey(a[1], b[0]),
      MakeDiagonalKey(a[2], b[1]),
      MakeDiagonalKey(a[2], b[0]),
    }};
  } else {
    candidate.tetra = {{
      {a[0], a[1], a[2], b[1]},
      {a[0], a[2], b[2], b[1]},
      {a[0], b[2], b[0], b[1]},
    }};
    candidate.diagonals = {{
      MakeDiagonalKey(a[0], b[1]),
      MakeDiagonalKey(a[2], b[1]),
      MakeDiagonalKey(a[0], b[2]),
    }};
  }
  candidate.quads = {{
    MakeQuadKey({a[0], a[1], b[0], b[1]}),
    MakeQuadKey({a[1], a[2], b[1], b[2]}),
    MakeQuadKey({a[2], a[0], b[2], b[0]}),
  }};
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
    std::map<QuadKey, DiagonalKey>* diagonals) {
  for (int reverse = 0; reverse < 2; ++reverse) {
    for (int rotation = 0; rotation < 3; ++rotation) {
      PrismCandidate candidate = MakePrismCandidate(lower, upper, rotation, reverse != 0, nodes);
      bool compatible = true;
      for (size_t i = 0u; i < candidate.quads.size(); ++i) {
        const auto found = diagonals->find(candidate.quads[i]);
        if (found != diagonals->end() && found->second != candidate.diagonals[i]) {
          compatible = false;
        }
      }
      if (!compatible) continue;
      for (size_t i = 0u; i < candidate.quads.size(); ++i) {
        diagonals->emplace(candidate.quads[i], candidate.diagonals[i]);
      }
      return candidate;
    }
  }
  assert(false && "No globally compatible triangular-prism tetrahedral template.");
  return {};
}

class Simulation {
 public:
  SourceMesh source{};
  std::vector<Node> nodes{};
  std::vector<Tetra> tetras{};
  std::vector<InterfaceFace> interfaces{};
  std::vector<BoundaryFace> boundaries{};
  std::vector<Fragment> fragments{};
  std::vector<RenderTriangle> render_triangles{};
  std::vector<RenderBvhNode> render_bvh{};
  Vec3 projectile_center{kInitialProjectileX, 0.0f, 0.0f};
  Vec3 projectile_velocity{kInitialProjectileSpeed, 0.0f, 0.0f};
  float projectile_impulse{0.0f};
  uint32_t tick{0u};
  bool fractured{false};

  void Build(const algorithm::AlgorithmContainerSet* set) {
    source = LoadSourceMesh(set);
    const int nx = static_cast<int>(std::lround((source.max_position.x - source.min_position.x) / kCellSize)) + 1;
    const int ny = static_cast<int>(std::lround((source.max_position.y - source.min_position.y) / kCellSize)) + 1;
    const int nz = static_cast<int>(std::lround((source.max_position.z - source.min_position.z) / kCellSize)) + 1;
    assert(nx >= 2 && ny >= 2 && nz >= 2);
    const size_t grid_size = static_cast<size_t>(nx) * static_cast<size_t>(ny) * static_cast<size_t>(nz);
    nodes.resize(grid_size);
    auto grid_index = [nx, ny](int x, int y, int z) {
      return (z * ny + y) * nx + x;
    };
    for (int z = 0; z < nz; ++z) {
      for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
          const Vec3 position{
            source.min_position.x + static_cast<float>(x) * kCellSize,
            source.min_position.y + static_cast<float>(y) * kCellSize,
            source.min_position.z + static_cast<float>(z) * kCellSize,
          };
          Node& node = nodes[static_cast<size_t>(grid_index(x, y, z))];
          node.rest = position;
          node.position = position;
        }
      }
    }

    std::map<QuadKey, DiagonalKey> diagonal_map;
    for (int z = 0; z < nz - 1; ++z) {
      for (int y = 0; y < ny - 1; ++y) {
        for (int x = 0; x < nx - 1; ++x) {
          const int p00 = grid_index(x, y, z);
          const int p10 = grid_index(x + 1, y, z);
          const int p11 = grid_index(x + 1, y + 1, z);
          const int p01 = grid_index(x, y + 1, z);
          const int q00 = grid_index(x, y, z + 1);
          const int q10 = grid_index(x + 1, y, z + 1);
          const int q11 = grid_index(x + 1, y + 1, z + 1);
          const int q01 = grid_index(x, y + 1, z + 1);
          const std::array<std::array<int, 3>, 2> lower_triangles = (x + y) % 2 == 0
            ? std::array<std::array<int, 3>, 2>{{{p00, p10, p11}, {p00, p11, p01}}}
            : std::array<std::array<int, 3>, 2>{{{p00, p10, p01}, {p10, p11, p01}}};
          const std::array<std::array<int, 3>, 2> upper_triangles = (x + y) % 2 == 0
            ? std::array<std::array<int, 3>, 2>{{{q00, q10, q11}, {q00, q11, q01}}}
            : std::array<std::array<int, 3>, 2>{{{q00, q10, q01}, {q10, q11, q01}}};
          for (size_t prism = 0u; prism < 2u; ++prism) {
            const PrismCandidate candidate = SelectPrismCandidate(
              lower_triangles[prism], upper_triangles[prism], nodes, &diagonal_map);
            for (const std::array<int, 4>& candidate_tetra : candidate.tetra) {
              const Vec3 center = (
                nodes[static_cast<size_t>(candidate_tetra[0])].rest +
                nodes[static_cast<size_t>(candidate_tetra[1])].rest +
                nodes[static_cast<size_t>(candidate_tetra[2])].rest +
                nodes[static_cast<size_t>(candidate_tetra[3])].rest) * 0.25f;
              if (!PointInsideMesh(center, source)) continue;
              Tetra tetra{};
              tetra.node = candidate_tetra;
              const Vec3 p0 = nodes[static_cast<size_t>(tetra.node[0])].rest;
              const Vec3 p1 = nodes[static_cast<size_t>(tetra.node[1])].rest;
              const Vec3 p2 = nodes[static_cast<size_t>(tetra.node[2])].rest;
              const Vec3 p3 = nodes[static_cast<size_t>(tetra.node[3])].rest;
              const Mat3 dm{{p1 - p0, p2 - p0, p3 - p0}};
              const float determinant = Determinant(dm);
              assert(determinant > 0.0f);
              tetra.volume = determinant / 6.0f;
              tetra.inverse_dm = Inverse(dm);
              tetras.push_back(tetra);
            }
          }
        }
      }
    }
    assert(!tetras.empty());
    BuildAdjacency();
    for (const Tetra& tetra : tetras) {
      const float mass = kDensity * tetra.volume / 4.0f;
      for (int node : tetra.node) nodes[static_cast<size_t>(node)].mass += mass;
    }
    for (const Node& node : nodes) {
      (void)node;
    }
  }

  void Step(float dt) {
    const int substeps = std::max(1, static_cast<int>(std::ceil(dt / (1.0f / 960.0f))));
    const float sub_dt = dt / static_cast<float>(substeps);
    for (int substep = 0; substep < substeps; ++substep) {
      for (Node& node : nodes) node.force = {};
      Vec3 projectile_force{};
      for (Tetra& tetra : tetras) {
        const Vec3 p0 = nodes[static_cast<size_t>(tetra.node[0])].position;
        const Vec3 p1 = nodes[static_cast<size_t>(tetra.node[1])].position;
        const Vec3 p2 = nodes[static_cast<size_t>(tetra.node[2])].position;
        const Vec3 p3 = nodes[static_cast<size_t>(tetra.node[3])].position;
        const Mat3 ds{{p1 - p0, p2 - p0, p3 - p0}};
        const Mat3 f = Mul(ds, tetra.inverse_dm);
        const float j = Determinant(f);
        assert(j > 0.0f);
        const Mat3 strain = Sub(Scale(Add(f, Transpose(f)), 0.5f), Identity());
        const float lambda = kYoungModulus * kPoissonRatio / ((1.0f + kPoissonRatio) * (1.0f - 2.0f * kPoissonRatio));
        const float mu = kYoungModulus / (2.0f * (1.0f + kPoissonRatio));
        tetra.stress = Add(Scale(Identity(), lambda * Trace(strain)), Scale(strain, 2.0f * mu));
        tetra.stress_score = std::max(0.0f, Trace(tetra.stress) / (3.0f * kTensileStrength));
        const Mat3 h = Scale(Mul(tetra.stress, Transpose(tetra.inverse_dm)), -tetra.volume);
        const Vec3 f1 = h.c[0];
        const Vec3 f2 = h.c[1];
        const Vec3 f3 = h.c[2];
        const Vec3 f0 = (f1 + f2 + f3) * -1.0f;
        nodes[static_cast<size_t>(tetra.node[0])].force += f0;
        nodes[static_cast<size_t>(tetra.node[1])].force += f1;
        nodes[static_cast<size_t>(tetra.node[2])].force += f2;
        nodes[static_cast<size_t>(tetra.node[3])].force += f3;
      }

      for (const BoundaryFace& boundary : boundaries) {
        const Vec3 p0 = nodes[static_cast<size_t>(boundary.oriented[0])].position;
        const Vec3 p1 = nodes[static_cast<size_t>(boundary.oriented[1])].position;
        const Vec3 p2 = nodes[static_cast<size_t>(boundary.oriented[2])].position;
        const Vec3 center = (p0 + p1 + p2) * (1.0f / 3.0f);
        const float area = 0.5f * Length(Cross(p1 - p0, p2 - p0));
        const Vec3 normal = Normalize(Cross(p1 - p0, p2 - p0));
        const Vec3 delta = center - projectile_center;
        const float distance = Length(delta);
        const float penetration = kProjectileRadius - distance;
        if (penetration > 0.0f) {
          const Vec3 contact_normal = Normalize(delta);
          const Vec3 target_velocity = (
            nodes[static_cast<size_t>(boundary.oriented[0])].velocity +
            nodes[static_cast<size_t>(boundary.oriented[1])].velocity +
            nodes[static_cast<size_t>(boundary.oriented[2])].velocity) * (1.0f / 3.0f);
          const float normal_velocity = Dot(target_velocity - projectile_velocity, contact_normal);
          const float pressure = kContactStiffness * penetration + kContactDamping * std::max(-normal_velocity, 0.0f);
          const Vec3 force = contact_normal * (pressure * area);
          for (int node : boundary.oriented) nodes[static_cast<size_t>(node)].force += force * (1.0f / 3.0f);
          projectile_force -= force;
        }
        const float wall_penetration = center.x - kBackingPlaneX;
        if (wall_penetration > 0.0f) {
          const Vec3 target_velocity = (
            nodes[static_cast<size_t>(boundary.oriented[0])].velocity +
            nodes[static_cast<size_t>(boundary.oriented[1])].velocity +
            nodes[static_cast<size_t>(boundary.oriented[2])].velocity) * (1.0f / 3.0f);
          const float pressure = kContactStiffness * wall_penetration + kContactDamping * std::max(target_velocity.x, 0.0f);
          const Vec3 force{-pressure * area, 0.0f, 0.0f};
          for (int node : boundary.oriented) nodes[static_cast<size_t>(node)].force += force * (1.0f / 3.0f);
        }
        (void)normal;
      }

      projectile_impulse += Length(projectile_force) * sub_dt;
      projectile_velocity += projectile_force * (sub_dt / kProjectileMass);
      projectile_center += projectile_velocity * sub_dt;
      for (Node& node : nodes) {
        node.velocity += node.force * (sub_dt / node.mass);
        node.position += node.velocity * sub_dt;
      }

      for (InterfaceFace& face : interfaces) {
        if (face.broken) continue;
        const Mat3 average = Scale(Add(tetras[static_cast<size_t>(face.left)].stress, tetras[static_cast<size_t>(face.right)].stress), 0.5f);
        const Vec3 traction = Mul(average, face.normal);
        const float normal_traction = std::max(Dot(traction, face.normal), 0.0f);
        const Vec3 shear = traction - face.normal * Dot(traction, face.normal);
        const float phi = std::pow(normal_traction / kTensileStrength, 2.0f) + std::pow(Length(shear) / kShearStrength, 2.0f);
        face.damage += sub_dt / kFractureTime * std::pow(std::max(phi - 1.0f, 0.0f), kDamageExponent);
      }
      if (!fractured && projectile_impulse > 35.0f && tick > 4u) TryFracture();
    }
    ++tick;
  }

  void BuildAdjacency() {
    std::map<FaceKey, std::vector<FaceOwner>> owners;
    for (size_t i = 0u; i < tetras.size(); ++i) {
      const auto faces = TetraFaces(tetras[i]);
      for (const auto& face : faces) owners[MakeFaceKey(face)].push_back({static_cast<int>(i), face});
    }
    for (const auto& pair : owners) {
      assert(pair.second.size() <= 2u);
      if (pair.second.size() == 1u) {
        boundaries.push_back({pair.second[0].tetra, pair.second[0].oriented});
      } else {
        InterfaceFace interface_face{};
        interface_face.left = pair.second[0].tetra;
        interface_face.right = pair.second[1].tetra;
        interface_face.oriented = pair.second[0].oriented;
        interface_face.normal = FaceNormal(nodes, interface_face.oriented);
        interface_face.area = FaceArea(nodes, interface_face.oriented);
        interfaces.push_back(interface_face);
      }
    }
    assert(!boundaries.empty());
    assert(!interfaces.empty());
  }

  void TryFracture() {
    const float band = kCellSize * kFractureCenterBand;
    std::vector<int> selected;
    for (size_t i = 0u; i < interfaces.size(); ++i) {
      InterfaceFace& face = interfaces[i];
      const Vec3 center = (
        nodes[static_cast<size_t>(face.oriented[0])].position +
        nodes[static_cast<size_t>(face.oriented[1])].position +
        nodes[static_cast<size_t>(face.oriented[2])].position) * (1.0f / 3.0f);
      const float axial_alignment = std::abs(face.normal.x);
      if (std::abs(center.x) < band && axial_alignment > 0.35f && face.damage > 0.0f) {
        selected.push_back(static_cast<int>(i));
      }
    }
    if (selected.empty()) return;
    std::vector<int> parent(tetras.size());
    for (size_t i = 0u; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    auto find = [&](int value) {
      int root = value;
      while (parent[static_cast<size_t>(root)] != root) root = parent[static_cast<size_t>(root)];
      while (parent[static_cast<size_t>(value)] != value) {
        const int next = parent[static_cast<size_t>(value)];
        parent[static_cast<size_t>(value)] = root;
        value = next;
      }
      return root;
    };
    auto unite = [&](int a, int b) {
      const int ra = find(a);
      const int rb = find(b);
      if (ra != rb) parent[static_cast<size_t>(rb)] = ra;
    };
    for (size_t i = 0u; i < interfaces.size(); ++i) {
      if (std::find(selected.begin(), selected.end(), static_cast<int>(i)) != selected.end()) continue;
      unite(interfaces[i].left, interfaces[i].right);
    }
    std::map<int, int> roots;
    for (size_t i = 0u; i < tetras.size(); ++i) roots.emplace(find(static_cast<int>(i)), 0);
    if (roots.size() < 2u) return;
    int next_component = 0;
    for (auto& root : roots) root.second = next_component++;
    fragments.resize(roots.size());
    for (size_t i = 0u; i < tetras.size(); ++i) {
      const int component = roots[find(static_cast<int>(i))];
      tetras[i].component = component;
      for (int node : tetras[i].node) fragments[static_cast<size_t>(component)].center += nodes[static_cast<size_t>(node)].position * (kDensity * tetras[i].volume / 4.0f);
      fragments[static_cast<size_t>(component)].mass += kDensity * tetras[i].volume;
    }
    for (Fragment& fragment : fragments) fragment.center = fragment.center * (1.0f / (fragment.mass * 4.0f));
    for (Tetra& tetra : tetras) {
      const Fragment& fragment = fragments[static_cast<size_t>(tetra.component)];
      Vec3 momentum{};
      for (int node : tetra.node) momentum += nodes[static_cast<size_t>(node)].velocity * (kDensity * tetra.volume / 4.0f);
      fragments[static_cast<size_t>(tetra.component)].velocity += momentum;
      for (size_t local = 0u; local < tetra.node.size(); ++local) tetra.fragment_local[local] = nodes[static_cast<size_t>(tetra.node[local])].position - fragment.center;
    }
    for (Fragment& fragment : fragments) fragment.velocity = fragment.velocity * (1.0f / fragment.mass);
    for (int index : selected) interfaces[static_cast<size_t>(index)].broken = true;
    fractured = true;
  }

  Vec3 PointForTetNode(const Tetra& tetra, size_t local) const {
    if (!fractured) return nodes[static_cast<size_t>(tetra.node[local])].position;
    const Fragment& fragment = fragments[static_cast<size_t>(tetra.component)];
    return fragment.center + tetra.fragment_local[local];
  }

  void AddRenderTriangle(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 color) {
    assert(render_triangles.size() < kMaxRenderTriangles);
    render_triangles.push_back({{p0, p1, p2}, color});
  }

  int BuildRenderBvhRecursive(const std::vector<int>& indices) {
    const int index = static_cast<int>(render_bvh.size());
    render_bvh.push_back({});
    Vec3 min_position{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    Vec3 max_position{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    Vec3 centroid_min = min_position;
    Vec3 centroid_max = max_position;
    for (int triangle_index : indices) {
      const RenderTriangle& triangle = render_triangles[static_cast<size_t>(triangle_index)];
      const Vec3 centroid = (triangle.p[0] + triangle.p[1] + triangle.p[2]) * (1.0f / 3.0f);
      min_position = Min(min_position, Min(triangle.p[0], Min(triangle.p[1], triangle.p[2])));
      max_position = Max(max_position, Max(triangle.p[0], Max(triangle.p[1], triangle.p[2])));
      centroid_min = Min(centroid_min, centroid);
      centroid_max = Max(centroid_max, centroid);
    }
    if (indices.size() == 1u) {
      render_bvh[static_cast<size_t>(index)] = {min_position, max_position, -1, -1, indices[0]};
      return index;
    }
    const Vec3 extent = centroid_max - centroid_min;
    int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
    std::vector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), [&](int left, int right) {
      const RenderTriangle& a = render_triangles[static_cast<size_t>(left)];
      const RenderTriangle& b = render_triangles[static_cast<size_t>(right)];
      const Vec3 ca = (a.p[0] + a.p[1] + a.p[2]) * (1.0f / 3.0f);
      const Vec3 cb = (b.p[0] + b.p[1] + b.p[2]) * (1.0f / 3.0f);
      return axis == 0 ? ca.x < cb.x : axis == 1 ? ca.y < cb.y : ca.z < cb.z;
    });
    const size_t middle = sorted.size() / 2u;
    const int left = BuildRenderBvhRecursive({sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(middle)});
    const int right = BuildRenderBvhRecursive({sorted.begin() + static_cast<std::ptrdiff_t>(middle), sorted.end()});
    render_bvh[static_cast<size_t>(index)] = {min_position, max_position, left, right, -1};
    return index;
  }

  void BuildRender() {
    render_triangles.clear();
    for (const BoundaryFace& boundary : boundaries) {
      const Tetra& tetra = tetras[static_cast<size_t>(boundary.tetra)];
      const auto find_local = [&](int node) {
        for (size_t i = 0u; i < tetra.node.size(); ++i) if (tetra.node[i] == node) return i;
        assert(false);
        return size_t{0u};
      };
      const Vec3 p0 = PointForTetNode(tetra, find_local(boundary.oriented[0]));
      const Vec3 p1 = PointForTetNode(tetra, find_local(boundary.oriented[1]));
      const Vec3 p2 = PointForTetNode(tetra, find_local(boundary.oriented[2]));
      const float heat = std::min(1.0f, tetra.stress_score);
      AddRenderTriangle(p0, p1, p2, {0.16f + heat * 0.8f, 0.36f - heat * 0.20f, 0.92f - heat * 0.55f});
    }
    if (fractured) {
      for (const InterfaceFace& face : interfaces) {
        if (!face.broken) continue;
        const Tetra& left = tetras[static_cast<size_t>(face.left)];
        const Tetra& right = tetras[static_cast<size_t>(face.right)];
        const auto left_local = [&](int node) { for (size_t i = 0u; i < left.node.size(); ++i) if (left.node[i] == node) return i; assert(false); return size_t{0u}; };
        const auto right_local = [&](int node) { for (size_t i = 0u; i < right.node.size(); ++i) if (right.node[i] == node) return i; assert(false); return size_t{0u}; };
        AddRenderTriangle(
          PointForTetNode(left, left_local(face.oriented[0])),
          PointForTetNode(left, left_local(face.oriented[1])),
          PointForTetNode(left, left_local(face.oriented[2])),
          {0.95f, 0.08f, 0.04f});
        AddRenderTriangle(
          PointForTetNode(right, right_local(face.oriented[2])),
          PointForTetNode(right, right_local(face.oriented[1])),
          PointForTetNode(right, right_local(face.oriented[0])),
          {0.95f, 0.08f, 0.04f});
      }
    }
    for (const SourceTriangle& triangle : source.triangles) {
      AddRenderTriangle(
        projectile_center + triangle.p[0] * (kProjectileRadius / 0.5f),
        projectile_center + triangle.p[1] * (kProjectileRadius / 0.5f),
        projectile_center + triangle.p[2] * (kProjectileRadius / 0.5f),
        {0.95f, 0.42f, 0.04f});
    }
    const Vec3 wall0{kBackingPlaneX, -0.62f, -1.0f};
    const Vec3 wall1{kBackingPlaneX, 0.62f, -1.0f};
    const Vec3 wall2{kBackingPlaneX, 0.62f, 1.0f};
    const Vec3 wall3{kBackingPlaneX, -0.62f, 1.0f};
    AddRenderTriangle(wall0, wall1, wall2, {0.34f, 0.36f, 0.40f});
    AddRenderTriangle(wall0, wall2, wall3, {0.34f, 0.36f, 0.40f});
    const Vec3 floor0{-1.6f, -0.62f, -1.3f};
    const Vec3 floor1{1.0f, -0.62f, -1.3f};
    const Vec3 floor2{1.0f, -0.62f, 1.3f};
    const Vec3 floor3{-1.6f, -0.62f, 1.3f};
    AddRenderTriangle(floor0, floor2, floor1, {0.12f, 0.14f, 0.17f});
    AddRenderTriangle(floor0, floor3, floor2, {0.12f, 0.14f, 0.17f});
    render_bvh.clear();
    std::vector<int> indices(render_triangles.size());
    for (size_t i = 0u; i < indices.size(); ++i) indices[i] = static_cast<int>(i);
    BuildRenderBvhRecursive(indices);
  }

  void Write(algorithm::AlgorithmContainerSet* set) {
    algorithm::AlgorithmContainer& frame = *RequireMutableContainer<uint32_t>(set, "frame_tick");
    algorithm::AlgorithmContainer& source_count = *RequireMutableContainer<uint32_t>(set, "source_triangle_count");
    algorithm::AlgorithmContainer& triangle_count = *RequireMutableContainer<uint32_t>(set, "render_triangle_count");
    algorithm::AlgorithmContainer& bvh_count = *RequireMutableContainer<uint32_t>(set, "render_bvh_count");
    algorithm::AlgorithmContainer& active_count = *RequireMutableContainer<uint32_t>(set, "active_tetra_count");
    algorithm::AlgorithmContainer& interface_count = *RequireMutableContainer<uint32_t>(set, "interface_face_count");
    algorithm::AlgorithmContainer& broken_count = *RequireMutableContainer<uint32_t>(set, "broken_face_count");
    algorithm::AlgorithmContainer& component_count = *RequireMutableContainer<uint32_t>(set, "component_count");
    algorithm::AlgorithmContainer& fracture_state = *RequireMutableContainer<uint32_t>(set, "fracture_state");
    algorithm::AlgorithmContainer& scene = *RequireMutableContainer<std::array<float, 4>>(set, "render_scene");
    algorithm::AlgorithmContainer& triangle_buffer = *RequireMutableContainer<std::array<float, 4>>(set, "render_triangle_buffer");
    algorithm::AlgorithmContainer& bvh_buffer = *RequireMutableContainer<std::array<float, 4>>(set, "render_bvh_buffer");
    WriteUint32(frame, tick);
    WriteUint32(source_count, static_cast<uint32_t>(source.triangles.size()));
    WriteUint32(triangle_count, static_cast<uint32_t>(render_triangles.size()));
    WriteUint32(bvh_count, static_cast<uint32_t>(render_bvh.size()));
    WriteUint32(active_count, static_cast<uint32_t>(tetras.size()));
    WriteUint32(interface_count, static_cast<uint32_t>(interfaces.size()));
    size_t broken = 0u;
    for (const InterfaceFace& face : interfaces) if (face.broken) ++broken;
    WriteUint32(broken_count, static_cast<uint32_t>(broken));
    WriteUint32(component_count, static_cast<uint32_t>(fractured ? fragments.size() : 1u));
    WriteUint32(fracture_state, fractured ? 1u : 0u);
    WriteVec4(scene, 0u, {0.0f, 0.0f, 0.0f}, 1.55f);
    for (size_t i = 0u; i < render_triangles.size(); ++i) {
      const RenderTriangle& triangle = render_triangles[i];
      WriteVec4(triangle_buffer, i * 4u + 0u, triangle.p[0], 1.0f);
      WriteVec4(triangle_buffer, i * 4u + 1u, triangle.p[1], 1.0f);
      WriteVec4(triangle_buffer, i * 4u + 2u, triangle.p[2], 1.0f);
      WriteVec4(triangle_buffer, i * 4u + 3u, triangle.color, 1.0f);
    }
    for (size_t i = 0u; i < render_bvh.size(); ++i) {
      const RenderBvhNode& node = render_bvh[i];
      WriteVec4(bvh_buffer, i * 2u + 0u, node.min_position, node.triangle >= 0 ? static_cast<float>(node.triangle) : static_cast<float>(node.left));
      WriteVec4(bvh_buffer, i * 2u + 1u, node.max_position, node.triangle >= 0 ? -1.0f : static_cast<float>(node.right));
    }
  }
};

class FractureProbeJobsExecutor final : public algomanager::bridge::IAlgorithmJobsExecutor {
 public:
  bool ExecuteJobsAlgorithm(
      const algomanager::bridge::AgentTickContext& context,
      const algorithm::AlgorithmProfile& algorithm_profile,
      const AgentToAlgorithmSignal& agent_to_algorithm_signal,
      algorithm::AlgorithmContainerSet* container_set,
      AlgorithmToAgentSignal* algorithm_to_agent_signal,
      algomanager::bridge::AlgorithmPackageDebugState* debug_state) override {
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    assert(container_set);
    if (!simulation_) {
      simulation_ = std::make_unique<Simulation>();
      simulation_->Build(container_set);
    }
    const float dt = context.dt_seconds > 0.0f ? context.dt_seconds : 1.0f / 60.0f;
    simulation_->Step(dt);
    simulation_->BuildRender();
    simulation_->Write(container_set);
    if (algorithm_to_agent_signal) *algorithm_to_agent_signal = {};
    if (debug_state) {
      size_t broken = 0u;
      for (const InterfaceFace& face : simulation_->interfaces) if (face.broken) ++broken;
      debug_state->signals.push_back({
        .name = "agent_adaptive_prism_sphere_fracture_probe.jobs",
        .payload = "tick=" + std::to_string(simulation_->tick) +
          ", tetra=" + std::to_string(simulation_->tetras.size()) +
          ", interfaces=" + std::to_string(simulation_->interfaces.size()) +
          ", render_triangles=" + std::to_string(simulation_->render_triangles.size()) +
          ", broken_faces=" + std::to_string(broken) +
          ", components=" + std::to_string(simulation_->fractured ? simulation_->fragments.size() : 1u),
      });
    }
    return true;
  }

 private:
  std::unique_ptr<Simulation> simulation_{};
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
  out_bundle->jobs_executor = new FractureProbeJobsExecutor();
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
