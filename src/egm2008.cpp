// SPDX-License-Identifier: AGPL-3.0-or-later
//
// MISRA C++:2023 notes for this file are in docs/misra.md. In short: no
// dynamic memory, no exceptions, no C library string or I/O functions, no
// <cctype>; numbers are converted with std::from_chars; files are read with
// POSIX open/read/close.
#include "gravlab/egm2008.hpp"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace gravlab {
namespace egm2008 {
namespace {

constexpr std::size_t kFieldCount{6U};
constexpr std::size_t kRealFieldCount{4U};
constexpr std::size_t kFirstRealField{2U};

// File reads go through this buffer; lines are split out of it in place.
constexpr std::size_t kReadBufferSize{65536U};

// string_view literals, so no character array decays to a pointer when the
// constants are built (Rule 7.11.2).
using std::string_view_literals::operator""sv;
constexpr std::string_view kLfsPrefix{"version https://git-lfs.github.com/spec/"sv};
constexpr std::string_view kDigits{"0123456789"sv};

constexpr std::array<const char*, 20U> kDescriptions{{
    "ok",
    "empty line",
    "wrong number of fields (expected n m C S sigma_C sigma_S)",
    "invalid degree",
    "invalid order",
    "order exceeds degree",
    "invalid real number",
    "real number out of double range",
    "line too long",
    "duplicate coefficient",
    "missing coefficient",
    "degree exceeds table capacity",
    "coefficient not present",
    "coefficient index out of range",
    "invalid argument",
    "table has no storage bound",
    "no coefficient records found",
    "file is a Git LFS pointer; run `git lfs pull` to fetch the data",
    "cannot open file",
    "I/O error",
}};
static_assert((static_cast<std::size_t>(Status::io_error) + 1U) ==
                  kDescriptions.size(),
              "kDescriptions must cover every Status");

constexpr bool is_space(char c) noexcept {
  return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') ||
         (c == '\f') || (c == '\v');
}

constexpr bool is_digit(char c) noexcept {
  return kDigits.find(c) != std::string_view::npos;
}

// Splits on whitespace into at most kFieldCount fields. Returns the number of
// fields found, or kFieldCount + 1 if there are more.
std::size_t split_fields(std::string_view line,
                         std::array<std::string_view, kFieldCount>& fields) noexcept {
  std::size_t count{0U};
  std::size_t i{0U};
  const std::size_t size{line.size()};
  bool too_many{false};
  while ((i < size) && (!too_many)) {
    while ((i < size) && is_space(line[i])) {
      ++i;
    }
    if (i < size) {
      const std::size_t start{i};
      while ((i < size) && (!is_space(line[i]))) {
        ++i;
      }
      if (count < kFieldCount) {
        fields[count] = line.substr(start, i - start);
        ++count;
      } else {
        too_many = true;
      }
    }
  }
  return too_many ? (kFieldCount + 1U) : count;
}

// Parses a non-negative decimal integer with an optional leading '+'.
bool parse_non_negative(std::string_view token, std::int32_t& value) noexcept {
  std::string_view digits{token};
  if ((!digits.empty()) && (digits.front() == '+')) {
    digits.remove_prefix(1U);
  }
  bool ok{false};
  if ((!digits.empty()) && is_digit(digits.front())) {
    std::int32_t parsed{0};
    const char* const first{digits.data()};
    const char* const last{first + digits.size()};
    const std::from_chars_result r{std::from_chars(first, last, parsed)};
    if ((r.ec == std::errc{}) && (r.ptr == last)) {
      value = parsed;
      ok = true;
    }
  }
  return ok;
}

// Line-by-line reader over text in memory.
class TextSource final {
 public:
  explicit TextSource(std::string_view text) noexcept : text_{text} {}

  // Yields the next line without its '\n'. Sets end to true, and returns
  // Status::ok, once the text is exhausted.
  Status next_line(std::string_view& line, bool& end) noexcept {
    Status status{Status::ok};
    end = false;
    if (pos_ >= text_.size()) {
      end = true;
    } else {
      const std::size_t newline{text_.find('\n', pos_)};
      const std::size_t stop{(newline == std::string_view::npos) ? text_.size()
                                                                 : newline};
      const std::size_t length{stop - pos_};
      if (length > kMaxLineLength) {
        status = Status::line_too_long;
      } else {
        line = text_.substr(pos_, length);
        pos_ = (newline == std::string_view::npos) ? text_.size() : (stop + 1U);
      }
    }
    return status;
  }

