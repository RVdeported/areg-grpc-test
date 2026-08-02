#include <cassert>
#include <cstdio>
#include <cstdlib>
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
  std::fprintf(stderr,
               "usage: %s <num_samples> <dim_lower> <dim_upper> [output_file]\n",
               prog.data());
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
    std::fprintf(stderr, "error: 1 <= dim_lower <= dim_upper required\n");
    return 1;
  }

  if (dim_upper > MAX_D) [[unlikely]]
  {
    std::fprintf(stderr, "error: Max dim is %u\n", MAX_D);
    return 1;
  }

  FILE * out = stdout;
  if (argc >= 5)
  {
    out = std::fopen(argv[4], "w");
    if (!out)
    {
      std::fprintf(stderr, "error: cannot open '%s' for writing\n", argv[4]);
      return 1;
    }
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

    std::fprintf(out, "%u|%u|%u|", n, m, k);

    // matrix A
    for (I i = 0; i < a_size; ++i)
    {
      if (i > 0)
        std::fprintf(out, ",");
      std::fprintf(out, "%.9f", val_dist(rng));
    }

    std::fprintf(out, "|");

    // matrix B
    for (I i = 0; i < b_size; ++i)
    {
      if (i > 0)
        std::fprintf(out, ",");
      std::fprintf(out, "%.9f", val_dist(rng));
    }

    std::fprintf(out, "\n");
  }

  if (out != stdout)
    std::fclose(out);

  return 0;
}
