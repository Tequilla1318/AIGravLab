// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parser for the NGA EGM2008 spherical-harmonic coefficient file
// (EGM2008_to2190_TideFree.txt and files in the same format).
//
// File format: one record per line, whitespace separated, six fields:
//
//     n  m  C_nm  S_nm  sigma_C_nm  sigma_S_nm
//
// n, m are integers (degree, order, 0 <= m <= n). The four reals are fully
// normalized coefficients and their standard deviations, written in Fortran
// notation with a 'D' exponent marker, e.g. -0.484165143790815D-03.
// Every real is converted to an IEEE-754 binary64 value (C++ double), rounded
// correctly to nearest.
//
// Written to MISRA C++:2023 (see docs/misra.md):
//   - no dynamic memory: the caller provides the coefficient storage, and
//     parsing uses fixed-size buffers only;
//   - no exceptions: every operation reports a Status, and every function is
//     noexcept;
//   - fixed-width integer types throughout.
//
// The NGA file starts at degree 2; degrees 0 and 1 are not stored in it. This
// parser does not invent them: has(0, 0), has(1, 0) and has(1, 1) are false.
#ifndef GRAVLAB_EGM2008_HPP
#define GRAVLAB_EGM2008_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gravlab {
namespace egm2008 {

// Result of every fallible operation. Values are stable; describe() gives a
// short English description.
enum class Status : std::uint8_t {
  ok = 0U,
  empty_line,               // parse_line: blank line, nothing parsed
  wrong_field_count,        // record does not have exactly six fields
  invalid_degree,           // degree field is not an integer >= 0
  invalid_order,            // order field is not an integer >= 0
  order_exceeds_degree,     // m > n
  invalid_real,             // malformed real (incl. inf, nan, hex, junk)
  real_out_of_range,        // real does not fit a double
  line_too_long,            // line longer than kMaxLineLength
  duplicate_coefficient,    // (n, m) appears more than once
  missing_coefficient,      // completeness check found a gap
  degree_exceeds_capacity,  // record or option needs more storage than bound
  not_present,              // get(): (n, m) in range but never set
  out_of_range,             // get()/set(): (n, m) outside the table
  invalid_argument,         // bad options or storage
  table_not_bound,          // table has no storage attached
  no_records,               // input had no coefficient records
  lfs_pointer,              // file is a Git LFS pointer, not the data
  file_open_failed,         // file could not be opened
  io_error                  // read error
};

// Short, static, English description of a status. Never returns nullptr.
const char* describe(Status status) noexcept;

// Longest line accepted, excluding the line terminator. NGA lines are 99
// characters.
constexpr std::size_t kMaxLineLength = 256U;

// Longest real token accepted. The longest in the NGA file is 22 characters.
constexpr std::size_t kMaxRealLength = 64U;

// One record of the coefficient file.
struct Coefficient {
  std::int32_t n{0};
  std::int32_t m{0};
  double C{0.0};
  double S{0.0};
  double sigma_C{0.0};
  double sigma_S{0.0};
};

// Converts one Fortran-style real token to double. Accepts 'D', 'd', 'E' or
// 'e' as the exponent marker, an optional leading sign, and an optional
// exponent. The whole token must be consumed. On anything but Status::ok,
// value is left unchanged.
[[nodiscard]] Status parse_fortran_double(std::string_view token,
                                          double& value) noexcept;

// Parses one line. Returns Status::empty_line for an empty or whitespace-only
// line. On anything but Status::ok, record is left unchanged.
[[nodiscard]] Status parse_line(std::string_view line,
                                Coefficient& record) noexcept;

// Largest degree a table can hold. Keeps the slot arithmetic below free of
// overflow even with a 32-bit std::size_t (32768 * 32769 < 2^32). Far above
// any published gravity model.
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

// Index of (n, m) in the triangular arrays: n(n+1)/2 + m. The caller must
// ensure 0 <= m <= n <= kMaxCapacity.
constexpr std::size_t slot_index(std::int32_t n, std::int32_t m) noexcept {
  const std::size_t un{static_cast<std::size_t>(n)};
  return ((un * (un + 1U)) / 2U) + static_cast<std::size_t>(m);
}

// Caller-owned arrays the table writes into. Each must hold at least `length`
// elements and outlive the table. Obtain from Storage<>::buffers(), or point
// at memory the program sets aside once at start-up.
struct Buffers {
  double* C{nullptr};
  double* S{nullptr};
  double* sigma_C{nullptr};
  double* sigma_S{nullptr};
  bool* present{nullptr};
  std::size_t length{0U};
};

// Fixed-size storage for degrees 0..Capacity. Full EGM2008 (Capacity = 2190)
// is about 79 MB, so give it static storage duration in the application, not
// automatic (stack) storage.
template <std::int32_t Capacity>
class Storage {
  static_assert((Capacity >= 0) && (Capacity <= kMaxCapacity),
                "Capacity must be in 0..kMaxCapacity");

