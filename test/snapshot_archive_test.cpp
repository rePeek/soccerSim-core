#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/recording/snapshot_archive.hpp"

namespace {
using namespace football::app::recording;
using namespace football::sim::observation;
using football::sim::Tick;
using Bytes = std::vector<std::uint8_t>;

struct TempFile {
  std::filesystem::path path;
  TempFile() {
    static std::atomic<unsigned> next{0};
    path = std::filesystem::temp_directory_path() / ("soccer-snapshot-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(next++) + ".snap");
  }
  ~TempFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
};

SnapshotMetadata Metadata(std::size_t count = 3) {
  SnapshotMetadata metadata;
  for (std::size_t i = 0; i < count; ++i) metadata.player_ids.push_back(static_cast<std::uint32_t>(1000000000 + i));
  metadata.pitch = football::model::Pitch(110, 72, 0.03f, 1.7f, 0.04f);
  metadata.animation_library_hash = UINT64_C(0x123456789abcdef0);
  return metadata;
}
SnapshotRecord Record(std::uint64_t step, std::size_t count = 3) {
  SnapshotRecord record;
  record.stamp = {step, Tick{step / 3 + 10000000000ULL}, step / 101};
  const auto v = static_cast<float>(step);
  record.snapshot.ball = {{v, -0.0f, 2}, {3, v, 5}, {6, 7, v}};
  record.snapshot.players.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto& p = record.snapshot.players[i];
    const float n = static_cast<float>(i);
    p.position = {v, n, 0}; p.velocity = {n, v, 1};
    p.facing = {1, n, 0}; p.body_facing = {n, 1, -0.0f};
    p.animation_id = i % 2 ? -1 : static_cast<AnimationId>(i + 4);
    p.frame = static_cast<std::uint32_t>(step + i);
    p.active = i % 2 == 0; p.has_possession = i == step % count;
  }
  return record;
}
void SameVector(const blunted::Vector3& actual, const blunted::Vector3& expected) {
  REQUIRE(std::memcmp(actual.coords, expected.coords, sizeof(actual.coords)) == 0);
}
void SameRecord(const SnapshotRecord& a, const SnapshotRecord& e) {
  REQUIRE(a.stamp.step_index == e.stamp.step_index);
  REQUIRE(a.stamp.timeline_tick == e.stamp.timeline_tick);
  REQUIRE(a.stamp.generation == e.stamp.generation);
  SameVector(a.snapshot.ball.position, e.snapshot.ball.position);
  SameVector(a.snapshot.ball.velocity, e.snapshot.ball.velocity);
  SameVector(a.snapshot.ball.angular_velocity, e.snapshot.ball.angular_velocity);
  REQUIRE(a.snapshot.players.size() == e.snapshot.players.size());
  for (std::size_t i = 0; i < e.snapshot.players.size(); ++i) {
    const auto& p = a.snapshot.players[i]; const auto& q = e.snapshot.players[i];
    SameVector(p.position, q.position); SameVector(p.velocity, q.velocity);
    SameVector(p.facing, q.facing); SameVector(p.body_facing, q.body_facing);
    REQUIRE(p.animation_id == q.animation_id); REQUIRE(p.frame == q.frame);
    REQUIRE(p.active == q.active); REQUIRE(p.has_possession == q.has_possession);
  }
}
Bytes Load(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(stream), {});
}
void Save(const std::filesystem::path& path, const Bytes& bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
std::uint64_t Get(const Bytes& bytes, std::size_t offset, unsigned width) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(bytes.at(offset + i)) << (8 * i);
  return value;
}
void Put(Bytes& bytes, std::size_t offset, std::uint64_t value, unsigned width) {
  for (unsigned i = 0; i < width; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
void Rehash(Bytes& bytes, std::size_t block_offset) {
  const auto size = Get(bytes, block_offset + 4, 8);
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t i = block_offset + 16; i < block_offset + 16 + size; ++i) {
    crc ^= bytes.at(i);
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
  }
  Put(bytes, block_offset + 12, ~crc, 4);
}
void WriteFixture(const std::filesystem::path& path, std::uint64_t count = 250,
                  std::size_t chunk = 100) {
  SnapshotArchive writer;
  writer.Open(path, Metadata(), {chunk, 2, QueueFullPolicy::Block});
  for (std::uint64_t i = 0; i < count; ++i) writer.Append(Record(i));
  writer.Close();
}
}

