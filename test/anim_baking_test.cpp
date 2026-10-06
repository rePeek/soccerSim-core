#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <vector>

#include "anim_baker.hpp"

namespace {
// Per-test directory; atomic creation prevents collisions between concurrent CTest runs.
class TempDirectory {
 public:
  TempDirectory() {
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
      path = std::filesystem::temp_directory_path() /
             ("football-anim-test-" + std::to_string(random()));
      if (std::filesystem::create_directory(path)) return;
    }
    throw std::runtime_error("cannot create animation test directory");
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  std::filesystem::path path;
};
std::vector<char> ReadBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot read animation artifact");
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_CASE("animation bake is deterministic and verifies against source", "[animation]") {
  using namespace football::tools;
  const std::filesystem::path data = FOOTBALL_TEST_DATA_DIR;
  TempDirectory temp;
  const auto a = temp.path / "a.simanim";
  const auto b = temp.path / "b.simanim";
  const auto first = BakeAnimations(data);
  REQUIRE_FALSE(first.empty());
  const auto first_bytes = SerializeAnimations(first);
  REQUIRE_FALSE(first_bytes.empty());
  {
    // A second independent importer/bake, not serialization of the same clips.
    const auto second = BakeAnimations(data);
    REQUIRE(SerializeAnimations(second) == first_bytes);
    REQUIRE(WriteAnimations(b, second) == 0);
  }
  REQUIRE(WriteAnimations(a, first) == 0);
  REQUIRE(ReadBytes(a) == first_bytes);
  REQUIRE(ReadBytes(a) == ReadBytes(b));
  REQUIRE(CheckAnimations(a, first) == 0);
  REQUIRE(VerifyAnimations(data, a) == 0);
  REQUIRE(VerifyAnimationSelection(data, a) == 0);

  // Valid but different artifacts must fail --check, not merely be readable.
  auto changed = first;
  changed.front().name += "-changed";
  REQUIRE(WriteAnimations(b, changed) == 0);
  REQUIRE(CheckAnimations(b, first) != 0);
  REQUIRE(CheckAnimations(temp.path / "missing.simanim", first) != 0);
  REQUIRE(WriteAnimations(temp.path / "missing" / "out.simanim", first) != 0);
}
