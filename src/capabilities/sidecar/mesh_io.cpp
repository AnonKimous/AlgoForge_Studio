#define FASTFLOAT_CONSTEXPR_FEATURE_DETECT_H
#define FASTFLOAT_CONSTEXPR14 constexpr
#define FASTFLOAT_CONSTEXPR20
#define FASTFLOAT_IF_CONSTEXPR17(x) if constexpr (x)
#define FASTFLOAT_HAS_BIT_CAST 0
#define FASTFLOAT_HAS_IS_CONSTANT_EVALUATED 0
#define FASTFLOAT_IS_CONSTEXPR 0
#define FASTFLOAT_DETAIL_MUST_DEFINE_CONSTEXPR_VARIABLE 0

#include "capabilities/sidecar/mesh_io.h"

#include "algorithm_catalog/algorithm_library_paths.h"
#include "common_data/common_data.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace mesh_io {

namespace {

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

Vec2 ReadVertexUv(const aiMesh& mesh, unsigned int vertex_index) {
  if (mesh.HasTextureCoords(0) && mesh.mTextureCoords[0]) {
    const aiVector3D& uv = mesh.mTextureCoords[0][vertex_index];
    return Vec2{uv.x, uv.y};
  }
  return Vec2{0.0f, 0.0f};
}

void ComputeVertexNormals(Mesh* mesh) {
  mesh->normals.assign(mesh->positions.size(), Vec3{0.0f, 0.0f, 0.0f});
  if (mesh->positions.empty() || mesh->triangles.empty()) {
    for (Vec3& normal : mesh->normals) {
      normal = Vec3{0.0f, 0.0f, 1.0f};
    }
    return;
  }

  for (const auto& tri : mesh->triangles) {
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

template <typename ProbeFn>
Mesh BuildMeshFromAssimpScene(const aiScene& scene, const ProbeFn& append_probe) {
  Mesh mesh{};
  bool missing_normals = false;

  append_probe(
    "build.begin meshes=" + std::to_string(scene.mNumMeshes) +
    " materials=" + std::to_string(scene.mNumMaterials));
  mesh.positions.reserve(scene.mNumMeshes * 3u);
  mesh.normals.reserve(scene.mNumMeshes * 3u);
  mesh.triangles.reserve(scene.mNumMeshes);
  mesh.triangle_material_gpa.reserve(scene.mNumMeshes);

  for (unsigned int mesh_index = 0; mesh_index < scene.mNumMeshes; ++mesh_index) {
    const aiMesh* assimp_mesh = scene.mMeshes[mesh_index];
    if (!assimp_mesh) {
      throw std::runtime_error("Assimp scene mesh pointer is null");
    }

    append_probe(
      "build.mesh.begin index=" + std::to_string(mesh_index) +
      " vertices=" + std::to_string(assimp_mesh->mNumVertices) +
      " faces=" + std::to_string(assimp_mesh->mNumFaces) +
      " normals=" + std::string(assimp_mesh->HasNormals() ? "true" : "false"));
    const uint32_t vertex_base = static_cast<uint32_t>(mesh.positions.size());
    if (!assimp_mesh->HasNormals()) {
      missing_normals = true;
    }

    for (unsigned int i = 0; i < assimp_mesh->mNumVertices; ++i) {
      const aiVector3D& position = assimp_mesh->mVertices[i];
      mesh.positions.push_back(MakeVec3(position.x, position.y, position.z));
      if (assimp_mesh->HasNormals()) {
        const aiVector3D& normal = assimp_mesh->mNormals[i];
        mesh.normals.push_back(NormalizeOrFallback(MakeVec3(normal.x, normal.y, normal.z)));
      } else {
        mesh.normals.push_back(Vec3{0.0f, 0.0f, 1.0f});
      }
      mesh.uvs.push_back(ReadVertexUv(*assimp_mesh, i));
    }
    append_probe(
      "build.mesh.vertices.end index=" + std::to_string(mesh_index) +
      " positions=" + std::to_string(mesh.positions.size()) +
      " normals=" + std::to_string(mesh.normals.size()) +
      " uvs=" + std::to_string(mesh.uvs.size()));

    for (unsigned int i = 0; i < assimp_mesh->mNumFaces; ++i) {
      const aiFace& face = assimp_mesh->mFaces[i];
      if (face.mNumIndices != 3u) {
        throw std::runtime_error("Imported mesh face is not triangulated");
      }

      mesh.triangles.push_back(std::array<uint32_t, 3>{
        vertex_base + static_cast<uint32_t>(face.mIndices[0]),
        vertex_base + static_cast<uint32_t>(face.mIndices[1]),
        vertex_base + static_cast<uint32_t>(face.mIndices[2]),
      });
      mesh.triangle_material_gpa.push_back(std::numeric_limits<float>::quiet_NaN());
    }
    append_probe(
      "build.mesh.faces.end index=" + std::to_string(mesh_index) +
      " triangles=" + std::to_string(mesh.triangles.size()) +
      " triangle_material_gpa=" + std::to_string(mesh.triangle_material_gpa.size()));
  }

  append_probe(
    "build.before_normals missing_normals=" + std::string(missing_normals ? "true" : "false") +
    " positions=" + std::to_string(mesh.positions.size()) +
    " normals=" + std::to_string(mesh.normals.size()) +
    " triangles=" + std::to_string(mesh.triangles.size()));
  if (missing_normals || mesh.normals.size() != mesh.positions.size()) {
    ComputeVertexNormals(&mesh);
  } else {
    for (Vec3& normal : mesh.normals) {
      normal = NormalizeOrFallback(normal);
    }
  }
  append_probe(
    "build.after_normals positions=" + std::to_string(mesh.positions.size()) +
    " normals=" + std::to_string(mesh.normals.size()));

  append_probe("build.before_rebuild_edges");
  RebuildEdges(mesh);
  append_probe(
    "build.after_rebuild_edges edges=" + std::to_string(mesh.edges.size()) +
    " positions=" + std::to_string(mesh.positions.size()) +
    " triangles=" + std::to_string(mesh.triangles.size()));
  append_probe("build.before_normalize_triangle_materials");
  NormalizeTriangleMaterials(mesh);
  append_probe(
    "build.after_normalize_triangle_materials triangle_material_gpa=" +
    std::to_string(mesh.triangle_material_gpa.size()));
  append_probe("build.end");
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
  if (mesh.uvs.size() != mesh.positions.size()) {
    mesh.uvs.assign(mesh.positions.size(), Vec2{0.0f, 0.0f});
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

void WriteObjUvs(const Mesh& mesh, std::ofstream& file) {
  for (const Vec2& uv : mesh.uvs) {
    file << "vt " << uv.x << ' ' << uv.y << '\n';
  }
}

void WriteObjFaces(const Mesh& mesh, std::ofstream& file) {
  const bool has_uvs = mesh.uvs.size() == mesh.positions.size();
  for (const auto& triangle : mesh.triangles) {
    file << "f ";
    for (size_t i = 0; i < 3u; ++i) {
      const uint32_t index = triangle[i] + 1u;
      file << index << '/';
      if (has_uvs) {
        file << index;
      }
      file << '/' << index;
      if (i + 1u < 3u) {
        file << ' ';
      }
    }
    file << '\n';
  }
}

}  // namespace

Mesh LoadMeshFile(const std::string& path) {
  const auto append_probe = [&](const std::string& line) {
    const std::filesystem::path probe_path =
      algorithm::library_paths::ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot() / "mesh_io_probe.log";
    std::error_code ec;
    std::filesystem::create_directories(probe_path.parent_path(), ec);
    std::ofstream file(probe_path, std::ios::binary | std::ios::app);
    if (file) {
      file << line << '\n';
    }
  };
  append_probe("load.begin path=" + path);
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
    path,
    aiProcess_Triangulate |
      aiProcess_JoinIdenticalVertices);
  append_probe("load.after_read_file path=" + path + " scene=" + std::string(scene ? "true" : "false"));
  if (!scene) {
    throw std::runtime_error("Failed to read mesh file: " + path + "\nError: " + importer.GetErrorString());
  }
  if (!scene->HasMeshes()) {
    throw std::runtime_error("Mesh file does not contain any mesh data: " + path);
  }

  Mesh mesh = BuildMeshFromAssimpScene(*scene, append_probe);
  append_probe("load.after_build path=" + path);
  return mesh;
}

Mesh LoadMeshObjFile(const std::string& path) {
  return LoadMeshFile(path);
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
  if (mesh.uvs.size() == mesh.positions.size()) {
    WriteObjUvs(mesh, file);
  }
  WriteObjNormals(mesh, file);
  WriteObjFaces(mesh, file);
}

void GenerateDefaultTriangleObjFile(const std::string& path) {
  SaveMeshObjFile(common_data::BuildDefaultTriangleMesh(), path);
}

}  // namespace mesh_io
