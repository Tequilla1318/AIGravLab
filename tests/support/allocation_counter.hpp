// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Test support, outside the MISRA scope. Linking allocation_counter.cpp
// replaces every global operator new/delete with a version that counts
// allocations, so tests can prove that library code allocates nothing:
//
//   const long before = gravlab_test::allocation_count();
//   ... call the library ...
//   EXPECT_EQ(gravlab_test::allocation_count() - before, 0);
#ifndef GRAVLAB_TEST_ALLOCATION_COUNTER_HPP
#define GRAVLAB_TEST_ALLOCATION_COUNTER_HPP

namespace gravlab_test {

// Number of global operator new calls (all forms) since program start.
long allocation_count() noexcept;

}  // namespace gravlab_test

#endif  // GRAVLAB_TEST_ALLOCATION_COUNTER_HPP
