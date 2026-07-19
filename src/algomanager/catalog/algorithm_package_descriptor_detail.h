#pragma once

#include "algomanager/bridge/algorithm_protocol.h"

#include <cstdint>
#include <string>

namespace algomanager { namespace algocatalog { namespace descriptor_detail {

bool WriteDescriptorScalarValue(
  algorithm::AlgorithmContainer* container,
  size_t byte_offset,
  uint32_t scalar_bits,
  const std::string& value_codec,
  double value,
  std::string* out_error_message);

} } }  // namespace algomanager::algocatalog::descriptor_detail
