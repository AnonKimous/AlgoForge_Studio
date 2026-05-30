#pragma once

#include <cstdint>
#include <functional>

namespace algorithm {

struct CpuJobRange {
  uint32_t begin{};
  uint32_t end{};
};

using CpuJobCallback = std::function<void(CpuJobRange range)>;

void RunCpuJobsOnMainThread(uint32_t item_count, const CpuJobCallback& callback);

}  // namespace algorithm

using algorithm::CpuJobCallback;
using algorithm::CpuJobRange;
using algorithm::RunCpuJobsOnMainThread;
