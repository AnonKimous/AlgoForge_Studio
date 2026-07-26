#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../agent_adaptive_prism_pipeline_probe_shared.h"

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algomanager::algocatalog::AlgorithmPluginBundle* out_bundle) {
  return agent_adaptive_prism_pipeline_probe::detail::CreateBundle(request, out_bundle);
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
    const algomanager::algocatalog::AlgorithmPluginRequest* request,
    algorithm::AlgorithmReflector* out_reflector) {
  return agent_adaptive_prism_pipeline_probe::detail::CreateRuntimeReflector(request, out_reflector);
}
