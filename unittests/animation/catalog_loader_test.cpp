#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "football/animation/library.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
Json ReadJson(const fs::path& path) { std::ifstream in(path); return Json::parse(in); }
void SaveJson(const fs::path& path, const Json& value) { std::ofstream(path) << value.dump(); }
const AnimationLibrary& Catalog() {
  static const auto library = [] {
    AnimationLibrary result;
    REQUIRE(result.LoadCatalog(GFOOTBALL_ANIMATION_CATALOG_PATH));
    return result;
  }();
  return library;
}
void PutU32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes.at(offset+i) = static_cast<unsigned char>(value >> (8*i));
}
std::uint32_t U32(const std::vector<unsigned char>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value |= std::uint32_t(bytes.at(offset+i)) << (8*i);
  return value;
}
std::vector<unsigned char> Bytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
struct Fixture {
  fs::path directory = fs::temp_directory_path() / ("football-glb-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::path manifest = directory / "manifest.json", glb;
  Json doc;
  Fixture() {
    fs::create_directories(directory);
    const fs::path source(GFOOTBALL_ANIMATION_CATALOG_PATH);
    doc = ReadJson(source);
    const auto authored = std::find_if(doc["clips"].begin(), doc["clips"].end(),
                                      [](const auto& e) { return e.contains("file"); });
    auto entry = *authored;
    entry["id"] = 0;
    doc["clips"] = Json::array({entry});
    fs::copy_file(source.parent_path() / doc["skeleton"].get<std::string>(), directory / "skeleton.json");
    doc["skeleton"] = "skeleton.json";
    glb = directory / entry["file"].get<std::string>();
    fs::copy_file(source.parent_path() / entry["file"].get<std::string>(), glb);
    Save();
  }
  ~Fixture() { std::error_code ec; fs::remove_all(directory, ec); }
  void Save() { SaveJson(manifest, doc); }
  void Alter(const std::function<void(Json&, std::vector<unsigned char>&)>& change) {
    const auto bytes = Bytes(glb);
    const auto json_size = U32(bytes, 12);
    auto json = Json::parse(bytes.begin()+20, bytes.begin()+20+json_size);
    std::vector<unsigned char> binary(bytes.begin()+28+json_size, bytes.end());
    change(json, binary);
    auto text = json.dump();
    while (text.size() % 4) text += ' ';
    while (binary.size() % 4) binary.push_back(0);
    std::vector<unsigned char> output(28 + text.size() + binary.size());
    PutU32(output, 0, 0x46546c67); PutU32(output, 4, 2); PutU32(output, 8, output.size());
    PutU32(output, 12, text.size()); PutU32(output, 16, 0x4e4f534a);
    std::copy(text.begin(), text.end(), output.begin()+20);
    PutU32(output, 20+text.size(), binary.size()); PutU32(output, 24+text.size(), 0x004e4942);
    std::copy(binary.begin(), binary.end(), output.begin()+28+text.size());
    std::ofstream out(glb, std::ios::binary);
    out.write(reinterpret_cast<const char*>(output.data()), output.size());
  }
};
std::size_t Offset(const Json& doc, const Json& accessor) {
  return doc["bufferViews"][accessor["bufferView"].get<int>()].value("byteOffset", 0u) +
         accessor.value("byteOffset", 0u);
}
float QuaternionError(const Quaternion& a, const Quaternion& b) {
  float same = 0, opposite = 0;
  for (int i = 0; i < 4; ++i) {
    same = std::max(same, std::fabs(a.elements[i]-b.elements[i]));
    opposite = std::max(opposite, std::fabs(a.elements[i]+b.elements[i]));
  }
  return std::min(same, opposite);
}
}  // namespace