TEST_CASE("archive round trips full fields partial chunks and random ranges", "[archive][snapshot]") {
  TempFile file;
  WriteFixture(file.path);
  SnapshotArchiveReader reader;
  reader.Open(file.path, Metadata().animation_library_hash);
  REQUIRE(reader.Complete());
  REQUIRE(reader.RecordCount() == 250);
  REQUIRE(reader.Metadata().player_ids == Metadata().player_ids);
  REQUIRE(reader.Metadata().pitch == Metadata().pitch);
  REQUIRE(reader.Metadata().animation_library_hash == Metadata().animation_library_hash);
  const auto all = reader.ReadRange(0, std::numeric_limits<std::uint64_t>::max());
  REQUIRE(all.size() == 250);
  for (std::uint64_t i = 0; i < all.size(); ++i) SameRecord(all[i], Record(i));
  for (auto [first, last] : {std::pair(0ULL, 0ULL), {99, 101}, {199, 249}, {248, 300}, {300, 400}}) {
    const auto result = reader.ReadRange(first, last);
    const auto count = first >= 250 ? 0 : std::min(last, 249ULL) - first + 1;
    REQUIRE(result.size() == count);
    for (std::size_t i = 0; i < result.size(); ++i) SameRecord(result[i], Record(first + i));
  }
  REQUIRE_THROWS_AS(reader.ReadRange(10, 9), std::invalid_argument);
}

TEST_CASE("archive Append copies source and Flush publishes only committed chunks", "[archive][snapshot]") {
  TempFile file;
  SnapshotArchive writer;
  writer.Open(file.path, Metadata(), {100, 1, QueueFullPolicy::Block});
  auto sample = Record(0);
  writer.Append(sample);
  sample = Record(100);
  writer.Flush();
  SnapshotArchiveReader reader;
  REQUIRE_THROWS_AS(reader.Open(file.path), std::runtime_error);
  reader.Open(file.path, {}, ArchiveReadMode::RecoverPrefix);
  REQUIRE_FALSE(reader.Complete());
  REQUIRE(reader.RecordCount() == 1);
  SameRecord(reader.ReadRange(0, 0).front(), Record(0));
  writer.Append(Record(1));
  writer.Close();
  writer.Close();
  reader.Open(file.path);
  REQUIRE(reader.Complete());
  REQUIRE(reader.RecordCount() == 2);
  REQUIRE_FALSE(writer.IsOpen());
  REQUIRE_THROWS_AS(writer.Append(sample), std::logic_error);
}

TEST_CASE("archive empty sessions singleton chunks and sparse full-width step ranges", "[archive][snapshot]") {
  TempFile file;
  SnapshotArchive writer;
  writer.Open(file.path, Metadata(0), {1, 1});
  writer.Close();
  SnapshotArchiveReader reader;
  reader.Open(file.path);
  REQUIRE(reader.Complete());
  REQUIRE(reader.RecordCount() == 0);
  REQUIRE(reader.ReadRange(0, 100).empty());
  writer.Open(file.path, Metadata(0), {1, 1});
  SnapshotRecord record;
  record.stamp = {UINT64_C(1) << 40, Tick{5}, 1};
  writer.Append(record);
  record.stamp = {std::numeric_limits<std::uint64_t>::max(), Tick{100}, 2};
  writer.Append(record);
  writer.Close();
  reader.Open(file.path);
  const auto maximum = reader.ReadRange(record.stamp.step_index, record.stamp.step_index);
  REQUIRE(maximum.size() == 1);
  SameRecord(maximum.front(), record);
  REQUIRE(reader.ReadRange((UINT64_C(1) << 40) + 1, record.stamp.step_index - 1).empty());
}

