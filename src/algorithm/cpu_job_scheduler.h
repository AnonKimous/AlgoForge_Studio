#pragma once

#include <cstdint>
#include <functional>

struct CpuJobRange {
  uint32_t begin{};
  uint32_t end{};
};

using CpuJobCallback = std::function<void(CpuJobRange range)>;

void RunCpuJobsOnMainThread(uint32_t item_count, const CpuJobCallback& callback);