TEST_CASE("C++ loads the complete source catalog without a baked cache", "[animation][glb]") {
  const auto& library = Catalog();
  const auto doc = ReadJson(GFOOTBALL_ANIMATION_CATALOG_PATH);
  REQUIRE(library.Size() == 1452);
  REQUIRE(library.ContentHash() != 0);
  for (std::size_t i = 0; i < library.Size(); ++i) {
    const auto& clip = library.Get(i);
    REQUIRE(clip.id == i);
    REQUIRE(clip.name == doc["clips"][i]["name"].get<std::string>());
    REQUIRE(clip.poses.size() == clip.frame_count);
    REQUIRE(clip.root_positions.size() == clip.frame_count);
    REQUIRE(clip.foot_support.size() == clip.frame_count);
    for (const auto& root : clip.root_positions) REQUIRE(root.coords[2] == 0);
  }
}

TEST_CASE("GLB mirrors swap matching limbs and reflect roots and contacts", "[animation][glb]") {
  const auto& library = Catalog();
  const auto doc = ReadJson(GFOOTBALL_ANIMATION_CATALOG_PATH);
  constexpr std::array<int, 14> mirrored{0,1,3,2,5,4,7,6,9,8,11,10,12,13};
  for (const auto& entry : doc["clips"]) {
    if (!entry.contains("mirror_of")) continue;
    const auto& source = library.Get(entry["mirror_of"]);
    const auto& result = library.Get(entry["id"]);
    for (std::size_t f = 0; f < source.poses.size(); ++f) {
      for (std::size_t j = 0; j < mirrored.size(); ++j) {
        const auto& q = source.poses[f].orientations[mirrored[j]];
        const Quaternion expected(q.elements[0], -q.elements[1], -q.elements[2], q.elements[3]);
        REQUIRE(QuaternionError(result.poses[f].orientations[j], expected) == 0);
      }
      REQUIRE(result.root_positions[f].coords[0] == -source.root_positions[f].coords[0]);
      REQUIRE(result.root_positions[f].coords[1] == source.root_positions[f].coords[1]);
    }
    REQUIRE(result.metadata.current_foot == 1-source.metadata.current_foot);
    REQUIRE(result.has_contact == source.has_contact);
    if (source.has_contact) {
      REQUIRE(result.contact_position.coords[0] == Catch::Approx(-source.contact_position.coords[0]).margin(2e-5));
      REQUIRE(result.contact_position.coords[1] == Catch::Approx(source.contact_position.coords[1]).margin(2e-5));
    }
  }
}

TEST_CASE("GLB-generated transitions stay close to pre-migration reference poses", "[animation][glb]") {
  // Captured from the old cache, not generated by the importer under test.
  // Canonical template sampling has float roundoff and q/-q equivalence.
  struct Reference { int id; Quaternion body; Vector3 root; };
  const std::array<Reference, 5> references{{
      {1, {.0234210f,.0603230f,.1260490f,.9899111f}, {0,-.41999996f,0}},
      {203, {.0389013f,.0451618f,.3104689f,.9487129f}, {.16158514f,-.26385486f,0}},
      {537, {.0503658f,.0245060f,-.2263042f,.9724450f}, {0,-.70342857f,0}},
      {575, {.0652354f,.0574407f,.3311619f,.9395619f}, {.11280184f,-.73312682f,0}},
      {733, {-.0450307f,.0691636f,-.9358190f,.3426834f}, {.11590537f,-.48505807f,0}}
  }};
  for (const auto& reference : references) {
    const auto& clip = Catalog().Get(reference.id);
    REQUIRE(QuaternionError(clip.poses[12].orientations[body], reference.body) < .001f);
    REQUIRE((clip.root_positions[12]-reference.root).GetLength() < 1e-6f);
  }
}