 private:
  std::string_view text_;
  std::size_t pos_{0U};
};

// Line-by-line reader over a file, through a fixed buffer.
//
// Uses POSIX open/read/close rather than std::filebuf: libc++'s filebuf
// allocates its buffer in its constructor, so no standard C++ file API is
// heap-free on every standard library, and MISRA C++:2023 Rule 30.0.1 rules
// out <cstdio>. See docs/misra.md, N1.
class FileSource final {
 public:
  FileSource() noexcept = default;
  FileSource(const FileSource&) = delete;
  FileSource(FileSource&&) = delete;
  FileSource& operator=(const FileSource&) = delete;
  FileSource& operator=(FileSource&&) = delete;
  ~FileSource() {
    if (fd_ >= 0) {
      // Nothing useful can be done if closing a read-only file fails.
      static_cast<void>(::close(fd_));
    }
  }

  bool open(const char* path) noexcept {
    fd_ = ::open(path, O_RDONLY);
    return fd_ >= 0;
  }

  Status next_line(std::string_view& line, bool& end) noexcept {
    Status status{Status::ok};
    bool done{false};
    end = false;
    while (!done) {
      const std::string_view window{buffer_.data() + begin_, end_ - begin_};
      const std::size_t newline{window.find('\n')};
      if (newline != std::string_view::npos) {
        if (newline > kMaxLineLength) {
          status = Status::line_too_long;
        } else {
          line = window.substr(0U, newline);
          begin_ = begin_ + newline + 1U;
        }
        done = true;
      } else if (window.size() > kMaxLineLength) {
        status = Status::line_too_long;
        done = true;
      } else if (eof_) {
        if (window.empty()) {
          end = true;
        } else {
          line = window;  // last line has no terminator
          begin_ = end_;
        }
        done = true;
      } else {
        status = refill();
        if (status != Status::ok) {
          done = true;
        }
      }
    }
    return status;
  }

 private:
  // Moves the unread tail to the front of the buffer and reads more.
  Status refill() noexcept {
    Status status{Status::ok};
    const std::size_t remaining{end_ - begin_};
    for (std::size_t i{0U}; i < remaining; ++i) {
      buffer_[i] = buffer_[begin_ + i];
    }
    begin_ = 0U;
    end_ = remaining;
    const std::size_t room{kReadBufferSize - end_};
    ssize_t got{-1};
    bool retry{true};
    while (retry) {
      got = ::read(fd_, buffer_.data() + end_, room);
      retry = (got < 0) && (errno == EINTR);  // interrupted by a signal
    }
    if (got < 0) {
      status = Status::io_error;
    } else if (got == 0) {
      eof_ = true;
    } else {
      end_ = end_ + static_cast<std::size_t>(got);
    }
    return status;
  }

