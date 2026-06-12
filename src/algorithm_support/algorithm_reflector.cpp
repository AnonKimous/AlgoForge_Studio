#include "algorithm_support/algorithm_protocol.h"

namespace algorithm_support {

bool CreateAlgorithmPackageReflectorByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmPackageSupport>* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "Reflector output pointer is null.";
    }
    return false;
  }
  (void)algorithm_name;

  out_reflector->reset();
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace algorithm_support
