#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "v4a10_teapot_pbr_demo_shared.h"

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algomanager::algocatalog::AlgorithmPluginBundle* out_bundle) {
  return v4a10_teapot_pbr_demo::detail::CreateBundle(request, out_bundle);
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  return v4a10_teapot_pbr_demo::detail::CreateRuntimeReflector(request, out_reflector);
}