 public:
  static constexpr std::size_t kSlots{slot_count(Capacity)};

  // Lvalue-only (MISRA C++:2023 Rule 6.8.4): the pointers refer to this
  // object, so they must not be taken from a temporary.
  Buffers buffers() & noexcept {
    Buffers b{};
    b.C = C_.data();
    b.S = S_.data();
    b.sigma_C = sigma_C_.data();
    b.sigma_S = sigma_S_.data();
    b.present = present_.data();
    b.length = kSlots;
    return b;
  }

 private:
  std::array<double, kSlots> C_{};
  std::array<double, kSlots> S_{};
  std::array<double, kSlots> sigma_C_{};
  std::array<double, kSlots> sigma_S_{};
  std::array<bool, kSlots> present_{};
};

// Non-owning view of coefficient storage, lower-triangular, indexed with
// slot_index(). Copying the table copies the view, not the data.
class CoefficientTable {
 public:
  // An unbound table: every query fails with Status::table_not_bound.
  CoefficientTable() noexcept = default;

  // Attaches storage for degrees 0..capacity and clears it. Fails with
  // Status::invalid_argument if a pointer is null, capacity is outside
  // 0..kMaxCapacity, or buffers.length < slot_count(capacity); the table is
  // then left unbound.
  [[nodiscard]] Status bind(const Buffers& buffers,
                            std::int32_t capacity) noexcept;

  // Marks every slot empty and zeroes the values. Keeps the storage.
  void clear() noexcept;

  bool is_bound() const noexcept { return capacity_ >= 0; }

  // Highest degree the storage can hold; -1 when unbound.
  std::int32_t capacity() const noexcept { return capacity_; }

  // Highest degree stored so far; -1 when empty.
  std::int32_t max_degree() const noexcept { return max_degree_; }

  // True if (n, m) is within capacity and has been set.
  bool has(std::int32_t n, std::int32_t m) const noexcept;

  // Copies (n, m) into record. Status::out_of_range outside capacity,
  // Status::not_present for an empty slot.
  [[nodiscard]] Status get(std::int32_t n, std::int32_t m,
                           Coefficient& record) const noexcept;

  // Stores a record. Status::out_of_range if (n, m) is outside capacity,
  // Status::duplicate_coefficient (nothing changed) if already set.
  [[nodiscard]] Status set(const Coefficient& record) noexcept;

  // Read-only access to the triangular arrays (length slot_count(capacity()))
  // for a gravity-field evaluator. nullptr when unbound.
  const double* C_data() const noexcept { return buffers_.C; }
  const double* S_data() const noexcept { return buffers_.S; }
  const double* sigma_C_data() const noexcept { return buffers_.sigma_C; }
  const double* sigma_S_data() const noexcept { return buffers_.sigma_S; }

 private:
  bool in_range(std::int32_t n, std::int32_t m) const noexcept;

  Buffers buffers_{};
  std::int32_t capacity_{-1};
  std::int32_t max_degree_{-1};
};

struct LoadOptions {
  // Keep degrees up to this value; -1 keeps every degree in the input. When
  // set, reading stops at the first record with n > max_degree, which is
  // correct for the NGA file because it is sorted by degree. If an unsorted
  // file drops rows because of this, the completeness check reports it.
  // Must not exceed the table's capacity.
  std::int32_t max_degree{-1};

  // Require every (n, m) with min_degree <= n <= D and 0 <= m <= n to be
  // present, where D is max_degree if set, else the highest degree read.
  // Duplicates are always an error.
  bool require_complete{true};

  // Lowest degree the completeness check expects. The NGA file starts at 2.
  std::int32_t min_degree{2};
};

// Outcome of a load. On failure, line is the 1-based line number of the
// offending line (0 if not tied to a line), and n, m identify the coefficient
// for duplicate_coefficient and missing_coefficient (otherwise -1).
struct LoadResult {
  Status status{Status::ok};
  std::uint64_t line{0U};
  std::int32_t n{-1};
  std::int32_t m{-1};
  std::uint64_t records{0U};  // records stored
};

// Parses coefficient text held in memory. The table is cleared first. On
// failure the table may hold a partial load; check the result.
[[nodiscard]] LoadResult load_text(std::string_view text,
                                   CoefficientTable& table,
                                   const LoadOptions& options) noexcept;

// Reads a coefficient file (null-terminated path). The table is cleared
// first. Reading uses a fixed 64 KiB buffer and no dynamic memory.
[[nodiscard]] LoadResult load_file(const char* path, CoefficientTable& table,
                                   const LoadOptions& options) noexcept;

}  // namespace egm2008
}  // namespace gravlab

#endif  // GRAVLAB_EGM2008_HPP
