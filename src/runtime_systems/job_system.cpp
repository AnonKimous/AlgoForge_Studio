#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/job_system.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include "runtime_systems/algorithm_gpu_executor.h"

#include <algorithm>
#include <chrono>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace runtime_systems {

namespace {

[[noreturn]] void _AbortJobExecution(std::string message) {
  assert(false && "Job execution failed");
  throw std::runtime_error(std::move(message));
}

struct CpuJobCompletion {
  bool ok{false};
  std::string error_message;
};

struct CpuJobTask {
  algorithm_management::AlgorithmJobPriority priority{algorithm_management::AlgorithmJobPriority::High};
  std::shared_ptr<CpuJobCompletion> completion;
  std::shared_ptr<std::promise<void>> done;
  std::function<void(CpuJobCompletion&)> body;
};

struct CpuWorkerQueues {
  std::deque<CpuJobTask> high;
  std::deque<CpuJobTask> normal;
  std::deque<CpuJobTask> low;
};

thread_local int g_current_cpu_worker_index = -1;

class CpuJobSystem {
 public:
  static CpuJobSystem& Instance() {
    static CpuJobSystem instance{};
    return instance;
  }

  bool Init(size_t worker_count) {
    if (worker_count == 0u) {
      _AbortJobExecution("CPU job system requires at least one worker thread.");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
      if (worker_count_ != worker_count) {
        _AbortJobExecution("CPU job system was already initialized with a different worker count.");
      }
      return true;
    }

    try {
      worker_count_ = worker_count;
      workers_.resize(worker_count_);
      queues_.resize(worker_count_);
      stop_requested_ = false;
      for (size_t i = 0; i < worker_count_; ++i) {
        workers_[i] = std::thread([this, i]() {
          _WorkerLoop(i);
        });
      }
      initialized_ = true;
      return true;
    } catch (...) {
      stop_requested_ = true;
      cv_.notify_all();
      for (std::thread& worker : workers_) {
        if (worker.joinable()) {
          worker.join();
        }
      }
      workers_.clear();
      queues_.clear();
      worker_count_ = 0u;
      stop_requested_ = false;
      initialized_ = false;
      throw;
    }
  }

  void Shutdown() {
    std::vector<std::thread> workers_to_join;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) {
        return;
      }
      stop_requested_ = true;
      cv_.notify_all();
      workers_to_join = std::move(workers_);
      queues_.clear();
      worker_count_ = 0u;
      initialized_ = false;
    }

    for (std::thread& worker : workers_to_join) {
      if (worker.joinable()) {
        worker.join();
      }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    stop_requested_ = false;
  }

