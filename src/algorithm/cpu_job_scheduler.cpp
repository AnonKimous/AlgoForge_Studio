#include "cpu_job_scheduler.h"

namespace algorithm {

void RunCpuJobsOnMainThread(uint32_t item_count, const CpuJobCallback& callback) {
  if (!callback || item_count == 0) {
    return;
  }

  callback(CpuJobRange{0u, item_count});
}

}  // namespace algorithm
