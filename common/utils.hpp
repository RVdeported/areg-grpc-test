#pragma once
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>
namespace common
{
using I = uint32_t;
using F = double;

// -----------------------------------------------------------------------
// Reader of the tasks
// -----------------------------------------------------------------------
struct Task
{
  I n, m, k;
  std::vector<F> a; // n*m values, row-major
  std::vector<F> b; // m*k values, row-major

  Task(I n, I m, I k, std::vector<F> a, std::vector<F> b)
      : n(n), m(m), k(k), a(a), b(b)
  {
    assert(a.size() == n * m);
    assert(b.size() == m * k);
  }
};

inline Task parse_task(const std::string & line)
{
  std::stringstream ss(line);
  std::string token;

  auto next = [&]() -> std::string
  {
    if (!std::getline(ss, token, '|'))
      throw std::runtime_error("parse_task: missing field in: " + line);
    return token;
  };

  I n = static_cast<I>(std::stoul(next()));
  I m = static_cast<I>(std::stoul(next()));
  I k = static_cast<I>(std::stoul(next()));
  std::vector<F> a, b;

  if (n == 0 || m == 0 || k == 0)
    throw std::runtime_error("parse_task: dimensions must be positive in: " +
                             line);

  auto parse_values = [&](std::vector<F> & out, I expected)
  {
    std::string field = next();
    std::stringstream fs(field);
    std::string val;
    while (std::getline(fs, val, ','))
    {
      out.push_back(std::stod(val));
    }
    if (out.size() != expected)
      throw std::runtime_error("parse_task: expected " +
                               std::to_string(expected) + " values, got " +
                               std::to_string(out.size()) + " in: " + line);
  };

  parse_values(a, n * m);
  parse_values(b, m * k);

  if (std::getline(ss, token, '|'))
    throw std::runtime_error("parse_task: trailing fields in: " + line);

  return Task(n, m, k, a, b);
}

inline std::vector<Task> read_tasks(const std::string & filename)
{
  std::ifstream in(filename);
  if (!in)
    throw std::runtime_error("read_tasks: cannot open file: " + filename);

  std::vector<Task> tasks;
  std::string line;
  for (size_t lnum = 1; std::getline(in, line); ++lnum)
  {
    if (line.empty())
      continue;
    try
    {
      tasks.push_back(parse_task(line));
    }
    catch (const std::exception & e)
    {
      throw std::runtime_error("read_tasks: line " + std::to_string(lnum) +
                               ": " + e.what());
    }
  }
  return tasks;
}

// -----------------------------------------------------------------------
// Matrix multiply: A(r×c) × B(c×cb) → result (r×cb), all row-major.
// -----------------------------------------------------------------------
template <typename T>
concept Container = requires(T a) {
  typename T::value_type;
  { a.begin() } -> std::input_or_output_iterator;
  { a.end() } -> std::input_or_output_iterator;
};

// XXX: Note that we deliberately not using any optimizations
template <Container C>
inline void multiply(C const & a, C const & b, I rows_a, I cols_a, I cols_b,
                     std::vector<F> & out)
{
  assert(a.size() == cols_a * rows_a);
  assert(b.size() == cols_a * cols_b);
  out.resize(rows_a * cols_b, 0.0);
  for (I i = 0; i < rows_a; ++i)
  {
    for (I k = 0; k < cols_a; ++k)
    {
      F aik = a[i * cols_a + k];
      for (I j = 0; j < cols_b; ++j)
      {
        out[i * cols_b + j] += aik * b[k * cols_b + j];
      }
    }
  }
  return;
}

inline size_t ts()
{
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// -----------------------------------------------------------------------
// System resource metrics — read from /proc
// -----------------------------------------------------------------------
inline size_t get_ram_used_kb()
{
  std::ifstream meminfo("/proc/meminfo");
  std::string line;
  size_t mem_total = 0;
  size_t mem_available = 0;

  while (std::getline(meminfo, line))
  {
    if (line.starts_with("MemTotal:"))
    {
      std::stringstream ss(line);
      std::string key, value;
      ss >> key >> value;
      mem_total = std::stoull(value);
    }
    else if (line.starts_with("MemAvailable:"))
    {
      std::stringstream ss(line);
      std::string key, value;
      ss >> key >> value;
      mem_available = std::stoull(value);
    }
    if (mem_total != 0 && mem_available != 0)
      break;
  }
  return std::max(mem_total - mem_available, 0LU);
}

// Returns overall CPU usage percentage (0–100) by reading /proc/stat.
// Computed as: (busy_time / total_time) * 100, where busy_time excludes
// idle and iowait.
inline double get_cpu_usage_percent()
{
  std::ifstream stat("/proc/stat");
  std::string line;
  if (!std::getline(stat, line))
    return 0.0;

  std::stringstream ss(line);
  std::string cpu_label;
  ss >> cpu_label; // "cpu"

  uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
  uint64_t irq = 0, softirq = 0, steal = 0;
  ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

  uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
  uint64_t busy = total - idle - iowait;

  if (total == 0)
    return 0.0;

  return static_cast<double>(busy) * 100.0 / static_cast<double>(total);
}

// -----------------------------------------------------------------------
// CSV recorder — writes completed-task timings to disk
// -----------------------------------------------------------------------

// Set by main() before any framework initialization to capture the
// original CWD (the "place of execution") in case the framework changes it.
inline std::filesystem::path g_output_dir;
inline std::string g_output_file;

struct TaskRecord
{
  I task_id;
  I n, m, k;
  size_t ts_srv_snd, ts_srv_rec, ts_cli_rec, ts_cli_tsk, ts_cli_snd;
  size_t ram_used_kb;       // overall system RAM used in kB
  double cpu_usage_percent; // overall CPU usage 0–100

  void Validate() const
  {
#ifndef NDEBUG
    const auto v = {ts_srv_snd, ts_cli_rec, ts_cli_tsk, ts_cli_snd, ts_srv_rec};
    for (size_t i = 1; i < v.size(); i++)
    {
      assert(v.begin() + i - 1 <= v.begin() + i);
    }
#endif
  }
};

inline void record_csv(const std::vector<TaskRecord> & records,
                       const std::string & file_name)
{
  auto path = g_output_dir.empty() ? std::filesystem::path(file_name)
                                   : g_output_dir / file_name;
  std::ofstream out(path);
  if (!out)
    throw std::runtime_error("record_csv: cannot open file: " + path.string() +
                             "\n");

  std::cout << "recording to " << path << '\n';
  out << "task_id,n,m,k,ts_srv_snd,ts_srv_rec,ts_cli_rec,ts_cli_tsk,ts_cli_"
         "snd,ram_used_kb,cpu_usage_percent\n";
  for (const auto & r : records)
  {
    r.Validate();
    out << r.task_id << ',' << r.n << ',' << r.m << ',' << r.k << ','
        << r.ts_srv_snd << ',' << r.ts_srv_rec << ',' << r.ts_cli_rec << ','
        << r.ts_cli_tsk << ',' << r.ts_cli_snd << ',' << r.ram_used_kb << ','
        << r.cpu_usage_percent << '\n';
  }
}
} // namespace common
