#pragma once
#include <cassert>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <vector>
namespace common
{
using I = unsigned int;
using F = double;

// -----------------------------------------------------------------------
// Reader of the tasks
// -----------------------------------------------------------------------
struct Task
{
  I              n, m, k;
  std::vector<F> a; // n*m values, row-major
  std::vector<F> b; // m*k values, row-major
  
  Task(I n, I m, I k, 
      std::vector<F> a, std::vector<F> b)
    : n(n), m(m), k(k), a(a), b(b)
  {
    assert(a.size() == n * m);
    assert(b.size() == m * k);
  }
};

inline Task parse_task(const std::string & line)
{
  std::stringstream ss(line);
  std::string       token;

  auto next = [&]() -> std::string {
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

  auto parse_values = [&](std::vector<F> & out, I expected) {
    std::string field = next();
    std::stringstream fs(field);
    std::string       val;
    while (std::getline(fs, val, ','))
    {
      out.push_back(std::stod(val));
    }
    if (out.size() != expected)
      throw std::runtime_error(
          "parse_task: expected " + std::to_string(expected) + " values, got " +
          std::to_string(out.size()) + " in: " + line);
  };

  parse_values(a, n * m);
  parse_values(b, m * k);

  if (std::getline(ss, token, '|'))
    throw std::runtime_error("parse_task: trailing fields in: " + line);

  return Task(n, m ,k, a, b);
}

inline std::vector<Task> read_tasks(const std::string & filename)
{
  std::ifstream in(filename);
  if (!in)
    throw std::runtime_error("read_tasks: cannot open file: " + filename);

  std::vector<Task> tasks;
  std::string       line;
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
} // namespace common
