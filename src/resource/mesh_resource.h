#pragma once

#include "common_data/mesh.h"

#include <string>

namespace mesh_resource {

Mesh LoadMeshObjFile(const std::string& path);
void SaveMeshObjFile(const Mesh& mesh, const std::string& path);
void GenerateDefaultTriangleObjFile(const std::string& path);

}  // namespace mesh_resource

using mesh_resource::GenerateDefaultTriangleObjFile;
using mesh_resource::LoadMeshObjFile;
using mesh_resource::SaveMeshObjFile;
