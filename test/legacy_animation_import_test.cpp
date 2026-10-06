#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "animation/animation.hpp"
#include "import/legacy_text.hpp"
#include "import/legacy_value_codec.hpp"
#include "import/legacy_xml.hpp"

using namespace blunted;

TEST_CASE("legacy XML retains duplicates ordering recursion and whitespace semantics",
          "[animation][import]") {
  XMLLoader loader;
  const XMLTree tree = loader.Load(
      "<z> 1 \n 2\t3 </z><a><a>first value</a><b>second value</b></a>"
      "<z>fourth value</z>");
  REQUIRE(tree.children.size() == 3);
  CHECK(tree.value.empty());
  CHECK(tree.children.begin()->first == "a");
  const auto& nested = tree.children.begin()->second;
  REQUIRE(nested.children.size() == 2);
  CHECK(nested.children.find("a")->second.value == "firstvalue");
  CHECK(nested.children.find("b")->second.value == "secondvalue");
  const auto [begin, end] = tree.children.equal_range("z");
  auto entry = begin;
  CHECK(entry++->second.value == "123");
  CHECK(entry++->second.value == "fourthvalue");
  CHECK(entry == end);
  CHECK(loader.Load(" a b\r\n\t c ").value == "abc");
  CHECK(loader.Load("").children.empty());
}

TEST_CASE("legacy import operational failures are catchable exceptions",
          "[animation][import]") {
  XMLLoader loader;
  CHECK_THROWS_AS(loader.Load("<tag>value"), std::runtime_error);
  CHECK_THROWS_AS(loader.Load("<outer><inner>value</outer>"), std::runtime_error);
  CHECK_THROWS_AS(loader.LoadFile("missing-legacy-import-source.xml"), std::runtime_error);
  CHECK_THROWS_AS(BodyPartFromString("not_a_body_part"), std::runtime_error);
  CHECK_THROWS_AS(BodyPartString(static_cast<BodyPart>(-1)), std::runtime_error);
  Animation animation;
  CHECK_THROWS_AS(animation.Load("missing-legacy-import-source.anim"), std::runtime_error);
}

TEST_CASE("legacy tokenization appends nonempty delimiter-separated fields",
          "[animation][import]") {
  std::vector<std::string> tokens{"existing"};
  tokenize(",,one,, two ,\tthree,,", tokens, ",");
  REQUIRE(tokens.size() == 4);
  CHECK(tokens[0] == "existing");
  CHECK(tokens[1] == "one");
  CHECK(tokens[2] == " two ");
  CHECK(tokens[3] == "\tthree");
  tokenize(",,,", tokens, ",");
  CHECK(tokens.size() == 4);
}

TEST_CASE("legacy vector quaternion and decimal codecs retain numerical semantics",
          "[animation][import]") {
  CHECK(GetVectorFromString("") == Vector3(0.0f));
  CHECK(GetVectorFromString("1.25") == Vector3(1.25f, 0.0f, 0.0f));
  CHECK(GetVectorFromString("1.25, -2.5") == Vector3(1.25f, -2.5f, 0.0f));
  CHECK(GetVectorFromString("1.25, -2.5, 3tail") == Vector3(1.25f, -2.5f, 3.0f));
  CHECK(GetStringFromVector(Vector3(1.25f, -2.5f, 3.0f)) ==
        "1.250000, -2.500000, 3.000000");
  CHECK(real_to_str(0.8765432f) == "0.876543");
  CHECK(int_to_str(-27) == "-27");
  const Quaternion rotation = GetQuaternionFromString("90, 0, 0, 1");
  Quaternion expected;
  expected.SetAngleAxis(90.0f / 360.0f * 2.0f * pi, Vector3(0.0f, 0.0f, 1.0f));
  for (int i = 0; i < 4; ++i) CHECK(rotation.elements[i] == expected.elements[i]);
}
