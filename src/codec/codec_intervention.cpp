#include "codec/codec_protocol.h"

namespace codec {

bool CreateAlgorithmInterventionByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message) {
  if (!out_intervention) {
    if (out_error_message) {
      *out_error_message = "Intervention output pointer is null.";
    }
    return false;
  }
  (void)algorithm_name;

  out_intervention->reset();
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace codec