TEST_CASE("GLB edits change runtime poses and content provenance immediately", "[animation][glb]") {
  Fixture f;
  AnimationLibrary library;
  REQUIRE(library.LoadCatalog(f.manifest));
  const auto hash = library.ContentHash();
  const auto before = library.Get(0).poses.front().orientations[body];
  f.Alter([](Json& doc, auto& binary) {
    for (const auto& channel : doc["animations"][0]["channels"]) {
      if (channel["target"]["path"] != "rotation" ||
          doc["nodes"][channel["target"]["node"].template get<int>()]["name"] != "body") continue;
      const auto sampler = doc["animations"][0]["samplers"][channel["sampler"].template get<int>()];
      const auto offset = Offset(doc, doc["accessors"][sampler["output"].template get<int>()]);
      // Y-up quaternion maps to Z-up yaw +0.4 radians.
      const std::array<float,4> q{0, std::sin(.2f), 0, std::cos(.2f)};
      for (int i = 0; i < 4; ++i) PutU32(binary, offset+4*i, std::bit_cast<std::uint32_t>(q[i]));
    }
  });
  REQUIRE(library.LoadCatalog(f.manifest));
  REQUIRE(library.ContentHash() != hash);
  REQUIRE(QuaternionError(library.Get(0).poses.front().orientations[body], before) > .1f);
  REQUIRE(library.Get(0).poses.front().orientations[body].elements[2] == Catch::Approx(std::sin(.2f)));
  REQUIRE(library.Get(0).metadata.touch_frame == f.doc["clips"][0]["metadata"]["touch_frame"]);
}

