#pragma once

#include "common_data/mesh.h"

#include <string>

namespace algomanager { namespace algocatalog {

common_data::Mesh LoadAssimpMeshFile(const std::string& path);

}  // namespace algocatalog
}  // namespace algomanager
