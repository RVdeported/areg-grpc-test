#include <atomic>
#include <chrono>
#include <cstdlib>
#include <print>
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

class Dispatcher
{
public:
  // XXX: Enque tasks. We expect the enque to be done once at the beginning
  void enqueue(ArrayMultiplyTask task)
  {
    queue_.push_back(std::move(task));
  }
  
  // Block until a task is available, or shutdown was requested.
  // Returns false when there is no more work.
  // XXX: Order not sequential and there is no memory release
  bool dequeue(ArrayMultiplyTask & task)
  {
    auto id = enqueued_.fetch_add(1, std::memory_order_relaxed);
    if (id >= queue_.size()) return false;
    task = std::move(queue_[id]);
    return true;
  }

  void on_task_completed()
  {
    size_t c = completed_.fetch_add(1, std::memory_order_relaxed);
  }

  bool done()
  {
    return completed_.load(std::memory_order_relaxed) >= queue_.size() || shutdown_;
  }

  void shutdown()
  {
    shutdown_.store(true); 
  }

  void wait_until_done()
  {
    while(!done())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  size_t enqueued()  const { return queue_.size(); }
  size_t completed() const { return completed_.load(); }

private:
  // XXX: for simplicity, we use the vector with no memory release
  std::vector<ArrayMultiplyTask> queue_;
  std::atomic<size_t>            enqueued_{0};
  std::atomic<size_t>            completed_{0};
  std::atomic<bool>              shutdown_{false};
};

// =======================================================================
// gRPC Service implementation
// =======================================================================

class TaskDistributorImpl final : public TaskDistributor::Service
{
public:
  explicit TaskDistributorImpl(Dispatcher & dispatcher)
      : dispatcher_(dispatcher), ts_snd_rec(dispatcher.enqueued())
  {}

  Status RegisterWorker(ServerContext *              ctx,
                        const WorkerInfo *           /*req*/,
                        RegistrationResponse * resp) override
  {
    auto wid = next_worker_id_.fetch_add(1, std::memory_order_relaxed);
    std::print("[server] registered {}\n", wid);

    resp->set_worker_id(wid);
    return Status::OK;
  }

  Status AssignTasks(
      ServerContext *                                      ctx,
      ServerReaderWriter<ArrayMultiplyTask, TaskResult> * stream) override
  {
    // --- first message identifies the worker ------------------------------
    TaskResult init;
    if (!stream->Read(&init))
      return Status::OK;

    const auto wid = init.worker_id();
    std::print("[server] worker {} opened bidi stream\n", wid);

    // --- dispatch loop: write task → read result → repeat ----------------
    ArrayMultiplyTask task;
    while (dispatcher_.dequeue(task))
    {
      std::print("[server] dispatching {} to {}\n", task.task_id(), wid);
      
      size_t ts_snd = common::ts();
      if (!stream->Write(task))
      {
        std::print("[server] write to {} failed, stream broken\n", wid);
        (void) failed.fetch_add(1, std::memory_order_relaxed);
        dispatcher_.on_task_completed();
        return Status::OK;
      }

      TaskResult result;
      if (!stream->Read(&result))
      {
        std::print("[server] worker {} disconnected mid-task, {}\n",
                   wid, task.task_id());
        (void) failed.fetch_add(1, std::memory_order_relaxed);
        dispatcher_.on_task_completed();
        return Status::OK;
      }
      size_t ts_rec = common::ts();
      // TODO: handle timings
      ts_snd_rec[task.task_id()] = {ts_snd, ts_rec};

      dispatcher_.on_task_completed();
    }

    std::print("[server] worker {} stream finished (no more tasks)\n", wid);
    return Status::OK;
  }

private:
  Dispatcher          & dispatcher_;
  std::atomic<uint64_t> next_worker_id_{1};
  std::vector<std::array<size_t, 2>> 
                        ts_snd_rec;
  std::atomic<uint64_t> failed{0};

};

// =======================================================================
// Task loading
// =======================================================================

static std::vector<ArrayMultiplyTask>
load_tasks(const std::string & filename)
{
  auto raw = common::read_tasks(filename);
  std::print("[server] loaded {} tasks from '{}'\n", raw.size(), filename);

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

    for (auto v : t.a) pt.add_array_a(v);
    for (auto v : t.b) pt.add_array_b(v);

    out.push_back(std::move(pt));
  }

  return out;
}

// =======================================================================
// main
// =======================================================================

static void usage(std::string_view prog)
{
  std::println(stderr, "usage: {} <task_file> [bind_address]", prog);
  std::exit(1);
}

int main(int argc, char ** argv)
{
  if (argc < 2) usage(argv[0]);

  const std::string task_file = argv[1];
  const std::string bind_addr =
      (argc > 2) ? argv[2] : std::string("0.0.0.0:50000");

  auto tasks = load_tasks(task_file);
  if (tasks.empty())
  {
    std::println(stderr, "[server] no tasks to distribute, exiting");
    return 0;
  }

  Dispatcher dispatcher;
  for (auto & t : tasks) dispatcher.enqueue(std::move(t));
  tasks.clear();

  std::print("[server] enqueued {} tasks, binding to {}\n",
             dispatcher.enqueued(), bind_addr);

  TaskDistributorImpl service(dispatcher);

  ServerBuilder builder;
  builder.AddListeningPort(bind_addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (!server)
  {
    std::println(stderr, "[server] failed to start on {}", bind_addr);
    return 1;
  }
  std::print("[server] listening on {}\n", bind_addr);

  dispatcher.wait_until_done();

  std::print("[server] all {} tasks completed, shutting down\n",
             dispatcher.completed());

  dispatcher.shutdown();
  server->Shutdown();
  server->Wait();

  return 0;
}
