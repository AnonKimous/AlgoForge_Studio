#define FASTFLOAT_CONSTEXPR_FEATURE_DETECT_H
#define FASTFLOAT_CONSTEXPR14 constexpr
#define FASTFLOAT_CONSTEXPR20
#define FASTFLOAT_IF_CONSTEXPR17(x) if constexpr (x)
#define FASTFLOAT_HAS_BIT_CAST 0
#define FASTFLOAT_HAS_IS_CONSTANT_EVALUATED 0
#define FASTFLOAT_IS_CONSTEXPR 0
#define FASTFLOAT_DETAIL_MUST_DEFINE_CONSTEXPR_VARIABLE 0

#include "sidecar/mesh_io.h"

#include "common_data/mesh.h"

#include <tinyobjloader/tiny_obj_loader.h>

#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace mesh_io {

namespace {

struct VertexKey {
  int vertex_index{-1};
  int normal_index{-1};

  bool operator<(const VertexKey& other) const {
    if (vertex_index != other.vertex_index) {
      return vertex_index < other.vertex_index;
    }
    return normal_index < other.normal_index;
  }
};

Vec3 MakeVec3(float x, float y, float z) {
  return Vec3{x, y, z};
}

Vec3 Midpoint(const Vec3& a, const Vec3& b) {
  return Vec3{
    (a.x + b.x) * 0.5f,
    (a.y + b.y) * 0.5f,
    (a.z + b.z) * 0.5f,
  };
}

Vec3 Subtract(const Vec3& a, const Vec3& b) {
  return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Add(const Vec3& a, const Vec3& b) {
  return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
  return Vec3{
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x,
  };
}

float LengthSquared(const Vec3& v) {
  return v.x * v.x + v.y * v.y + v.z * v.z;
}

Vec3 NormalizeOrFallback(const Vec3& v, const Vec3& fallback = Vec3{0.0f, 0.0f, 1.0f}) {
  const float len_sq = LengthSquared(v);
  if (!(len_sq > 0.0f) || !std::isfinite(len_sq)) {
    return fallback;
  }
  const float inv_len = 1.0f / std::sqrt(len_sq);
  return Vec3{v.x * inv_len, v.y * inv_len, v.z * inv_len};
}

void ComputeVertexNormals(Mesh* mesh) {
  if (!mesh) return;
  mesh->normals.assign(mesh->positions.size(), Vec3{0.0f, 0.0f, 0.0f});
  if (mesh->positions.empty() || mesh->triangles.empty()) {
    for (Vec3& normal : mesh->normals) {
      normal = Vec3{0.0f, 0.0f, 1.0f};
    }
    return;
  }

  for (const auto& tri : mesh->triangles) {
    if (tri[0] >= mesh->positions.size() || tri[1] >= mesh->positions.size() || tri[2] >= mesh->positions.size()) {
      continue;
    }
    const Vec3& a = mesh->positions[tri[0]];
    const Vec3& b = mesh->positions[tri[1]];
    const Vec3& c = mesh->positions[tri[2]];
    const Vec3 face_normal = Cross(Subtract(b, a), Subtract(c, a));
    mesh->normals[tri[0]] = Add(mesh->normals[tri[0]], face_normal);
    mesh->normals[tri[1]] = Add(mesh->normals[tri[1]], face_normal);
    mesh->normals[tri[2]] = Add(mesh->normals[tri[2]], face_normal);
  }

  for (Vec3& normal : mesh->normals) {
    normal = NormalizeOrFallback(normal);
  }
}

Vec3 ReadVertexPosition(const tinyobj::attrib_t& attrib, int vertex_index) {
  if (vertex_index < 0) {
    throw std::runtime_error("OBJ vertex index is missing");
  }
  const size_t base = static_cast<size_t>(vertex_index) * 3u;
  if (base + 2u >= attrib.vertices.size()) {
    throw std::runtime_error("OBJ vertex index out of range");
  }
  return MakeVec3(
    attrib.vertices[base + 0u],
    attrib.vertices[base + 1u],
    attrib.vertices[base + 2u]);
}

Vec3 ReadVertexNormal(const tinyobj::attrib_t& attrib, int normal_index, bool* missing_normal) {
  if (normal_index < 0) {
    if (missing_normal) {
      *missing_normal = true;
    }
    return Vec3{0.0f, 0.0f, 1.0f};
  }
  const size_t base = static_cast<size_t>(normal_index) * 3u;
  if (base + 2u >= attrib.normals.size()) {
    if (missing_normal) {
      *missing_normal = true;
    }
    return Vec3{0.0f, 0.0f, 1.0f};
  }
  return NormalizeOrFallback(MakeVec3(
    attrib.normals[base + 0u],
    attrib.normals[base + 1u],
    attrib.normals[base + 2u]));
}

Mesh BuildMeshFromObjReader(const tinyobj::ObjReader& reader) {
  const tinyobj::attrib_t& attrib = reader.GetAttrib();
  const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

  Mesh mesh{};
  std::map<VertexKey, uint32_t> vertex_remap;
  bool missing_normals = attrib.normals.empty();

  for (const tinyobj::shape_t& shape : shapes) {
    size_t index_offset = 0;
    for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); ++face_index) {
      const int face_vertex_count = shape.mesh.num_face_vertices[face_index];
      if (face_vertex_count != 3) {
        throw std::runtime_error("OBJ face is not triangulated");
      }

      std::array<uint32_t, 3> triangle{};
      for (int corner = 0; corner < 3; ++corner) {
        const tinyobj::index_t& index = shape.mesh.indices[index_offset + static_cast<size_t>(corner)];
        const VertexKey key{index.vertex_index, index.normal_index};
        const auto found = vertex_remap.find(key);
        if (found != vertex_remap.end()) {
          triangle[static_cast<size_t>(corner)] = found->second;
          continue;
        }

        const uint32_t new_index = static_cast<uint32_t>(mesh.positions.size());
        mesh.positions.push_back(ReadVertexPosition(attrib, index.vertex_index));
        mesh.normals.push_back(ReadVertexNormal(attrib, index.normal_index, &missing_normals));
        vertex_remap.emplace(key, new_index);
        triangle[static_cast<size_t>(corner)] = new_index;
      }
      mesh.triangles.push_back(triangle);
      mesh.triangle_material_gpa.push_back(std::numeric_limits<float>::quiet_NaN());
      index_offset += static_cast<size_t>(face_vertex_count);
    }
  }

