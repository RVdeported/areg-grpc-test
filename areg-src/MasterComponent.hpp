#pragma once

#include "areg-src/InterfaceProviderBase.hpp"
#include "areg/component/Component.hpp"

#include "areg-src/Interface.hpp"
#include "common/utils.hpp"

#include <atomic>
#include <cstdint>
#include <vector>


class MasterComponent final : public areg::Component,
                              protected InterfaceProviderBase
{
public:
  MasterComponent(const areg::ComponentEntry & entry,
                  areg::ComponentThread & owner);

  // -- completion/status -------------------------------------------------
  bool all_tasks_completed() const;
  size_t enqueued() const { return mEnqueued.load(); }
  size_t completed() const { return mCompleted.load(); }


  void request_RegisterWorker() final;
  
  void request_SubmitTask(uint32_t worker_id, uint32_t task_id,
                          const Interface::DArray & result,
                          uint64_t ts_rec, uint64_t ts_tsk,
                          uint64_t ts_snd,
                          uint64_t ram_rss_kb) final;

  void request_AssignTask(uint32_t worker_id) final;

protected:
  void startup_service_interface(areg::Component & holder) override;

private:
  // -- worker counters ---------------------------------------------------
  std::atomic<uint32_t> mNextWorkerId{1};

  // -- task queue (mirrors gRPC Dispatcher) ------------------------------
  std::vector<common::Task> mQueue;
  std::vector<common::TaskRecord> mRes;
  std::atomic<size_t> mNextTask{0};
  std::atomic<size_t> mEnqueued{0};
  std::atomic<size_t> mCompleted{0};
  std::atomic<bool> mShutdown{false};

  void make_response(uint32_t worker_id);
private:
  // -- helper ------------------------------------------------------------
  inline MasterComponent & self() { return (*this); }

  MasterComponent() = delete;
  AREG_NOCOPY_NOMOVE(MasterComponent);
};
