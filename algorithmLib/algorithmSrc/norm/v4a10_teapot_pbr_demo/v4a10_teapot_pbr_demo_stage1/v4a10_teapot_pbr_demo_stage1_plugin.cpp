#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../v4a10_teapot_pbr_demo_shared.h"

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle) {
  return v4a10_teapot_pbr_demo::detail::CreateBundle(request, out_bundle);
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  return v4a10_teapot_pbr_demo::detail::CreateRuntimeReflector(request, out_reflector);
}
