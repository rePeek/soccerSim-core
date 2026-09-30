#ifndef FOOTBALL_SUPPORT_DIAGNOSTICS_ASSERT_HPP
#define FOOTBALL_SUPPORT_DIAGNOSTICS_ASSERT_HPP

#include "support/diagnostics/assert.hpp"
#include <cassert>

#define CHECK(condition) assert(condition)
#define CHECK_EQ(left, right) assert((left) == (right))

#endif  // FOOTBALL_SUPPORT_DIAGNOSTICS_ASSERT_HPP
