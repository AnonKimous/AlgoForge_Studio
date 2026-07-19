#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../v4a16_fireworks_pipeline_demo_shared.h"

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algomanager::algocatalog::AlgorithmPluginBundle* out_bundle) {
  return v4a16_fireworks_pipeline_demo::CreateBundle(request, out_bundle);
}
