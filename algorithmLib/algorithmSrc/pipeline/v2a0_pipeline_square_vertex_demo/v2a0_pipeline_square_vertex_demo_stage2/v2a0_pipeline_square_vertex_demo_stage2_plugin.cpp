#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../v2a0_pipeline_square_vertex_demo_shared.h"

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm_library_plugin::AlgorithmPluginBundle* out_bundle) {
  return v2a0_pipeline_square_vertex_demo::CreateBundle(request, out_bundle);
}