  bool Initialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
  }

  bool SubmitBlocking(
    algorithm_management::AlgorithmJobPriority priority,
    std::function<void(CpuJobCompletion&)> body,
    std::string* out_error_message) {
    auto completion = std::make_shared<CpuJobCompletion>();
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> future = done->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) {
        if (out_error_message) {
          *out_error_message = "CPU job system is not initialized.";
        }
        _AbortJobExecution("CPU job system is not initialized.");
      }

      const size_t target_worker_index = _SelectWorkerUnlocked(priority);
      CpuJobTask task{};
      task.priority = priority;
      task.completion = completion;
      task.done = done;
      task.body = std::move(body);
      _QueueForPriority(queues_[target_worker_index], priority).push_back(std::move(task));
    }

    cv_.notify_all();

    if (g_current_cpu_worker_index >= 0) {
      while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (!_TryExecuteOneTask(static_cast<size_t>(g_current_cpu_worker_index))) {
          std::this_thread::yield();
        }
      }
    } else {
      future.wait();
    }

    future.get();
    if (!completion->ok) {
      if (out_error_message) {
        *out_error_message = completion->error_message.empty()
          ? "CPU job execution failed."
          : std::move(completion->error_message);
      } else {
        _AbortJobExecution(
          completion->error_message.empty()
            ? "CPU job execution failed."
            : std::move(completion->error_message));
      }
      return false;
    }

    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

 private:
  CpuJobSystem() = default;
  ~CpuJobSystem() {
    Shutdown();
  }

  static std::deque<CpuJobTask>& _QueueForPriority(
    CpuWorkerQueues& queues,
    algorithm_management::AlgorithmJobPriority priority) {
    switch (priority) {
      case algorithm_management::AlgorithmJobPriority::High:
        return queues.high;
      case algorithm_management::AlgorithmJobPriority::Normal:
        return queues.normal;
      case algorithm_management::AlgorithmJobPriority::Low:
        return queues.low;
    }
    return queues.high;
  }

  static const std::deque<CpuJobTask>& _QueueForPriority(
    const CpuWorkerQueues& queues,
    algorithm_management::AlgorithmJobPriority priority) {
    switch (priority) {
      case algorithm_management::AlgorithmJobPriority::High:
        return queues.high;
      case algorithm_management::AlgorithmJobPriority::Normal:
        return queues.normal;
      case algorithm_management::AlgorithmJobPriority::Low:
        return queues.low;
    }
    return queues.high;
  }

  static bool _StealFromQueue(
    std::deque<CpuJobTask>& queue,
    CpuJobTask* out_task) {
    if (!out_task || queue.size() < 2u) {
      return false;
    }

    auto it = queue.begin();
    ++it;
    *out_task = std::move(*it);
    queue.erase(it);
    return true;
  }

  bool _HasWorkUnlocked() const {
    for (const CpuWorkerQueues& queues : queues_) {
      if (!queues.high.empty() || !queues.normal.empty() || !queues.low.empty()) {
        return true;
      }
    }
    return false;
  }

  size_t _QueueWeight(const CpuWorkerQueues& queues) const {
    return queues.high.size() * 9u + queues.normal.size() * 3u + queues.low.size();
  }

  size_t _SelectWorkerUnlocked(algorithm_management::AlgorithmJobPriority priority) const {
    if (workers_.empty()) {
      return 0u;
    }

    size_t best_worker_index = 0u;
    size_t best_weight = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < queues_.size(); ++i) {
      const size_t weight = _QueueWeight(queues_[i]);
      if (weight < best_weight) {
        best_weight = weight;
        best_worker_index = i;
      }
    }

    (void)priority;
    return best_worker_index;
  }

  bool _TryPopLocalTaskLocked(
    size_t worker_index,
    CpuJobTask* out_task) {
    if (!out_task || worker_index >= queues_.size()) {
      return false;
    }

    CpuWorkerQueues& queues = queues_[worker_index];
    if (!queues.high.empty()) {
      *out_task = std::move(queues.high.front());
      queues.high.pop_front();
      return true;
    }
    if (!queues.normal.empty()) {
      *out_task = std::move(queues.normal.front());
      queues.normal.pop_front();
      return true;
    }
    if (!queues.low.empty()) {
      *out_task = std::move(queues.low.front());
      queues.low.pop_front();
      return true;
    }
    return false;
  }

  bool _TryStealTaskLocked(
    size_t thief_index,
    CpuJobTask* out_task) {
    if (!out_task || queues_.empty()) {
      return false;
    }

    for (size_t offset = 1u; offset < queues_.size(); ++offset) {
      const size_t victim_index = (thief_index + offset) % queues_.size();
      CpuWorkerQueues& victim = queues_[victim_index];
      if (_StealFromQueue(victim.high, out_task)) {
        return true;
      }
      if (_StealFromQueue(victim.normal, out_task)) {
        return true;
      }
      if (_StealFromQueue(victim.low, out_task)) {
        return true;
      }
    }

    return false;
  }

  bool _TryAcquireTaskLocked(
    int worker_index,
    CpuJobTask* out_task) {
    if (worker_index >= 0) {
      if (_TryPopLocalTaskLocked(static_cast<size_t>(worker_index), out_task)) {
        return true;
      }
      return _TryStealTaskLocked(static_cast<size_t>(worker_index), out_task);
    }

    for (size_t i = 0; i < queues_.size(); ++i) {
      if (_TryPopLocalTaskLocked(i, out_task)) {
        return true;
      }
    }
    return _TryStealTaskLocked(0u, out_task);
  }

  bool _TryExecuteOneTask(size_t worker_index) {
    CpuJobTask task{};
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!initialized_) {
        return false;
      }
      if (!_TryAcquireTaskLocked(static_cast<int>(worker_index), &task)) {
        return false;
      }
    }

    try {
      if (task.body && task.completion) {
        task.body(*task.completion);
      } else if (task.completion) {
        task.completion->ok = false;
        task.completion->error_message = "CPU job task body is missing.";
      }
    } catch (const std::exception& ex) {
      if (task.completion) {
        task.completion->ok = false;
        task.completion->error_message = ex.what();
      }
    } catch (...) {
      if (task.completion) {
        task.completion->ok = false;
        task.completion->error_message = "CPU job task failed with an unknown error.";
      }
    }

    if (task.done) {
      task.done->set_value();
    }
    return true;
  }

  void _WorkerLoop(size_t worker_index) {
    g_current_cpu_worker_index = static_cast<int>(worker_index);
    while (true) {
      CpuJobTask task{};
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() {
          return stop_requested_ || _HasWorkUnlocked();
        });
        if (stop_requested_ && !_HasWorkUnlocked()) {
          break;
        }
        if (!_TryAcquireTaskLocked(static_cast<int>(worker_index), &task)) {
          continue;
        }
      }

      try {
        if (task.body && task.completion) {
          task.body(*task.completion);
        } else if (task.completion) {
          task.completion->ok = false;
          task.completion->error_message = "CPU job task body is missing.";
        }
      } catch (const std::exception& ex) {
        if (task.completion) {
          task.completion->ok = false;
          task.completion->error_message = ex.what();
        }
      } catch (...) {
        if (task.completion) {
          task.completion->ok = false;
          task.completion->error_message = "CPU job task failed with an unknown error.";
        }
      }

      if (task.done) {
        task.done->set_value();
      }
    }
    g_current_cpu_worker_index = -1;
  }

  mutable std::mutex mutex_{};
  std::condition_variable cv_{};
  bool stop_requested_{false};
  bool initialized_{false};
  size_t worker_count_{0u};
  std::vector<CpuWorkerQueues> queues_{};
  std::vector<std::thread> workers_{};
};

}  // namespace

