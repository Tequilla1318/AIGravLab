// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lower-triangular (n, m) indexing shared by every GravLab module that stores
// one value per spherical-harmonic degree n and order m (0 <= m <= n): the
// EGM2008 coefficient table, the Legendre recursion coefficients and the
// Legendre function values. One definition, so the layouts always agree
// (MISRA C++:2023 Rule 6.2.3).
//
// Layout: row by row in n, then m: index(n, m) = n(n+1)/2 + m.
#ifndef GRAVLAB_TRIANGULAR_HPP
#define GRAVLAB_TRIANGULAR_HPP

#include <cstddef>
#include <cstdint>

namespace gravlab {

// Largest degree any triangular table can hold. Keeps the slot arithmetic
// below free of overflow even with a 32-bit std::size_t
// (32768 * 32769 < 2^32). Far above any published gravity model.
constexpr std::int32_t kMaxCapacity{32767};

// Number of (n, m) slots for degrees 0..max_degree: (max_degree + 1)(max_degree
// + 2) / 2. Returns 0 for a negative max_degree or one above kMaxCapacity.
constexpr std::size_t slot_count(std::int32_t max_degree) noexcept {
  std::size_t count{0U};
  if ((max_degree >= 0) && (max_degree <= kMaxCapacity)) {
    const std::size_t d{static_cast<std::size_t>(max_degree)};
    count = ((d + 1U) * (d + 2U)) / 2U;
  }
  return count;
}

// Index of (n, m) in a triangular array: n(n+1)/2 + m. The caller must ensure
// 0 <= m <= n <= kMaxCapacity.
constexpr std::size_t slot_index(std::int32_t n, std::int32_t m) noexcept {
  const std::size_t un{static_cast<std::size_t>(n)};
  return ((un * (un + 1U)) / 2U) + static_cast<std::size_t>(m);
}

}  // namespace gravlab

#endif  // GRAVLAB_TRIANGULAR_HPP
