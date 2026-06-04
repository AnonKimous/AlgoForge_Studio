#pragma once

#include "common_data/mesh.h"

#include <string>

namespace mesh_io {

Mesh LoadMeshObjFile(const std::string& path);
void SaveMeshObjFile(const Mesh& mesh, const std::string& path);
void GenerateDefaultTriangleObjFile(const std::string& path);

}  // namespace mesh_io

using mesh_io::GenerateDefaultTriangleObjFile;
using mesh_io::LoadMeshObjFile;
using mesh_io::SaveMeshObjFile;