  if (missing_normals || mesh.normals.size() != mesh.positions.size()) {
    ComputeVertexNormals(&mesh);
  }

  RebuildEdges(mesh);
  NormalizeTriangleMaterials(mesh);
  return mesh;
}

Mesh BuildDefaultTriangleMesh() {
  const std::vector<Vec3> base = {
    {-0.110f, -0.090f, 0.0f},
    {0.110f, -0.090f, 0.0f},
    {0.000f, 0.100f, 0.0f},
  };

  Mesh mesh{};
  mesh.positions = base;
  mesh.positions.push_back(Midpoint(base[0], base[1]));
  mesh.positions.push_back(Midpoint(base[1], base[2]));
  mesh.positions.push_back(Midpoint(base[2], base[0]));
  mesh.normals.assign(mesh.positions.size(), Vec3{0.0f, 0.0f, 1.0f});
  mesh.triangles = {
    {0, 3, 5},
    {3, 1, 4},
    {5, 4, 2},
    {3, 4, 5},
  };
  mesh.triangle_material_gpa.assign(mesh.triangles.size(), std::numeric_limits<float>::quiet_NaN());
  RebuildEdges(mesh);
  NormalizeTriangleMaterials(mesh);
  return mesh;
}

Mesh PrepareMeshForObjExport(const Mesh& input_mesh) {
  Mesh mesh = input_mesh;
  if (mesh.positions.empty()) {
    return mesh;
  }
  if (mesh.normals.size() != mesh.positions.size()) {
    ComputeVertexNormals(&mesh);
  } else {
    for (Vec3& normal : mesh.normals) {
      normal = NormalizeOrFallback(normal);
    }
  }
  if (mesh.triangle_material_gpa.size() != mesh.triangles.size()) {
    NormalizeTriangleMaterials(mesh);
  }
  RebuildEdges(mesh);
  return mesh;
}

void WriteObjVertex(const Mesh& mesh, std::ofstream& file) {
  for (const Vec3& position : mesh.positions) {
    file << "v " << position.x << ' ' << position.y << ' ' << position.z << '\n';
  }
}

void WriteObjNormals(const Mesh& mesh, std::ofstream& file) {
  for (const Vec3& normal : mesh.normals) {
    file << "vn " << normal.x << ' ' << normal.y << ' ' << normal.z << '\n';
  }
}

void WriteObjFaces(const Mesh& mesh, std::ofstream& file) {
  for (const auto& triangle : mesh.triangles) {
    file << "f "
         << (triangle[0] + 1u) << "//" << (triangle[0] + 1u) << ' '
         << (triangle[1] + 1u) << "//" << (triangle[1] + 1u) << ' '
         << (triangle[2] + 1u) << "//" << (triangle[2] + 1u) << '\n';
  }
}

}  // namespace

Mesh LoadMeshObjFile(const std::string& path) {
  tinyobj::ObjReaderConfig config{};
  config.triangulate = true;
  config.vertex_color = false;

  tinyobj::ObjReader reader;
  if (!reader.ParseFromFile(path, config)) {
    const std::string warning = reader.Warning();
    const std::string error = reader.Error();
    std::string message = "Failed to read OBJ file: " + path;
    if (!error.empty()) {
      message += "\nError: " + error;
    }
    if (!warning.empty()) {
      message += "\nWarning: " + warning;
    }
    throw std::runtime_error(message);
  }

  return BuildMeshFromObjReader(reader);
}

void SaveMeshObjFile(const Mesh& input_mesh, const std::string& path) {
  Mesh mesh = PrepareMeshForObjExport(input_mesh);

  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to write OBJ file: " + path);
  }

  file << "# generated mesh\n";
  file << "o mesh\n";
  WriteObjVertex(mesh, file);
  WriteObjNormals(mesh, file);
  WriteObjFaces(mesh, file);
}

void GenerateDefaultTriangleObjFile(const std::string& path) {
  SaveMeshObjFile(BuildDefaultTriangleMesh(), path);
}

}  // namespace mesh_io
