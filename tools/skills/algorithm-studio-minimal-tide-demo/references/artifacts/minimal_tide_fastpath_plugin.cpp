#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

namespace {

constexpr const char* kAlgorithmName = "minimal_tide_fastpath";

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithm_support::AlgorithmPluginRequest* request,
  algorithm_support::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }
  out_bundle->Clear();
  (void)kAlgorithmName;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithm_support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }
  (void)kAlgorithmName;
  return true;
}
