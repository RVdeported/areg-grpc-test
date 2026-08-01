#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <print>
#include <random>
#include <string_view>
#include "utils.hpp"

// -----------------------------------------------------------------------
// Task generator: produces matrix-multiply tasks in text format.
//
// Format: n|m|k|a1,a2,...,a_N|b1,b2,...,b_M
//   n, m, k     – dimensions: A is n×m, B is m×k
//   a1..a_N     – matrix A values, row-major, comma-separated (N = n*m)
//   b1..b_M     – matrix B values, row-major, comma-separated (M = m*k)
//
// Dimensions are uniformly sampled from [dim_lower, dim_upper].
// Matrix values are normally distributed ~ N(0, 1).
// -----------------------------------------------------------------------

namespace {
static void usage(std::string_view prog)
{
  std::println(stderr,
               "usage: {} <num_samples> <dim_lower> <dim_upper> [output_file]",
               prog);
  std::exit(1);
}
}

int main(int argc, char ** argv)
{
  using namespace common;
  constexpr static I MAX_D = 100;

  if (argc < 4)
    usage(argv[0]);

  I num_samples = static_cast<I>(std::stoul(argv[1]));
  I dim_lower = static_cast<I>(std::stoul(argv[2]));
  I dim_upper = static_cast<I>(std::stoul(argv[3]));
  if (dim_lower < 1 || dim_lower > dim_upper) [[unlikely]]
  {
    std::println(stderr, "error: 1 <= dim_lower <= dim_upper required");
    return 1;
  }

  if (dim_upper > MAX_D) [[unlikely]]
  {
    std::println(stderr, "error: Max dim is {}", MAX_D);
    return 1;
  }

  std::ofstream fout;
  std::ostream * out = &std::cout;
  if (argc >= 5)
  {
    fout.open(argv[4]);
    if (!fout)
    {
      std::println(stderr, "error: cannot open '{}' for writing", argv[4]);
      return 1;
    }
    out = &fout;
  }

  std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<I> dim_dist(dim_lower, dim_upper);
  std::normal_distribution<F> val_dist(0.0, 1.0);

  for (I s = 0; s < num_samples; ++s)
  {
    const I n = dim_dist(rng);
    const I m = dim_dist(rng);
    const I k = dim_dist(rng);

    const I a_size = n * m;
    const I b_size = m * k;

    std::print(*out, "{}|{}|{}|", n, m, k);

    // matrix A
    for (I i = 0; i < a_size; ++i)
    {
      if (i > 0)
        std::print(*out, ",");
      std::print(*out, "{:.9f}", val_dist(rng));
    }

    std::print(*out, "|");

    // matrix B
    for (I i = 0; i < b_size; ++i)
    {
      if (i > 0)
        std::print(*out, ",");
      std::print(*out, "{:.9f}", val_dist(rng));
    }

    std::println(*out, "");
  }

  return 0;
}
