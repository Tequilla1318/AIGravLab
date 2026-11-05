// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Holmes & Featherstone (2002) modified forward column method for fully
// normalized associated Legendre functions. See the header and
// docs/legendre_hf.md. MISRA C++:2023 notes are in docs/misra.md.
#include "gravlab/legendre_holmes_and_featherstone.hpp"

#include "gravlab/triangular.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gravlab {
namespace legendre_hf {
namespace {

// pi rounded to double. The largest double <= pi, so theta = kPi is the
// value std::acos(-1.0) gives and is accepted.
constexpr double kPi{3.141592653589793};

constexpr std::array<const char*, 5U> kDescriptions{{
    "ok",
    "invalid argument",
    "recursion not initialized",
    "degree exceeds recursion capacity",
    "output buffer too small",
}};
static_assert((static_cast<std::size_t>(Status::buffer_too_small) + 1U) ==
                  kDescriptions.size(),
              "kDescriptions must cover every Status");

// a_nm and b_nm for n > m.
//
// How they are rounded matters more than it looks. Near the poles (t = +-1)
// the column recursion has a repeated characteristic root, so a systematic
// rounding bias in the coefficients accumulates like n^2 instead of
// averaging out. Taking one square root of the whole ratio gave errors of
// 6e-11 at degree 2190 at the pole. Following SHTOOLS (PlmBar), each integer
// factor gets its own square root, and integer factors that need no root
// stay exact; that gives 4e-12, matching pyshtools. See docs/legendre_hf.md.
double root(std::int32_t k) noexcept { return std::sqrt(static_cast<double>(k)); }

double coefficient_a(std::int32_t n, std::int32_t m) noexcept {
  double a{0.0};
  if (m == 0) {
    // sqrt((2n-1)(2n+1)) / n
    a = (root((2 * n) - 1) * root((2 * n) + 1)) / static_cast<double>(n);
  } else if (n == (m + 1)) {
    // sqrt((2m+1)(2m+3) / (1 * (2m+1))) = sqrt(2m+3)
    a = root((2 * m) + 3);
  } else {
    a = ((root((2 * n) + 1) * root((2 * n) - 1)) / root(n + m)) / root(n - m);
  }
  return a;
}

double coefficient_b(std::int32_t n, std::int32_t m) noexcept {
  double b{0.0};
  if (n <= (m + 1)) {
    b = 0.0;  // the factor (n - m - 1) is zero one below the diagonal
  } else if (m == 0) {
    // (n-1) sqrt(2n+1) / (sqrt(2n-3) n)
    b = ((static_cast<double>(n - 1) * root((2 * n) + 1)) / root((2 * n) - 3)) /
        static_cast<double>(n);
  } else {
    b = ((((root((2 * n) + 1) * root((n - m) - 1)) * root((n + m) - 1)) /
          root((2 * n) - 3)) /
         root(n + m)) /
        root(n - m);
  }
  return b;
}

// Keeping the unscaling factor normal (see evaluate()). Multiplying by
// 2^kRenorm is exact, and 2^-kRenorm leaves ample headroom above the
// smallest normal double (2^-1022).
constexpr std::int32_t kRenorm{900};
constexpr double kRenormThreshold{0x1p-900};
constexpr double kRenormFactor{0x1p900};
// Beyond this the true values are far below the double range (u^m < 2^-4000
// or so); stop renormalizing and let them flush to zero.
constexpr std::int32_t kMaxShift{4500};

// X * factor * 2^-shift, with a single rounding.
double unscale(double x, double factor, std::int32_t shift) noexcept {
  double v{x * factor};
  if (shift > 0) {
    v = std::ldexp(v, -shift);
  }
  return v;
}

}  // namespace

const char* describe(Status status) noexcept {
  const std::size_t index{static_cast<std::size_t>(status)};
  return (index < kDescriptions.size()) ? kDescriptions[index]
                                        : "unknown status";
}

Status Recursion::init(const RecursionBuffers& buffers,
                       std::int32_t max_degree) noexcept {
  buffers_ = RecursionBuffers{};
  max_degree_ = -1;
  const bool pointers_ok{(buffers.a != nullptr) && (buffers.b != nullptr) &&
                         (buffers.seeds != nullptr)};
  if ((!pointers_ok) || (max_degree < 0) || (max_degree > kMaxDegree)) {
    return Status::invalid_argument;
  }
  const std::size_t seeds_needed{static_cast<std::size_t>(max_degree) + 1U};
  if ((buffers.length < slot_count(max_degree)) ||
      (buffers.seed_length < seeds_needed)) {
    return Status::invalid_argument;
  }

  // Recursion coefficients. Slots with n == m are not used by the column
  // recursion; they are set to zero so the tables hold no stale values.
  for (std::int32_t n{0}; n <= max_degree; ++n) {
    for (std::int32_t m{0}; m <= n; ++m) {
      const std::size_t i{slot_index(n, m)};
      if (n > m) {
        buffers.a[i] = coefficient_a(n, m);
        buffers.b[i] = coefficient_b(n, m);
      } else {
        buffers.a[i] = 0.0;
        buffers.b[i] = 0.0;
      }
    }
  }

  // Scaled sectoral seeds, seeds[m] = s * Pbar_mm / u^m. They do not depend
  // on theta: the u^m factor is exactly what Holmes & Featherstone take out.
  buffers.seeds[0U] = kScale;
  if (max_degree >= 1) {
    buffers.seeds[1U] = kScale * root(3);
  }
  for (std::int32_t m{2}; m <= max_degree; ++m) {
    const std::size_t i{static_cast<std::size_t>(m)};
    buffers.seeds[i] = (buffers.seeds[i - 1U] * root((2 * m) + 1)) / root(2 * m);
  }

  buffers_ = buffers;
  max_degree_ = max_degree;
  return Status::ok;
}

Status evaluate(const Recursion& recursion, double theta, std::int32_t degree,
                double* values, std::size_t length) noexcept {
  if (!recursion.is_initialized()) {
    return Status::not_initialized;
  }
  if ((degree < 0) || (degree > recursion.max_degree())) {
    return Status::degree_exceeds_capacity;
  }
  // The comparisons are false for NaN, so NaN is rejected too.
  if ((values == nullptr) || (!((theta >= 0.0) && (theta <= kPi)))) {
    return Status::invalid_argument;
  }
  if (length < slot_count(degree)) {
    return Status::buffer_too_small;
  }

  const double t{std::cos(theta)};
  const double u{std::sin(theta)};
  const double* const a{recursion.a_data()};
  const double* const b{recursion.b_data()};
  const double* const seeds{recursion.seeds_data()};

  // Unscaling factor for the current column, u^m / s = factor * 2^-shift.
  // factor starts at 1/s and gains one factor of u per column. Left alone it
  // would turn subnormal once u^m < ~1e-588 and lose precision on values as
  // large as ~1e-200, so it is kept normal: when it drops below 2^-kRenorm it
  // is multiplied back up by 2^kRenorm (exact) and shift records that. Columns
  // with shift > 0 are unscaled with one std::ldexp, which rounds once, so a
  // value only loses precision if it is itself below the double range.
  double factor{kInverseScale};
  std::int32_t shift{0};

  for (std::int32_t m{0}; m <= degree; ++m) {
    // Column m. x2 = X(n-2, m), x1 = X(n-1, m), where X = s * Pbar / u^m.
    const double x_mm{seeds[static_cast<std::size_t>(m)]};
    values[slot_index(m, m)] = unscale(x_mm, factor, shift);

    if (m < degree) {
      // n = m + 1: b is zero, so only the term above is needed.
      const std::size_t i1{slot_index(m + 1, m)};
      double x2{x_mm};
      double x1{a[i1] * t * x_mm};
      values[i1] = unscale(x1, factor, shift);

      for (std::int32_t n{m + 2}; n <= degree; ++n) {
        const std::size_t i{slot_index(n, m)};
        const double x{(a[i] * t * x1) - (b[i] * x2)};
        values[i] = unscale(x, factor, shift);
        x2 = x1;
        x1 = x;
      }
    }

    factor = factor * u;
    if ((factor < kRenormThreshold) && (factor > 0.0) && (shift < kMaxShift)) {
      factor = factor * kRenormFactor;
      shift = shift + kRenorm;
    }
  }
  return Status::ok;
}

}  // namespace legendre_hf
}  // namespace gravlab