TEST_CASE("archive validates startup metadata capacities stamps and lifecycle", "[archive][snapshot]") {
  TempFile file;
  SnapshotArchive writer;
  SnapshotArchiveReader reader;
  REQUIRE_THROWS_AS(writer.Append(Record(0)), std::logic_error);
  REQUIRE_THROWS_AS(writer.Flush(), std::logic_error);
  REQUIRE_THROWS_AS(reader.Metadata(), std::logic_error);
  REQUIRE_THROWS_AS(reader.Complete(), std::logic_error);
  REQUIRE_THROWS_AS(reader.RecordCount(), std::logic_error);
  REQUIRE_THROWS_AS(reader.ReadRange(0, 1), std::logic_error);
  REQUIRE_THROWS_AS(writer.Open(file.path, Metadata(), {0, 1}), std::invalid_argument);
  REQUIRE_THROWS_AS(writer.Open(file.path, Metadata(), {1, 0}), std::invalid_argument);
  REQUIRE_THROWS_AS(writer.Open(file.path, Metadata(), {std::numeric_limits<std::size_t>::max(), 1}), std::invalid_argument);
  auto duplicate = Metadata(); duplicate.player_ids[1] = duplicate.player_ids[0];
  REQUIRE_THROWS_AS(writer.Open(file.path, duplicate), std::invalid_argument);
  writer.Open(file.path, Metadata(), {1, 1});
  REQUIRE_THROWS_AS(writer.Open(file.path, Metadata()), std::logic_error);
  writer.Append(Record(1));
  REQUIRE_THROWS_AS(writer.Append(Record(1)), std::logic_error);
  REQUIRE_THROWS_AS(writer.Append(Record(2, 2)), std::invalid_argument);
  auto invalid = Record(2); invalid.stamp.timeline_tick = Tick{0};
  REQUIRE_THROWS_AS(writer.Append(invalid), std::logic_error);
  writer.Append(Record(3));
  writer.Close();
  reader.Open(file.path);
  REQUIRE(reader.RecordCount() == 2);
  REQUIRE(reader.ReadRange(2, 2).empty());
  REQUIRE_THROWS_AS(reader.Open(file.path, 123), std::runtime_error);
  REQUIRE(reader.RecordCount() == 2); // Failed re-open does not publish partial state.
}

TEST_CASE("archive rejects incompatible version rate frame hash and corrupt index", "[archive][snapshot]") {
  TempFile file;
  WriteFixture(file.path, 2, 1);
  const auto original = Load(file.path);
  SnapshotArchiveReader reader;
  for (auto [offset, value] : {std::pair(24U, 2U), {28U, 60U}, {32U, 2U}}) {
    auto bytes = original;
    Put(bytes, offset, value, 4);
    Rehash(bytes, 8);
    Save(file.path, bytes);
    REQUIRE_THROWS_AS(reader.Open(file.path, {}, ArchiveReadMode::RecoverPrefix), std::runtime_error);
  }
  auto bytes = original;
  bytes[0] ^= 1;
  Save(file.path, bytes);
  REQUIRE_THROWS_AS(reader.Open(file.path), std::runtime_error);
  bytes = original;
  const auto index_offset = Get(bytes, bytes.size() - 16, 8);
  // Change first chunk offset with valid index CRC: structural checks must reject it.
  Put(bytes, static_cast<std::size_t>(index_offset) + 40, 0, 8);
  Rehash(bytes, static_cast<std::size_t>(index_offset));
  Save(file.path, bytes);
  REQUIRE_THROWS_AS(reader.Open(file.path), std::runtime_error);
  reader.Open(file.path, {}, ArchiveReadMode::RecoverPrefix);
  REQUIRE_FALSE(reader.Complete());
  REQUIRE(reader.RecordCount() == 2);
}

TEST_CASE("truncation recovers checksum-valid prefix and never reports complete", "[archive][snapshot]") {
  TempFile file;
  WriteFixture(file.path);
  const auto bytes = Load(file.path);
  const auto data_offset = 8 + 16 + Get(bytes, 12, 8);
  const auto first_end = data_offset + 16 + Get(bytes, static_cast<std::size_t>(data_offset) + 4, 8);
  const auto second_end = first_end + 16 + Get(bytes, static_cast<std::size_t>(first_end) + 4, 8);
  for (auto [size, count] : {std::pair(data_offset, 0ULL), {data_offset + 7, 0},
       {first_end - 1, 0}, {first_end, 100}, {first_end + 40, 100},
       {second_end - 1, 100}, {second_end, 200}, {std::uint64_t(bytes.size() - 1), 250}}) {
    Save(file.path, Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size)));
    SnapshotArchiveReader reader;
    REQUIRE_THROWS_AS(reader.Open(file.path), std::runtime_error);
    reader.Open(file.path, {}, ArchiveReadMode::RecoverPrefix);
    REQUIRE_FALSE(reader.Complete());
    REQUIRE(reader.RecordCount() == count);
    const auto prefix = reader.ReadRange(0, 1000);
    REQUIRE(prefix.size() == count);
    for (std::size_t i = 0; i < prefix.size(); ++i) SameRecord(prefix[i], Record(i));
  }
  Save(file.path, Bytes(bytes.begin(), bytes.begin() + 20));
  SnapshotArchiveReader reader;
  REQUIRE_THROWS_AS(reader.Open(file.path, {}, ArchiveReadMode::RecoverPrefix), std::runtime_error);
}

