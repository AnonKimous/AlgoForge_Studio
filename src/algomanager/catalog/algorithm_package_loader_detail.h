#pragma once
#include <string>
namespace algomanager { namespace algocatalog { namespace loader_detail {
bool ShouldEmitPipelineRunnerProbe(const std::string& algorithm_name);
void AppendPipelineRunnerProbe(const std::string& file_name, const std::string& line);
} } }  // namespace algomanager::algocatalog::loader_detail
