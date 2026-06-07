#pragma once

// Optional convenience aggregator for the common-data layer.
// Unlike other layers, common_data may also be included as loose headers.

#define COMMON_DATA_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "common_data/io_packet.h"
#include "common_data/input_state.h"
#include "common_data/interaction/interaction_signals.h"
#include "common_data/mesh.h"
#include "common_data/vector_types.h"
#undef COMMON_DATA_LAYER_PUBLIC_FACADE_INCLUDE
