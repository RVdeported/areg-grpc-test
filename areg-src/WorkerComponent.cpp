#include "WorkerComponent.hpp"
#include "areg/appbase/Application.hpp"
#include "common/utils.hpp"

#include <cstdio>
#include <sched.h>

WorkerComponent::WorkerComponent(const areg::ComponentEntry & entry,
                                 areg::ComponentThread & owner)
    : areg::Component(entry, owner),
      InterfaceConsumerBase(entry.mDependencyServices[0].mRoleName.as_string(),
                            static_cast<areg::Component &>(self()))
{
}

// ---------------------------------------------------------------------------
// InterfaceConsumerBase overrides
// ---------------------------------------------------------------------------

bool WorkerComponent::service_connected(areg::ServiceConnectionState status,
                                        areg::ProxyBase & proxy)
{
  bool result{false};
  if (InterfaceConsumerBase::service_connected(status, proxy))
  {
    result = true;
    if (areg::is_service_connected(status))
    {
      std::printf("[worker] connected to master, registering\n");
      request_RegisterWorker();
    }
    else
    {
      std::printf("[worker] disconnected from master\n");
      areg::Application::signal_quit();
    }
  }
  return result;
}

void WorkerComponent::response_RegisterWorkerReply(uint32_t worker_id)
{
  mWorkerId = worker_id;
  request_AssignTask(mWorkerId);
}

void WorkerComponent::response_AssignTaskReply(uint32_t task_id,
                                               const Interface::DArray & arr_a,
                                               const Interface::DArray & arr_b,
                                               uint32_t rows_a, uint32_t cols_a,
                                               uint32_t cols_b)
{
  size_t ts_rec = common::ts();
  std::printf("[worker %u] received task assignment\n", mWorkerId);

  auto res =
      common::multiply(arr_a.data(), arr_b.data(), rows_a, cols_a, cols_b);
  size_t ts_tsk = common::ts();

  size_t ts_snd = common::ts();
  request_SubmitTask(mWorkerId, task_id, {res}, ts_rec, ts_tsk, ts_snd);
}
