#include "algomanager/catalog/algorithm_package_descriptor_detail.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace algomanager { namespace algocatalog { namespace descriptor_detail {

namespace {

void SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

bool ValidateIntegerScalarValue(
  double value,
  const std::string& codec_name,
  const std::string& container_name,
  std::string* out_error_message) {
  if (!std::isfinite(value) || std::trunc(value) != value) {
    SetErrorMessage(
      out_error_message,
      "Descriptor value for container '" + container_name + "' is not an exact " + codec_name + " scalar.");
    return false;
  }
  return true;
}

}  // namespace

bool WriteDescriptorScalarValue(
  algorithm::AlgorithmContainer* container,
  size_t byte_offset,
  uint32_t scalar_bits,
  const std::string& value_codec,
  double value,
  std::string* out_error_message) {
  if (!container) {
    SetErrorMessage(out_error_message, "Descriptor write target container is null.");
    return false;
  }
  const size_t scalar_bytes = static_cast<size_t>(scalar_bits / 8u);
  if (scalar_bytes == 0u || byte_offset + scalar_bytes > container->bytes.size()) {
    SetErrorMessage(out_error_message, "Descriptor write exceeds target container storage.");
    return false;
  }

  void* destination = container->bytes.data() + byte_offset;
  if (value_codec == "float" || value_codec == "ieee754") {
    if (scalar_bits == 32u) {
      const float encoded = static_cast<float>(value);
      std::memcpy(destination, &encoded, sizeof(encoded));
      return true;
    }
    if (scalar_bits == 64u) {
      const double encoded = value;
      std::memcpy(destination, &encoded, sizeof(encoded));
      return true;
    }
    SetErrorMessage(out_error_message, "Float descriptor codec requires 32-bit or 64-bit target storage.");
    return false;
  }

  if (value_codec != "int" && value_codec != "uint") {
    SetErrorMessage(out_error_message, "Unsupported descriptor codec '" + value_codec + "'.");
    return false;
  }
  if (!ValidateIntegerScalarValue(
        value,
        value_codec == "int" ? "signed integer" : "unsigned integer",
        container->name,
        out_error_message)) {
    return false;
  }
  if (value_codec == "uint" && value < 0.0) {
    SetErrorMessage(out_error_message, "Unsigned descriptor value cannot be negative.");
    return false;
  }

  const double integral_value = value;
  if (value_codec == "int") {
    switch (scalar_bits) {
      case 8u: {
        if (integral_value < std::numeric_limits<int8_t>::min() || integral_value > std::numeric_limits<int8_t>::max()) return false;
        const int8_t encoded = static_cast<int8_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
      }
      case 16u: {
        if (integral_value < std::numeric_limits<int16_t>::min() || integral_value > std::numeric_limits<int16_t>::max()) return false;
        const int16_t encoded = static_cast<int16_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
      }
      case 32u: {
        if (integral_value < std::numeric_limits<int32_t>::min() || integral_value > std::numeric_limits<int32_t>::max()) return false;
        const int32_t encoded = static_cast<int32_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
      }
      case 64u: {
        if (integral_value < static_cast<double>(std::numeric_limits<int64_t>::min()) || integral_value > static_cast<double>(std::numeric_limits<int64_t>::max())) return false;
        const int64_t encoded = static_cast<int64_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
      }
      default: SetErrorMessage(out_error_message, "Signed integer descriptor codec only supports 8/16/32/64-bit targets."); return false;
    }
  }

  switch (scalar_bits) {
    case 8u: {
      if (integral_value > std::numeric_limits<uint8_t>::max()) return false;
      const uint8_t encoded = static_cast<uint8_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
    }
    case 16u: {
      if (integral_value > std::numeric_limits<uint16_t>::max()) return false;
      const uint16_t encoded = static_cast<uint16_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
    }
    case 32u: {
      if (integral_value > std::numeric_limits<uint32_t>::max()) return false;
      const uint32_t encoded = static_cast<uint32_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
    }
    case 64u: {
      if (integral_value > static_cast<double>(std::numeric_limits<uint64_t>::max())) return false;
      const uint64_t encoded = static_cast<uint64_t>(value); std::memcpy(destination, &encoded, sizeof(encoded)); return true;
    }
    default: SetErrorMessage(out_error_message, "Unsigned integer descriptor codec only supports 8/16/32/64-bit targets."); return false;
  }
}

} } }  // namespace algomanager::algocatalog::descriptor_detail