  // POSIX file descriptors are int; std::int32_t names the same type here
  // without using the basic type name (Rule 6.9.2).
  static_assert(std::is_same<std::int32_t, int>::value,
                "file descriptors must be std::int32_t on this platform");
  std::int32_t fd_{-1};
  std::array<char, kReadBufferSize> buffer_{};
  std::size_t begin_{0U};
  std::size_t end_{0U};
  bool eof_{false};
};

bool starts_with(std::string_view text, std::string_view prefix) noexcept {
  return (text.size() >= prefix.size()) &&
         (text.substr(0U, prefix.size()) == prefix);
}

template <typename Source>
LoadResult load_lines(Source& source, CoefficientTable& table,
                      const LoadOptions& options) noexcept {
  LoadResult result{};
  if (!table.is_bound()) {
    result.status = Status::table_not_bound;
    return result;
  }
  if ((options.max_degree < -1) || (options.min_degree < 0)) {
    result.status = Status::invalid_argument;
    return result;
  }
  if (options.max_degree > table.capacity()) {
    result.status = Status::degree_exceeds_capacity;
    return result;
  }
  table.clear();

  const bool truncate{options.max_degree >= 0};
  const std::int32_t limit{truncate ? options.max_degree : table.capacity()};
  std::int32_t highest{-1};
  std::uint64_t line_number{0U};
  bool finished{false};

  while (!finished) {
    std::string_view line{};
    bool end{false};
    const Status read_status{source.next_line(line, end)};
    if (read_status != Status::ok) {
      result.status = read_status;
      result.line = line_number + 1U;
      return result;
    }
    if (end) {
      finished = true;
    } else {
      ++line_number;
      if ((line_number == 1U) && starts_with(line, kLfsPrefix)) {
        result.status = Status::lfs_pointer;
        result.line = line_number;
        return result;
      }
      Coefficient record{};
      const Status parse_status{parse_line(line, record)};
      if (parse_status == Status::empty_line) {
        // Blank lines are allowed anywhere.
      } else if (parse_status != Status::ok) {
        result.status = parse_status;
        result.line = line_number;
        return result;
      } else if (record.n > limit) {
        if (truncate) {
          finished = true;  // the file is sorted by degree
        } else {
          result.status = Status::degree_exceeds_capacity;
          result.line = line_number;
          result.n = record.n;
          result.m = record.m;
          return result;
        }
      } else {
        const Status set_status{table.set(record)};
        if (set_status != Status::ok) {
          result.status = set_status;
          result.line = line_number;
          result.n = record.n;
          result.m = record.m;
          return result;
        }
        ++result.records;
        highest = std::max(highest, record.n);
      }
    }
  }

  if (result.records == 0U) {
    result.status = Status::no_records;
    return result;
  }

  if (options.require_complete) {
    const std::int32_t last{truncate ? options.max_degree : highest};
    for (std::int32_t n{options.min_degree}; n <= last; ++n) {
      for (std::int32_t m{0}; m <= n; ++m) {
        if (!table.has(n, m)) {
          result.status = Status::missing_coefficient;
          result.n = n;
          result.m = m;
          return result;
        }
      }
    }
  }
  return result;
}

}  // namespace

const char* describe(Status status) noexcept {
  const std::size_t index{static_cast<std::size_t>(status)};
  return (index < kDescriptions.size()) ? kDescriptions[index] : "unknown status";
}

Status parse_fortran_double(std::string_view token, double& value) noexcept {
  if (token.empty() || (token.size() > kMaxRealLength)) {
    return Status::invalid_real;
  }

  std::array<char, kMaxRealLength> buffer{};
  std::size_t length{0U};
  std::size_t mantissa_start{0U};

  // Optional sign. from_chars accepts '-' but not '+'.
  if ((token[0U] == '+') || (token[0U] == '-')) {
    if (token[0U] == '-') {
      buffer[length] = '-';
      ++length;
    }
    mantissa_start = 1U;
  }
  // The mantissa must start with a digit or '.', which rules out "inf",
  // "nan", a second sign and an empty mantissa.
  if ((mantissa_start == token.size()) ||
      (!(is_digit(token[mantissa_start]) || (token[mantissa_start] == '.')))) {
    return Status::invalid_real;
  }
  // length <= mantissa_start <= i here, so buffer[length] stays in bounds.
  for (std::size_t i{mantissa_start}; i < token.size(); ++i) {
    const char c{token[i]};
    buffer[length] = ((c == 'D') || (c == 'd')) ? 'e' : c;
    ++length;
  }

  double parsed{0.0};
  const char* const first{buffer.data()};
  const char* const last{first + length};
  const std::from_chars_result r{
      std::from_chars(first, last, parsed, std::chars_format::general)};
  Status status{Status::ok};
  if (r.ec == std::errc::result_out_of_range) {
    status = Status::real_out_of_range;
  } else if ((r.ec != std::errc{}) || (r.ptr != last)) {
    status = Status::invalid_real;
  } else {
    value = parsed;
  }
  return status;
}

Status parse_line(std::string_view line, Coefficient& record) noexcept {
  std::array<std::string_view, kFieldCount> fields{};
  const std::size_t count{split_fields(line, fields)};
  if (count == 0U) {
    return Status::empty_line;
  }
  if (count != kFieldCount) {
    return Status::wrong_field_count;
  }

  Coefficient parsed{};
  if (!parse_non_negative(fields[0U], parsed.n)) {
    return Status::invalid_degree;
  }
  if (!parse_non_negative(fields[1U], parsed.m)) {
    return Status::invalid_order;
  }
  if (parsed.m > parsed.n) {
    return Status::order_exceeds_degree;
  }

  std::array<double, kRealFieldCount> reals{};
  for (std::size_t k{0U}; k < kRealFieldCount; ++k) {
    const Status status{parse_fortran_double(fields[kFirstRealField + k], reals[k])};
    if (status != Status::ok) {
      return status;
    }
  }
  parsed.C = reals[0U];
  parsed.S = reals[1U];
  parsed.sigma_C = reals[2U];
  parsed.sigma_S = reals[3U];
  record = parsed;
  return Status::ok;
}

// ---------------------------------------------------------------------------
// CoefficientTable

Status CoefficientTable::bind(const Buffers& buffers,
                              std::int32_t capacity) noexcept {
  buffers_ = Buffers{};
  capacity_ = -1;
  max_degree_ = -1;
  const bool pointers_ok{(buffers.C != nullptr) && (buffers.S != nullptr) &&
                         (buffers.sigma_C != nullptr) &&
                         (buffers.sigma_S != nullptr) &&
                         (buffers.present != nullptr)};
  if ((!pointers_ok) || (capacity < 0) || (capacity > kMaxCapacity) ||
      (buffers.length < slot_count(capacity))) {
    return Status::invalid_argument;
  }
  buffers_ = buffers;
  capacity_ = capacity;
  clear();
  return Status::ok;
}

void CoefficientTable::clear() noexcept {
  // std::fill returns void, so there is no result to discard (Rule 0.1.2).
  // Every pointer + count stays within the bound buffers (Rule 8.7.1).
  const std::size_t count{slot_count(capacity_)};
  if (count > 0U) {
    std::fill(buffers_.C, buffers_.C + count, 0.0);
    std::fill(buffers_.S, buffers_.S + count, 0.0);
    std::fill(buffers_.sigma_C, buffers_.sigma_C + count, 0.0);
    std::fill(buffers_.sigma_S, buffers_.sigma_S + count, 0.0);
    std::fill(buffers_.present, buffers_.present + count, false);
  }
  max_degree_ = -1;
}

bool CoefficientTable::in_range(std::int32_t n, std::int32_t m) const noexcept {
  return (n >= 0) && (m >= 0) && (m <= n) && (n <= capacity_);
}

bool CoefficientTable::has(std::int32_t n, std::int32_t m) const noexcept {
  return in_range(n, m) && buffers_.present[slot_index(n, m)];
}

Status CoefficientTable::get(std::int32_t n, std::int32_t m,
                             Coefficient& record) const noexcept {
  if (!is_bound()) {
    return Status::table_not_bound;
  }
  if (!in_range(n, m)) {
    return Status::out_of_range;
  }
  const std::size_t i{slot_index(n, m)};
  if (!buffers_.present[i]) {
    return Status::not_present;
  }
  record.n = n;
  record.m = m;
  record.C = buffers_.C[i];
  record.S = buffers_.S[i];
  record.sigma_C = buffers_.sigma_C[i];
  record.sigma_S = buffers_.sigma_S[i];
  return Status::ok;
}

Status CoefficientTable::set(const Coefficient& record) noexcept {
  if (!is_bound()) {
    return Status::table_not_bound;
  }
  if (!in_range(record.n, record.m)) {
    return Status::out_of_range;
  }
  const std::size_t i{slot_index(record.n, record.m)};
  if (buffers_.present[i]) {
    return Status::duplicate_coefficient;
  }
  buffers_.C[i] = record.C;
  buffers_.S[i] = record.S;
  buffers_.sigma_C[i] = record.sigma_C;
  buffers_.sigma_S[i] = record.sigma_S;
  buffers_.present[i] = true;
  max_degree_ = std::max(max_degree_, record.n);
  return Status::ok;
}

// ---------------------------------------------------------------------------
// Loading

LoadResult load_text(std::string_view text, CoefficientTable& table,
                     const LoadOptions& options) noexcept {
  TextSource source{text};
  return load_lines(source, table, options);
}

LoadResult load_file(const char* path, CoefficientTable& table,
                     const LoadOptions& options) noexcept {
  LoadResult result{};
  if (path == nullptr) {
    result.status = Status::invalid_argument;
    return result;
  }
  FileSource source{};
  if (!source.open(path)) {
    result.status = Status::file_open_failed;
    return result;
  }
  return load_lines(source, table, options);
}

}  // namespace egm2008
}  // namespace gravlab