bool InitializeJobSystem(size_t worker_count) {
  return CpuJobSystem::Instance().Init(worker_count);
}

void ShutdownJobSystem() {
  CpuJobSystem::Instance().Shutdown();
}

bool IsJobSystemInitialized() {
  return CpuJobSystem::Instance().Initialized();
}

namespace job_cpu {

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  const algorithm_management::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  algorithm_management::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set) {
    if (out_error_message) {
      *out_error_message = "AlgorithmContainerSet pointer is null.";
    }
    _AbortJobExecution("AlgorithmContainerSet pointer is null.");
  }
  if (!object.temporaryTest_main_thread_executor) {
    if (out_error_message) {
      *out_error_message = "CPU job executor is unavailable.";
    }
    _AbortJobExecution("CPU job executor is unavailable.");
  }

  auto body = [
    &object,
    &context,
    &agent_to_algorithm_signal,
    container_set,
    out_algorithm_to_agent_signal,
    out_debug_state](CpuJobCompletion& completion) {
    completion.ok = object.temporaryTest_main_thread_executor->temporaryTestExecuteOnMainThread(
      context,
      object.algorithm_profile,
      agent_to_algorithm_signal,
      container_set,
      out_algorithm_to_agent_signal,
      out_debug_state);
    if (!completion.ok && completion.error_message.empty()) {
      completion.error_message = "CPU algorithm execution failed.";
    }
  };

  return CpuJobSystem::Instance().SubmitBlocking(
    context.job_priority,
    std::move(body),
    out_error_message);
}

}  // namespace job_cpu

namespace job_gpu {

bool Execute(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  const algorithm_management::AgentTickContext& context,
  std::string* out_error_message) {
  return TryExecuteGpuTick(object, container_set, context, out_error_message);
}

bool Synchronize(
  const algorithm_management::AlgorithmObject& object,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return TrySynchronizeGpuTickState(object, container_set, out_error_message);
}

}  // namespace job_gpu

}  // namespace runtime_systems
