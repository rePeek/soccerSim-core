// Standalone allocator interposition: never linked into Simulation suites.
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "football/ball/ball.hpp"

namespace {
thread_local std::size_t* allocation_count = nullptr;
struct CountAllocations {
  std::size_t count = 0;
  CountAllocations() { allocation_count = &count; }
  ~CountAllocations() { allocation_count = nullptr; }
};
}
void* operator new(std::size_t size) {
  if (allocation_count) ++*allocation_count;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
  if (allocation_count) ++*allocation_count;
  const auto align = static_cast<std::size_t>(alignment);
  if (size == 0) size = 1;
  if (size > std::numeric_limits<std::size_t>::max() - (align - 1)) throw std::bad_alloc();
  const auto rounded = ((size + align - 1) / align) * align;
  if (void* p = std::aligned_alloc(align, rounded)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

TEST_CASE("dynamic collider merge reuses capacity without per-tick input allocation", "[ball][allocation]") {
  using namespace football::ball;
  Ball ball{football::model::Pitch{}};
  const BallState high{{0, 0, 10000}, blunted::Vector3(0), blunted::Vector3(0), blunted::Quaternion{}};
  for (const std::size_t count : {66u, 300u}) {
    std::vector<ColliderMotion> motions;
    for (std::size_t i = 0; i < count; ++i) {
      const Sphere shape{{1000, 1000, static_cast<float>(i)}, .2f};
      motions.push_back({static_cast<ColliderId>(100 + i), shape, shape, {.35f, .45f}});
    }
    ball.Reset(high);
    ball.Step(BallTickInput{motions, {}}); // Oversized rosters may grow once.
    std::size_t allocations = 0;
    std::size_t contacts = 0;
    {
      CountAllocations measured;
      for (int step = 0; step < 100; ++step) {
        const auto result = ball.Step(BallTickInput{motions, {}});
        contacts += result.contacts.size();
      }
      allocations = measured.count;
    }
    REQUIRE(contacts == 0);
    REQUIRE(allocations == 0);
  }
  // This proves no input merge allocations. Contact-result vectors and static
  // prediction hits are intentionally outside this no-contact measured scope.
}
