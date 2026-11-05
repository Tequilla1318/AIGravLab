// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Test support, outside the MISRA scope. See allocation_counter.hpp.
#include "support/allocation_counter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {
std::atomic<long> g_allocations{0};

// Every replaceable form is replaced, so all allocations go through malloc and
// every deallocation through free (mixing with the default ones upsets ASan).
void* counted_alloc(std::size_t size) noexcept {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(size == 0 ? 1 : size);
}
void* counted_aligned_alloc(std::size_t size, std::align_val_t align) noexcept {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  const std::size_t a = static_cast<std::size_t>(align);
  const std::size_t rounded = ((size == 0 ? 1 : size) + a - 1) / a * a;
  return std::aligned_alloc(a, rounded);
}
}  // namespace

long gravlab_test::allocation_count() noexcept { return g_allocations.load(); }

void* operator new(std::size_t size) {
  if (void* p = counted_alloc(size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
  if (void* p = counted_alloc(size)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted_alloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return counted_alloc(size);
}
void* operator new(std::size_t size, std::align_val_t a) {
  if (void* p = counted_aligned_alloc(size, a)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t a) {
  if (void* p = counted_aligned_alloc(size, a)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept {
  return counted_aligned_alloc(size, a);
}
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept {
  return counted_aligned_alloc(size, a);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(p);
}