TEST_CASE("Bad source catalogs fail atomically without a cache fallback", "[animation][glb]") {
  Fixture f;
  AnimationLibrary library;
  REQUIRE(library.LoadCatalog(f.manifest));
  const auto hash = library.ContentHash();
  SECTION("missing GLB") { fs::remove(f.glb); }
  SECTION("bad ID") { f.doc["clips"][0]["id"] = 7; f.Save(); }
  SECTION("missing metadata") { f.doc["clips"][0]["metadata"].erase("touch_frame"); f.Save(); }
  SECTION("unknown metadata") { f.doc["clips"][0]["metadata"]["extra"] = 0; f.Save(); }
  SECTION("path escape") { f.doc["clips"][0]["file"] = "../outside.glb"; f.Save(); }
  SECTION("mirror self-reference") {
    f.doc["clips"][0].erase("file"); f.doc["clips"][0]["mirror_of"] = 0; f.Save();
  }
  SECTION("unresolved procedural input") {
    f.doc["clips"][0].erase("file"); f.doc["clips"][0]["procedural"] = true; f.Save();
  }
  SECTION("missing GLB channels") {
    f.Alter([](auto& doc, auto&) { doc["animations"][0]["channels"].clear(); });
  }
  SECTION("wrong rest basis") {
    f.Alter([](auto& doc, auto&) { doc["nodes"][0]["rotation"] = {0,1,0,0}; });
  }
  SECTION("wrong rig") { f.Alter([](auto& doc, auto&) { doc["extras"]["rig_version"] = "wrong"; }); }
  SECTION("unsupported interpolation") {
    f.Alter([](auto& doc, auto&) { doc["animations"][0]["samplers"][0]["interpolation"] = "STEP"; });
  }
  SECTION("nonfinite sample") {
    f.Alter([](Json& doc, auto& binary) {
      const auto& accessor = doc["accessors"][doc["animations"][0]["samplers"][0]["output"].get<int>()];
      PutU32(binary, Offset(doc, accessor), std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    });
  }
  SECTION("wrong timebase") {
    f.Alter([](Json& doc, auto& binary) {
      const auto& accessor = doc["accessors"][doc["animations"][0]["samplers"][0]["input"].get<int>()];
      PutU32(binary, Offset(doc, accessor)+4, std::bit_cast<std::uint32_t>(.02f));
    });
  }
  SECTION("accessor overrun") {
    f.Alter([](Json& doc, auto&) {
      doc["accessors"][doc["animations"][0]["samplers"][0]["output"].get<int>()]["count"] = 1000000;
    });
  }
  SECTION("external buffer") {
    f.Alter([](auto& doc, auto&) { doc["buffers"][0]["uri"] = "external.bin"; });
  }
  SECTION("duplicate JSON key") {
    std::ofstream(f.manifest) << "{\"schema\":1,\"schema\":2}";
  }
  SECTION("invalid touch frame") {
    f.doc["clips"][0]["metadata"]["touch_frame"] = 100000; f.Save();
  }
  REQUIRE_FALSE(library.LoadCatalog(f.manifest));
  REQUIRE(library.Size() == 1);
  REQUIRE(library.ContentHash() == hash);
}

TEST_CASE("Catalog import is deterministic across independent libraries", "[animation][glb]") {
  AnimationLibrary library;
  REQUIRE(library.LoadCatalog(GFOOTBALL_ANIMATION_CATALOG_PATH));
  REQUIRE(library.ContentHash() == Catalog().ContentHash());
}

TEST_CASE("Library Load accepts only catalogs and rejects legacy binary artifacts", "[animation][glb]") {
  Fixture f;
  AnimationLibrary library;
  REQUIRE(library.Load(f.manifest));
  const auto hash = library.ContentHash();
  const auto old_artifact = f.directory / "legacy.simanim";
  // A valid empty v7 artifact used to load successfully. Construct its header
  // explicitly so this test never depends on the removed binary writer/reader.
  const std::array<unsigned char, 16> header{
      'S', 'I', 'M', 'A', 'N', 'I', 'M', '1', 7, 0, 0, 0, 0, 0, 0, 0};
  {
    std::ofstream out(old_artifact, std::ios::binary);
    out.write(reinterpret_cast<const char*>(header.data()), header.size());
  }
  REQUIRE_FALSE(library.Load(old_artifact));
  REQUIRE_FALSE(library.LoadCatalog(old_artifact));
  REQUIRE(library.Size() == 1);
  REQUIRE(library.ContentHash() == hash);
}

TEST_CASE("Procedural variants read actual GLB templates, not legacy assets", "[animation][glb]") {
  Fixture f;
  const fs::path source(GFOOTBALL_ANIMATION_CATALOG_PATH);
  const auto original = ReadJson(source);
  auto entry = original["clips"][575]; // sprint -> walk with a 45 degree turn
  entry["id"] = 0;
  f.doc["clips"] = Json::array({entry});
  const auto templates = source.parent_path() / original["motion_templates"].get<std::string>();
  fs::copy(templates.parent_path(), f.directory / "templates", fs::copy_options::recursive);
  auto template_doc = ReadJson(f.directory / "templates/manifest.json");
  template_doc["skeleton"] = "../skeleton.json";
  SaveJson(f.directory / "templates/manifest.json", template_doc);
  f.doc["motion_templates"] = "templates/manifest.json";
  f.doc["generation_keys"] = "templates/generation_keys.json";
  f.Save();
  AnimationLibrary library;
  REQUIRE(library.LoadCatalog(f.manifest));
  const auto hash = library.ContentHash();
  const auto before = library.Get(0).poses.front().orientations[body];
  f.glb = f.directory / "templates/06_sprintD000.glb";
  f.Alter([](Json& doc, auto& binary) {
    for (const auto& channel : doc["animations"][0]["channels"]) {
      if (channel["target"]["path"] != "rotation" ||
          doc["nodes"][channel["target"]["node"].template get<int>()]["name"] != "body") continue;
      const auto sampler = doc["animations"][0]["samplers"][channel["sampler"].template get<int>()];
      const auto offset = Offset(doc, doc["accessors"][sampler["output"].template get<int>()]);
      const std::array<float,4> q{std::sin(.2f), 0, 0, std::cos(.2f)};
      for (int i = 0; i < 4; ++i) PutU32(binary, offset+4*i, std::bit_cast<std::uint32_t>(q[i]));
    }
  });
  REQUIRE(library.LoadCatalog(f.manifest));
  REQUIRE(library.ContentHash() != hash);
  REQUIRE(QuaternionError(library.Get(0).poses.front().orientations[body], before) > .1f);
  const auto modified_hash = library.ContentHash();
  fs::remove(f.glb);
  REQUIRE_FALSE(library.LoadCatalog(f.manifest));
  REQUIRE(library.ContentHash() == modified_hash);
}
