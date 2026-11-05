// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Fully normalized associated Legendre functions Pbar_nm(cos theta), computed
// with the modified forward column method of Holmes & Featherstone (2002):
//
//   S. A. Holmes, W. E. Featherstone, "A unified approach to the Clenshaw
//   summation and the recursive computation of very high degree and order
//   normalised associated Legendre functions", J. Geodesy 76 (2002) 279-299,
//   doi:10.1007/s00190-002-0216-2
//
// Normalization: 4-pi ("geodesy"), no Condon-Shortley phase, the same
// convention as the EGM2008 coefficients, so Pbar_nm pairs one-to-one with
// Cbar_nm, Sbar_nm. theta is the geocentric colatitude, 0 <= theta <= pi.
//
// Method (see docs/legendre_hf.md for the derivation):
//
//   t = cos theta, u = sin theta
//   Pbar(0,0) = 1, Pbar(1,1) = sqrt(3) u
//   Pbar(m,m) = u sqrt((2m+1)/(2m)) Pbar(m-1,m-1)                  m >= 2
//   Pbar(n,m) = a_nm t Pbar(n-1,m) - b_nm Pbar(n-2,m)              n > m
//   a_nm = sqrt[(2n-1)(2n+1) / ((n-m)(n+m))]
//   b_nm = sqrt[(2n+1)(n+m-1)(n-m-1) / ((n-m)(n+m)(2n-3))]
//
// The factor u^m in every Pbar_nm of column m is what underflows at high
// degree. Holmes-Featherstone run the recursion on X_nm = s Pbar_nm / u^m
// instead, with a fixed scale s, and multiply u^m / s back in only when
// storing each value.
// The recursion is linear in each column, so the same a_nm, b_nm apply to X.
// This keeps the column recursion in range for all theta up to degree 2700.
//
// Written to MISRA C++:2023 like the rest of the library (docs/misra.md):
// caller-provided storage, no dynamic memory, Status returns, noexcept.
#ifndef GRAVLAB_LEGENDRE_HOLMES_AND_FEATHERSTONE_HPP
#define GRAVLAB_LEGENDRE_HOLMES_AND_FEATHERSTONE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "gravlab/triangular.hpp"

namespace gravlab {
namespace legendre_hf {

enum class Status : std::uint8_t {
  ok = 0U,
  invalid_argument,         // bad buffers, null pointer, or theta outside [0, pi]
  not_initialized,          // Recursion::init() has not succeeded
  degree_exceeds_capacity,  // requested degree above what was initialized
  buffer_too_small          // output shorter than slot_count(degree)
};

// Short, static, English description of a status. Never returns nullptr.
const char* describe(Status status) noexcept;

// Highest degree supported. Holmes & Featherstone show the scaled recursion
// stays in double range for every theta up to about degree 2700.
constexpr std::int32_t kMaxDegree{2700};

// Scale applied to the column recursion. A power of two, so scaling and
// unscaling are exact (multiplying by 2^k never rounds); it is about
// 1.1e-280, the magnitude Holmes & Featherstone use (1e-280).
constexpr double kScale{0x1p-930};
constexpr double kInverseScale{0x1p930};

// Caller-owned arrays for the precomputed recursion coefficients:
// a and b triangular (length >= slot_count(max_degree)), seeds one per order
// (length >= max_degree + 1). They must outlive the Recursion.
struct RecursionBuffers {
  double* a{nullptr};
  double* b{nullptr};
  double* seeds{nullptr};
  std::size_t length{0U};       // elements in a and in b
  std::size_t seed_length{0U};  // elements in seeds
};

// Fixed-size storage for the recursion coefficients up to MaxDegree. At
// degree 2190 it is about 38 MB, so give it static storage duration.
template <std::int32_t MaxDegree>
class RecursionStorage {
  static_assert((MaxDegree >= 0) && (MaxDegree <= kMaxDegree),
                "MaxDegree must be in 0..kMaxDegree");

 public:
  static constexpr std::size_t kSlots{slot_count(MaxDegree)};
  static constexpr std::size_t kSeeds{static_cast<std::size_t>(MaxDegree) + 1U};

  // Lvalue-only (Rule 6.8.4): the pointers refer to this object.
  RecursionBuffers buffers() & noexcept {
    RecursionBuffers b{};
    b.a = a_.data();
    b.b = b_.data();
    b.seeds = seeds_.data();
    b.length = kSlots;
    b.seed_length = kSeeds;
    return b;
  }

 private:
  std::array<double, kSlots> a_{};
  std::array<double, kSlots> b_{};
  std::array<double, kSeeds> seeds_{};
};

// Fixed-size storage for one set of Pbar_nm values up to MaxDegree, in
// triangular order (slot_index). About 19 MB at degree 2190.
template <std::int32_t MaxDegree>
class ValueStorage {
  static_assert((MaxDegree >= 0) && (MaxDegree <= kMaxDegree),
                "MaxDegree must be in 0..kMaxDegree");

 public:
  static constexpr std::size_t kSlots{slot_count(MaxDegree)};

  // Lvalue-only (Rule 6.8.4).
  double* data() & noexcept { return values_.data(); }
  const double* data() const& noexcept { return values_.data(); }
  static constexpr std::size_t size() noexcept { return kSlots; }

 private:
  std::array<double, kSlots> values_{};
};

// The theta-independent part of the method, computed once and reused for every
// evaluation (and safe to share between threads once initialized):
//   a_nm, b_nm   at slot_index(n, m) for n > m (zero elsewhere);
//   seeds[m]     = s * Pbar_mm / u^m = s * sqrt3 * prod_{k=2..m} sqrt((2k+1)/(2k)),
//                  with seeds[0] = s.
class Recursion {
 public:
  Recursion() noexcept = default;

  // Attaches the buffers and fills them for degrees 0..max_degree. Fails
  // with Status::invalid_argument for a null pointer, max_degree outside
  // 0..kMaxDegree, or buffers that are too short; the Recursion is then left
  // uninitialized.
  [[nodiscard]] Status init(const RecursionBuffers& buffers,
                            std::int32_t max_degree) noexcept;

  bool is_initialized() const noexcept { return max_degree_ >= 0; }

  // Highest degree initialized; -1 before a successful init().
  std::int32_t max_degree() const noexcept { return max_degree_; }

  // Read-only views of the precomputed tables; nullptr before init().
  const double* a_data() const noexcept { return buffers_.a; }
  const double* b_data() const noexcept { return buffers_.b; }
  const double* seeds_data() const noexcept { return buffers_.seeds; }

 private:
  RecursionBuffers buffers_{};
  std::int32_t max_degree_{-1};
};

// Computes Pbar_nm(cos theta) for 0 <= m <= n <= degree into values, at
// values[slot_index(n, m)]. theta is the colatitude in radians, 0 <= theta <= pi.
//
// Values whose true magnitude is below about 1e-280 can lose relative
// precision or flush to zero; they cannot affect any gravity sum.
//
// Fails, writing nothing, with:
//   not_initialized          recursion not initialized;
//   degree_exceeds_capacity  degree < 0 or above recursion.max_degree();
//   invalid_argument         values is null, or theta is not in [0, pi] (NaN too);
//   buffer_too_small         length < slot_count(degree).
[[nodiscard]] Status evaluate(const Recursion& recursion, double theta,
                              std::int32_t degree, double* values,
                              std::size_t length) noexcept;

}  // namespace legendre_hf
}  // namespace gravlab

#endif  // GRAVLAB_LEGENDRE_HOLMES_AND_FEATHERSTONE_HPP
