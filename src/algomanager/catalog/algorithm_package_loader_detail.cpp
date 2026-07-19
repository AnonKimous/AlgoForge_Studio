#include "algomanager/catalog/algorithm_package_loader_detail.h"
#include "algomanager/bridge/algorithm_library_paths.h"
#include <filesystem>
#include <fstream>
#include <system_error>
namespace algomanager { namespace algocatalog { namespace loader_detail {
namespace fs = std::filesystem;
bool ShouldEmitPipelineRunnerProbe(const std::string& algorithm_name) {
  return algorithm_name.find("v4a10_teapot_pbr_demo") != std::string::npos ||
    algorithm_name.find("v4a16_fireworks_pipeline_demo") != std::string::npos;
}
void AppendPipelineRunnerProbe(const std::string& file_name, const std::string& line) {
  const fs::path path = algorithm::library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() /
    file_name;
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\\n';
  }
}
} } }  // namespace algomanager::algocatalog::loader_detail
