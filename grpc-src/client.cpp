#include <chrono>
#include <iostream>
#include <cstdio>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "task.grpc.pb.h"
#include "task.pb.h"

#include <common/utils.hpp>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;

// =======================================================================
// main — worker client
// =======================================================================

int main(int argc, char * argv[])
{
  const std::string server_addr =
      (argc > 1) ? argv[1] : std::string("localhost:50000");

  auto channel =
      grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials());
  auto stub = taskdist::TaskDistributor::NewStub(channel);

  // -- register -----------------------------------------------------------
  uint32_t worker_id;
  {
    taskdist::WorkerInfo info;
    taskdist::RegistrationResponse resp;
    ClientContext ctx;

    Status st = stub->RegisterWorker(&ctx, info, &resp);
    if (!st.ok())
    {
      std::fprintf(stderr, "[client] registration failed: %s\n",
                 st.error_message().c_str());
      return 1;
    }
    worker_id = resp.worker_id();
    std::printf("[client %u] registered\n", worker_id);
  }

  // -- open bidi stream ----------------------------------------------------
  ClientContext ctx;
  auto stream = stub->AssignTasks(&ctx);

  // Send the initial message identifying ourselves.
  {
    taskdist::TaskResult ready;
    ready.set_worker_id(worker_id);
    if (!stream->Write(ready))
    {
      std::fprintf(stderr, "[client %u] failed to send initial message\n",
                 worker_id);
      return 1;
    }
  }

  std::printf("[client %u] accepting tasks\n", worker_id);

  // -- process tasks -------------------------------------------------------
  taskdist::ArrayMultiplyTask task;
  while (stream->Read(&task))
  {
    size_t ts_rec = common::ts();
  
    // XXX: do not care about excessive coping, etc
    std::vector<double> a(task.array_a().begin(), task.array_a().end());
    std::vector<double> b(task.array_b().begin(), task.array_b().end());
    auto result =
        common::multiply(a, b, task.rows_a(), task.cols_a(), task.cols_b());
    size_t ts_tsk = common::ts();

    taskdist::TaskResult tr;
    tr.set_task_id(task.task_id());
    tr.set_worker_id(worker_id);
    for (double v : result)
      tr.add_result(v);
    tr.set_ts_rec(ts_rec);
    tr.set_ts_tsk(ts_tsk);
    tr.set_ts_snd(common::ts());
    tr.set_ram_rss_kb(common::get_ram_rss_kb());

    if (!stream->Write(tr))
    {
      std::fprintf(stderr, "[client %u] write failed, stream broken\n",
          worker_id);
      break;
    }
    std::printf("[client %u] submitted result for %d\n", worker_id,
               task.task_id());
  }

  Status st = stream->Finish();
  std::printf("[client %u] stream closed: %s\n", worker_id, st.error_message().c_str());
  return 0;
}