TEST_CASE("chunk checksums and field bounds are checked before returning records", "[archive][snapshot]") {
  TempFile file;
  WriteFixture(file.path);
  const auto original = Load(file.path);
  const auto first = 24 + Get(original, 12, 8);
  auto bytes = original;
  bytes.at(static_cast<std::size_t>(first) + 40) ^= 1;
  Save(file.path, bytes);
  SnapshotArchiveReader reader;
  reader.Open(file.path); // Offline index open does not scan irrelevant chunks.
  REQUIRE_THROWS_AS(reader.ReadRange(0, 1), std::runtime_error);
  reader.Open(file.path, {}, ArchiveReadMode::RecoverPrefix);
  REQUIRE_FALSE(reader.Complete());
  REQUIRE(reader.RecordCount() == 0);
  bytes = original;
  Put(bytes, static_cast<std::size_t>(first) + 4, std::numeric_limits<std::uint64_t>::max(), 8);
  Save(file.path, bytes);
  reader.Open(file.path);
  REQUIRE_THROWS_AS(reader.ReadRange(0, 1), std::runtime_error);
  bytes = original;
  // First player's active byte: block 16 + chunk ranges 20 + stamp/ball 60 + player fields 56.
  bytes.at(static_cast<std::size_t>(first) + 16 + 20 + 60 + 56) = 2;
  Rehash(bytes, static_cast<std::size_t>(first));
  Save(file.path, bytes);
  reader.Open(file.path);
  REQUIRE_THROWS_AS(reader.ReadRange(0, 1), std::runtime_error);
}

TEST_CASE("disk open and write errors are explicit and do not leave running workers", "[archive][snapshot]") {
  TempFile file;
  SnapshotArchive writer;
  REQUIRE_THROWS_AS(writer.Open(file.path / "missing" / "match.snap", Metadata()), std::runtime_error);
  REQUIRE_FALSE(writer.IsOpen());
  // Linux's always-full sink exercises a real write/flush failure, not a fake stream.
  if (std::filesystem::exists("/dev/full")) {
    REQUIRE_THROWS_AS(writer.Open("/dev/full", Metadata()), std::runtime_error);
    REQUIRE_FALSE(writer.IsOpen());
  }
  writer.Open(file.path, Metadata());
  writer.Append(Record(0));
  writer.Close();
  SnapshotArchiveReader reader;
  reader.Open(file.path);
  REQUIRE(reader.RecordCount() == 1);
}

TEST_CASE("queue fail policy reports overflow and leaves an incomplete recoverable prefix", "[archive][snapshot]") {
  TempFile file;
  SnapshotArchive writer;
  const auto metadata = Metadata(1000);
  writer.Open(file.path, metadata, {100, 1, QueueFullPolicy::Fail});
  auto record = Record(0, 1000);
  bool overflow = false;
  for (std::uint64_t i = 0; i < 10000; ++i) {
    record.stamp = Record(i, 0).stamp;
    try { writer.Append(record); }
    catch (const std::runtime_error&) { overflow = true; break; }
  }
  REQUIRE(overflow);
  REQUIRE_THROWS_AS(writer.Flush(), std::runtime_error);
  REQUIRE_THROWS_AS(writer.Close(), std::runtime_error);
  REQUIRE_FALSE(writer.IsOpen());
  SnapshotArchiveReader reader;
  REQUIRE_THROWS_AS(reader.Open(file.path), std::runtime_error);
  reader.Open(file.path, {}, ArchiveReadMode::RecoverPrefix);
  REQUIRE_FALSE(reader.Complete());
  // A new session must be usable after the failed worker has been joined.
  writer.Open(file.path, Metadata());
  writer.Append(Record(0));
  writer.Close();
  reader.Open(file.path);
  REQUIRE(reader.RecordCount() == 1);
}
