#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "v2a0_teapot_pbr_demo_shared.h"

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle) {
  return v2a0_teapot_pbr_demo::detail::CreateBundle(request, out_bundle);
}
