#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace algomanager { namespace algocatalog { namespace package_precision_detail {

inline bool TryParseScalarPrecisionBits(std::string_view text, uint32_t* out_bits) {
  if (!out_bits) {
    return false;
  }
  if (text == "8" || text == "i8" || text == "u8") {
    *out_bits = 8u;
    return true;
  }
  if (text == "16" || text == "i16" || text == "u16" || text == "fp16" || text == "float16") {
    *out_bits = 16u;
    return true;
  }
  if (text == "32" || text == "i32" || text == "u32" || text == "fp32" || text == "float32" || text == "float") {
    *out_bits = 32u;
    return true;
  }
  if (text == "64" || text == "i64" || text == "u64" || text == "fp64" || text == "float64" || text == "double") {
    *out_bits = 64u;
    return true;
  }
  return false;
}

inline bool ParseScalarPrecisionBits(
  std::string_view text,
  const std::string& package_path,
  uint32_t* out_bits,
  std::string* out_error_message) {
  if (TryParseScalarPrecisionBits(text, out_bits)) {
    return true;
  }
  if (out_error_message) {
    *out_error_message =
      "Unsupported precision '" + std::string(text) + "' in package JSON: " + package_path;
  }
  return false;
}

} } }  // namespace algomanager::algocatalog::package_precision_detail
