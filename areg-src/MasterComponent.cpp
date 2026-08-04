#include "MasterComponent.hpp"
#include "areg/appbase/Application.hpp"

#include "areg-src/Interface.hpp"
#include "common/utils.hpp"

#include <cstdio>
#include <vector>

// -- declared in server.cpp ----------------------------------------
extern std::vector<common::Task> gMasterTasks;

MasterComponent::MasterComponent(const areg::ComponentEntry & entry,
                                 areg::ComponentThread & owner)
    : areg::Component(entry, owner),
      InterfaceProviderBase(static_cast<areg::Component &>(self()))
{
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

bool MasterComponent::all_tasks_completed() const
{
  return mCompleted.load(std::memory_order_relaxed) >= mQueue.size() ||
         mShutdown.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// InterfaceProviderBase overrides
// ---------------------------------------------------------------------------

void MasterComponent::startup_service_interface(areg::Component & holder)
{
  InterfaceProviderBase::startup_service_interface(holder);
  // Consume pre-loaded tasks from the static pool.
  for (auto & t : gMasterTasks)
  {
    mQueue.push_back(std::move(t));
    mRes.emplace_back();
  }
  gMasterTasks.clear();

  mEnqueued.store(mQueue.size(), std::memory_order_relaxed);
  std::printf("[master] enqueued %zu tasks\n", mQueue.size());
}

void MasterComponent::make_response(uint32_t worker_id)
{
  auto idx = mNextTask.fetch_add(1, std::memory_order_relaxed);

  if (idx < mQueue.size()) [[likely]]
  {
    auto task = mQueue[idx];
    std::printf("[master] dispatching task %zu\n", idx);
    
    // XXX: Do not care here about excessive copying
    Interface::DArray arr_a(task.a);
    Interface::DArray arr_b(task.b);
    size_t ts_snd = common::ts();
    response_AssignTaskReply(idx, arr_a, arr_b, task.n, task.m, task.k);
    mRes[idx].ts_srv_snd = ts_snd;
  }
  else
  {
    std::printf("[master] no more tasks — queue exhausted\n");

    response_AssignTaskReply(0, {}, {}, 0, 0, 0);
    
    if (!mShutdown)
    {
      mShutdown.store(true, std::memory_order_relaxed);
      common::record_csv(mRes, "areg_out.csv");
    }
    areg::Application::signal_quit();
  }
}

void MasterComponent::request_RegisterWorker()
{
  auto wid = mNextWorkerId.fetch_add(1, std::memory_order_relaxed);
  std::printf("[master] registered worker %u\n", wid);

  response_RegisterWorkerReply(wid);
}

void MasterComponent::request_AssignTask(uint32_t worker_id)
{
  make_response(worker_id);
}

void MasterComponent::request_SubmitTask(uint32_t worker_id, uint32_t task_id,
                                         const Interface::DArray & result,
                                         uint64_t ts_rec, uint64_t ts_tsk,
                                         uint64_t ts_snd)
{
  size_t ts_src_rec = common::ts();
  (void) mCompleted.fetch_add(1, std::memory_order_relaxed);
  std::printf("[master] task %u done\n", task_id);

  common::TaskRecord out{task_id,
                         mQueue[task_id].n,
                         mQueue[task_id].m,
                         mQueue[task_id].k,
                         mRes[task_id].ts_srv_snd,
                         ts_src_rec,
                         ts_rec,
                         ts_tsk,
                         ts_snd};

  mRes[task_id] = out;

  make_response(worker_id);
}
