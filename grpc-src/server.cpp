#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

#include "task.grpc.pb.h"
#include "task.pb.h"

#include <common/utils.hpp>

using namespace taskdist;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;

// =======================================================================
// Dispatcher — a thread-safe FIFO task queue with shutdown coordination.
// No worker state — results arrive inline on the bidi stream.
// =======================================================================

struct Dispatcher
{
  // XXX: Enque tasks. We expect the enque to be done once at the beginning
  void enqueue(ArrayMultiplyTask task)
  {
    task_dims_.push_back({static_cast<common::I>(task.rows_a()),
                          static_cast<common::I>(task.cols_a()),
                          static_cast<common::I>(task.cols_b())});
    queue_.push_back(std::move(task));
  }

  // Block until a task is available, or shutdown was requested.
  // Returns false when there is no more work.
  // XXX: Order not sequential and there is no memory release
  bool dequeue(ArrayMultiplyTask & task)
  {
    auto id = enqueued_.fetch_add(1, std::memory_order_relaxed);
    if (id >= queue_.size())
      return false;
    task = std::move(queue_[id]);
    return true;
  }

  void on_task_completed()
  {
    size_t c = completed_.fetch_add(1, std::memory_order_relaxed);
  }

  bool done()
  {
    return completed_.load(std::memory_order_relaxed) >= queue_.size() ||
           shutdown_;
  }

  void shutdown() { shutdown_.store(true); }

  void wait_until_done()
  {
    while (!done())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  size_t enqueued() const { return queue_.size(); }
  size_t completed() const { return completed_.load(); }

  // XXX: for simplicity, we use the vector with no memory release
  std::vector<ArrayMultiplyTask> queue_;
  std::vector<std::array<common::I, 3>> task_dims_;
  std::atomic<size_t> enqueued_{0};
  std::atomic<size_t> completed_{0};
  std::atomic<bool> shutdown_{false};
};

// =======================================================================
// gRPC Service implementation
// =======================================================================

class TaskDistributorImpl final : public TaskDistributor::Service
{
public:
  explicit TaskDistributorImpl(Dispatcher & dispatcher)
      : dispatcher_(dispatcher), ts_snd_rec(dispatcher.enqueued())
  {
  }

  Status RegisterWorker(ServerContext * ctx, const WorkerInfo * /*req*/,
                        RegistrationResponse * resp) override
  {
    auto wid = next_worker_id_.fetch_add(1, std::memory_order_relaxed);
    std::printf("[server] registered %lu\n", wid);

    resp->set_worker_id(wid);
    return Status::OK;
  }

  Status AssignTasks(
      ServerContext * ctx,
      ServerReaderWriter<ArrayMultiplyTask, TaskResult> * stream) override
  {
    // --- first message identifies the worker ------------------------------
    TaskResult init;
    if (!stream->Read(&init))
      return Status::OK;

    const auto wid = init.worker_id();
    std::printf("[server] worker %d opened bidi stream\n", wid);

    // --- dispatch loop: write task → read result → repeat ----------------
    ArrayMultiplyTask task;
    while (dispatcher_.dequeue(task))
    {
      std::printf("[server] dispatching %d to %d\n", task.task_id(), wid);

      size_t ts_snd = common::ts();
      if (!stream->Write(task))
      {
        std::printf("[server] write to %d failed, stream broken\n", wid);
        (void)failed.fetch_add(1, std::memory_order_relaxed);
        dispatcher_.on_task_completed();
        return Status::OK;
      }

      TaskResult result;
      if (!stream->Read(&result))
      {
        std::printf("[server] worker %d disconnected mid-task, %d\n", wid,
                   task.task_id());
        (void)failed.fetch_add(1, std::memory_order_relaxed);
        dispatcher_.on_task_completed();
        return Status::OK;
      }
      size_t ts_rec = common::ts();
      
      // record timings
      common::TaskRecord tss{task.task_id(), task.rows_a(),   task.cols_a(),
                             task.cols_b(), ts_snd, ts_rec,  result.ts_rec(), result.ts_tsk(),
                             result.ts_snd(), result.ram_rss_kb()};
      
      ts_snd_rec[task.task_id()] = tss;

      dispatcher_.on_task_completed();
    }

    std::printf("[server] worker %d stream finished (no more tasks)\n", wid);
    return Status::OK;
  }

  Dispatcher & dispatcher_;
  std::atomic<uint64_t>           next_worker_id_{1};
  std::vector<common::TaskRecord> ts_snd_rec;
  std::atomic<uint64_t>           failed{0};
};

// =======================================================================
// Task loading
// =======================================================================

static std::vector<ArrayMultiplyTask> load_tasks(const std::string & filename)
{
  auto raw = common::read_tasks(filename);
  std::printf("[server] loaded %zu tasks from '%s'\n", raw.size(), filename.c_str());

  std::vector<ArrayMultiplyTask> out;
  out.reserve(raw.size());

  for (size_t i = 0; i < raw.size(); ++i)
  {
    const auto & t = raw[i];

    ArrayMultiplyTask pt;
    pt.set_task_id(i);
    pt.set_rows_a(t.n);
    pt.set_cols_a(t.m);
    pt.set_cols_b(t.k);

    for (auto v : t.a)
      pt.add_array_a(v);
    for (auto v : t.b)
      pt.add_array_b(v);

    out.push_back(std::move(pt));
  }

  return out;
}

// =======================================================================
// main
// =======================================================================

static void usage(std::string_view prog)
{
  std::printf("usage: %s <task_file> [bind_address] [out_file]\n", prog.data());
  std::exit(1);
}

int main(int argc, char ** argv)
{
  if (argc < 2)
    usage(argv[0]);

  // Capture CWD before anything changes it.
  common::g_output_dir = std::filesystem::current_path();
  
  const std::string task_file = argv[1];
  const std::string bind_addr =
      (argc > 2) ? argv[2] : std::string("0.0.0.0:50000");
  const std::string out_file =
      (argc > 3) ? argv[3] : std::string("grpc_out");


  auto tasks = load_tasks(task_file);
  if (tasks.empty())
  {
    std::printf("[server] no tasks to distribute, exiting\n");
    return 0;
  }

  Dispatcher dispatcher;
  for (auto & t : tasks)
    dispatcher.enqueue(std::move(t));
  tasks.clear();

  std::printf("[server] enqueued %zu tasks, binding to %s\n",
             dispatcher.enqueued(), bind_addr.c_str());

  TaskDistributorImpl service(dispatcher);

  ServerBuilder builder;
  builder.AddListeningPort(bind_addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (!server)
  {
    std::printf("[server] failed to start on %s\n", bind_addr.c_str());
    return 1;
  }
  std::printf("[server] listening on %s\n", bind_addr.c_str());

  dispatcher.wait_until_done();

  std::printf("[server] all %zu tasks completed, shutting down\n",
             dispatcher.completed());

  // --- Build task-timing records and write CSV --------------------------
  common::record_csv(service.ts_snd_rec, out_file);

  dispatcher.shutdown();
  server->Shutdown();
  server->Wait();

  return 0;
}
