#pragma once
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <vector>
#include <ranges>
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
// XXX: Note that we deliberately not using any optimizations
inline std::vector<double> multiply(const std::vector<F> & a,
                                    const std::vector<F> & b, I rows_a,
                                    I cols_a, I cols_b)
{
  assert(a.size() == cols_a * rows_a);
  assert(b.size() == cols_a * cols_b);
  std::vector<double> result(rows_a * cols_b, 0.0);
  for (I i = 0; i < rows_a; ++i)
  {
    for (I k = 0; k < cols_a; ++k)
    {
      F aik = a[i * cols_a + k];
      for (I j = 0; j < cols_b; ++j)
      {
        result[i * cols_b + j] += aik * b[k * cols_b + j];
      }
    }
  }
  return result;
}

inline size_t ts()
{
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

// -----------------------------------------------------------------------
// CSV recorder — writes completed-task timings to disk
// -----------------------------------------------------------------------

// Set by main() before any framework initialization to capture the
// original CWD (the "place of execution") in case the framework changes it.
inline std::filesystem::path g_output_dir;

struct TaskRecord
{
  I task_id;
  I n, m, k;
  size_t ts_srv_snd, ts_srv_rec, ts_cli_rec, ts_cli_tsk, ts_cli_snd;

  void Validate() const
  {
#ifndef NDEBUG
    const auto v = {ts_srv_snd, ts_cli_rec, ts_cli_tsk, ts_cli_snd, ts_srv_rec};
    for (int i = 1; i < v.size(); i++)
    {
      assert(v[i-1] <= v[i]);
    }
#endif
  }
};

inline void record_csv(const std::vector<TaskRecord> & records)
{
  auto path = g_output_dir.empty()
                  ? std::filesystem::path("out.csv")
                  : g_output_dir / "out.csv";
  std::ofstream out(path);
  if (!out)
    throw std::runtime_error("record_csv: cannot open file: " +
                             path.string() + "\n");
  out << "task_id,n,m,k,ts_srv_snd,ts_srv_rec,ts_cli_rec,ts_cli_tsk,ts_cli_"
         "snd\n";
  for (const auto & r : records)
  {
    r.Validate();
    std::printf("%lu %lu %lu\n", r.ts_cli_rec, r.ts_cli_tsk, r.ts_cli_snd);
    out << r.task_id << ',' << r.n << ',' << r.m << ',' << r.k << ','
        << r.ts_srv_snd << ',' << r.ts_srv_rec << ',' << r.ts_cli_rec << ','
        << r.ts_cli_tsk << ',' << r.ts_cli_snd << '\n';
  }
}
} // namespace common
