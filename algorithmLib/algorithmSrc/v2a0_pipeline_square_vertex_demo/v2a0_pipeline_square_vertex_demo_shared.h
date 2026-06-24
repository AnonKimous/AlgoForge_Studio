#pragma once

#include "../algorithm_plugin_api.h"

namespace v2a0_pipeline_square_vertex_demo {

inline bool CreateBundle(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm_library_plugin::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->cpu_symbol = false;
  out_bundle->gpu_symbol = true;
  return true;
}

}  // namespace v2a0_pipeline_square_vertex_demo
