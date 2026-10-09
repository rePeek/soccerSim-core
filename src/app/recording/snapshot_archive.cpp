#include "app/recording/snapshot_archive.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace football::app::recording {
namespace {
using namespace sim::observation;
using Bytes = std::vector<std::uint8_t>;
constexpr std::array<char, 8> kMagic{'S', 'O', 'C', 'C', 'S', 'N', 'P', '1'};
constexpr std::uint32_t kHeader = 0x52444853; // SHDR, little endian
constexpr std::uint32_t kChunk = 0x4b4e4843;  // CHNK
constexpr std::uint32_t kIndex = 0x58444e49;  // INDX
constexpr std::uint32_t kEnd = 0x21444e45;    // END!
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaxBlockBytes = 64 * 1024 * 1024;
constexpr std::size_t kMaxPlayers = 100000;
constexpr std::uint64_t kFooterBytes = 32;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

[[noreturn]] void Bad(const char* message) {
  throw std::runtime_error(std::string("snapshot archive: ") + message);
}

void Put(Bytes& bytes, std::uint64_t value, unsigned width) {
  for (unsigned i = 0; i < width; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void PutFloat(Bytes& bytes, float value) { Put(bytes, std::bit_cast<std::uint32_t>(value), 4); }
void PutVector(Bytes& bytes, const blunted::Vector3& v) {
  for (float coord : v.coords) PutFloat(bytes, coord);
}

class Cursor {
 public:
  explicit Cursor(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
  std::uint64_t Get(unsigned width) {
    if (width > bytes_.size() - position_) Bad("truncated fields");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(bytes_[position_++]) << (8 * i);
    return value;
  }
  float Float() { return std::bit_cast<float>(static_cast<std::uint32_t>(Get(4))); }
  blunted::Vector3 Vector() {
    const float x = Float(), y = Float(), z = Float();
    return {x, y, z};
  }
  bool Bool() {
    const auto value = Get(1);
    if (value > 1) Bad("invalid boolean");
    return value != 0;
  }
  void End() const { if (position_ != bytes_.size()) Bad("unexpected trailing fields"); }
 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_ = 0;
};

std::uint32_t Crc32(std::span<const std::uint8_t> bytes) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> result{};
    for (std::uint32_t i = 0; i < result.size(); ++i) {
      auto crc = i;
      for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
      result[i] = crc;
    }
    return result;
  }();
  std::uint32_t crc = 0xffffffffU;
  for (auto byte : bytes) crc = (crc >> 8) ^ table[(crc ^ byte) & 0xff];
  return ~crc;
}

void Write(std::ostream& stream, std::span<const std::uint8_t> bytes) {
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream) Bad("disk write failed");
}
Bytes Read(std::istream& stream, std::size_t count) {
  Bytes bytes(count);
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
  if (!stream) Bad("truncated file or disk read failed");
  return bytes;
}
std::uint64_t Offset(std::ostream& stream) {
  const auto position = stream.tellp();
  if (position < 0) Bad("cannot obtain file offset");
  return static_cast<std::uint64_t>(position);
}
void Seek(std::istream& stream, std::uint64_t offset) {
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) Bad("offset overflow");
  stream.clear();
  stream.seekg(static_cast<std::streamoff>(offset));
  if (!stream) Bad("seek failed");
}
void FlushStream(std::ostream& stream) {
  stream.flush();
  if (!stream) Bad("disk flush failed");
}
void WriteBlock(std::ostream& stream, std::uint32_t tag, const Bytes& payload) {
  if (payload.size() > kMaxBlockBytes) Bad("block exceeds format safety limit");
  Bytes header;
  Put(header, tag, 4);
  Put(header, payload.size(), 8);
  Put(header, Crc32(payload), 4);
  Write(stream, header);
  Write(stream, payload);
}
struct Block {
  std::uint32_t tag;
  Bytes payload;
  std::uint64_t size;
};
Block ReadBlock(std::istream& stream, std::uint64_t offset, std::uint64_t file_size) {
  if (offset > file_size || file_size - offset < 16) Bad("truncated block header");
  Seek(stream, offset);
  const auto header = Read(stream, 16);
  Cursor cursor(header);
  const auto tag = static_cast<std::uint32_t>(cursor.Get(4));
  const auto size = cursor.Get(8);
  const auto checksum = static_cast<std::uint32_t>(cursor.Get(4));
  if (size > kMaxBlockBytes || size > file_size - offset - 16) Bad("invalid block size");
  auto payload = Read(stream, static_cast<std::size_t>(size));
  if (Crc32(payload) != checksum) Bad("block checksum mismatch");
  return {tag, std::move(payload), size + 16};
}

void ValidateMetadata(const SnapshotMetadata& metadata) {
  if (metadata.player_ids.size() > kMaxPlayers) throw std::invalid_argument("too many snapshot player slots");
  std::set<model::PlayerId> ids;
  for (auto id : metadata.player_ids) {
    if (id == model::kInvalidPlayerId || !ids.insert(id).second)
      throw std::invalid_argument("invalid or duplicate snapshot PlayerId");
  }
}
Bytes EncodeMetadata(const SnapshotMetadata& metadata) {
  Bytes bytes;
  Put(bytes, kVersion, 4);
  Put(bytes, sim::kTicksPerSecond, 4);
  Put(bytes, 1, 4); // coordinate frame: fixed WorldState home-pitch frame
  Put(bytes, metadata.animation_library_hash, 8);
  const auto& p = metadata.pitch;
  for (float value : {p.length(), p.width(), p.quadratic_resistance(), p.ground_deceleration(),
       p.grass_height(), p.line_half_width(), p.goal_half_width(), p.goal_height(),
       p.goal_depth(), p.penalty_area_depth(), p.penalty_mark_distance()}) PutFloat(bytes, value);
  Put(bytes, metadata.player_ids.size(), 4);
  for (auto id : metadata.player_ids) Put(bytes, id, 4);
  return bytes;
}
SnapshotMetadata DecodeMetadata(const Bytes& bytes) {
  Cursor cursor(bytes);
  if (cursor.Get(4) != kVersion) Bad("unsupported format version");
  if (cursor.Get(4) != sim::kTicksPerSecond || cursor.Get(4) != 1) Bad("unsupported rate or coordinate frame");
  SnapshotMetadata metadata;
  metadata.animation_library_hash = cursor.Get(8);
  const float length = cursor.Float(), width = cursor.Float(), resistance = cursor.Float();
  const float deceleration = cursor.Float(), grass = cursor.Float();
  metadata.pitch = model::Pitch(length, width, resistance, deceleration, grass);
  const auto& p = metadata.pitch;
  for (float value : {p.line_half_width(), p.goal_half_width(), p.goal_height(),
       p.goal_depth(), p.penalty_area_depth(), p.penalty_mark_distance()}) {
    if (cursor.Float() != value) Bad("unsupported pitch geometry");
  }
  const auto count = cursor.Get(4);
  if (count > kMaxPlayers || bytes.size() != 68 + count * 4) Bad("invalid player slot table size");
  metadata.player_ids.resize(static_cast<std::size_t>(count));
  for (auto& id : metadata.player_ids) id = static_cast<model::PlayerId>(cursor.Get(4));
  cursor.End();
  ValidateMetadata(metadata);
  return metadata;
}

std::uint64_t RecordBytes(std::size_t players) { return 60 + std::uint64_t(players) * 58; }
bool Ordered(const SnapshotStamp& previous, const SnapshotStamp& next) {
  return next.step_index > previous.step_index && next.timeline_tick >= previous.timeline_tick &&
         next.generation >= previous.generation;
}
void EncodeRecord(Bytes& bytes, const SnapshotRecord& record) {
  Put(bytes, record.stamp.step_index, 8);
  Put(bytes, record.stamp.timeline_tick.value, 8);
  Put(bytes, record.stamp.generation, 8);
  PutVector(bytes, record.snapshot.ball.position);
  PutVector(bytes, record.snapshot.ball.velocity);
  PutVector(bytes, record.snapshot.ball.angular_velocity);
  for (const auto& player : record.snapshot.players) {
    PutVector(bytes, player.position);
    PutVector(bytes, player.velocity);
    PutVector(bytes, player.facing);
    PutVector(bytes, player.body_facing);
    Put(bytes, std::bit_cast<std::uint32_t>(player.animation_id), 4);
    Put(bytes, player.frame, 4);
    Put(bytes, player.active, 1);
    Put(bytes, player.has_possession, 1);
  }
}
SnapshotRecord DecodeRecord(Cursor& cursor, std::size_t players) {
  SnapshotRecord record;
  record.stamp.step_index = cursor.Get(8);
  record.stamp.timeline_tick.value = cursor.Get(8);
  record.stamp.generation = cursor.Get(8);
  record.snapshot.ball = {cursor.Vector(), cursor.Vector(), cursor.Vector()};
  record.snapshot.players.resize(players);
  for (auto& player : record.snapshot.players) {
    player.position = cursor.Vector();
    player.velocity = cursor.Vector();
    player.facing = cursor.Vector();
    player.body_facing = cursor.Vector();
    player.animation_id = std::bit_cast<AnimationId>(static_cast<std::uint32_t>(cursor.Get(4)));
    player.frame = static_cast<std::uint32_t>(cursor.Get(4));
    player.active = cursor.Bool();
    player.has_possession = cursor.Bool();
  }
  return record;
}

struct IndexEntry {
  std::uint64_t first, last, offset, bytes;
  std::uint32_t count;
};
std::vector<SnapshotRecord> DecodeChunk(const Block& block, std::size_t players,
                                      const IndexEntry* entry = nullptr) {
  if (block.tag != kChunk) Bad("expected chunk");
  Cursor cursor(block.payload);
  const auto count = cursor.Get(4), first = cursor.Get(8), last = cursor.Get(8);
  if (count == 0 || block.payload.size() != 20 + count * RecordBytes(players)) Bad("invalid chunk frame count");
  if (entry && (entry->count != count || entry->first != first || entry->last != last || entry->bytes != block.size))
    Bad("chunk does not match index");
  std::vector<SnapshotRecord> records;
  records.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    auto record = DecodeRecord(cursor, players);
    if (!records.empty() && !Ordered(records.back().stamp, record.stamp)) Bad("nonmonotonic chunk stamps");
    records.push_back(std::move(record));
  }
  cursor.End();
  if (records.front().stamp.step_index != first || records.back().stamp.step_index != last) Bad("chunk range mismatch");
  return records;
}
}  // namespace

struct SnapshotArchive::Impl {
  std::ofstream stream;
  SnapshotArchiveOptions options;
  std::vector<SnapshotRecord> queue;
  std::size_t read_index = 0, write_index = 0, size = 0;
  std::optional<SnapshotStamp> latest;
  std::mutex mutex;
  std::condition_variable available, space, flushed;
  std::thread worker;
  bool closing = false, closed = false;
  std::uint64_t flush_requested = 0, flush_completed = 0;
  std::exception_ptr error;

  // Worker-owned disk state, never queried by the producer.
  std::vector<IndexEntry> index;
  Bytes chunk;
  std::uint32_t chunk_count = 0;
  std::uint64_t chunk_first = 0, chunk_last = 0, record_count = 0;

  Impl(const std::filesystem::path& path, const SnapshotMetadata& metadata,
       SnapshotArchiveOptions opts) : options(opts) {
    ValidateMetadata(metadata);
    if (options.chunk_frames == 0 || options.queue_capacity == 0 ||
        options.chunk_frames > (kMaxBlockBytes - 20) / RecordBytes(metadata.player_ids.size()))
      throw std::invalid_argument("invalid snapshot chunk/queue capacity");
    queue.resize(options.queue_capacity);
    for (auto& slot : queue) slot.snapshot.players.resize(metadata.player_ids.size());
    chunk.reserve(static_cast<std::size_t>(20 + options.chunk_frames * RecordBytes(metadata.player_ids.size())));
    stream.open(path, std::ios::binary | std::ios::trunc);
    if (!stream) Bad("cannot open output file");
    stream.write(kMagic.data(), kMagic.size());
    WriteBlock(stream, kHeader, EncodeMetadata(metadata));
    FlushStream(stream);
  }

  void WriteChunk() {
    if (chunk_count == 0) return;
    Bytes payload;
    payload.reserve(20 + chunk.size());
    Put(payload, chunk_count, 4);
    Put(payload, chunk_first, 8);
    Put(payload, chunk_last, 8);
    payload.insert(payload.end(), chunk.begin(), chunk.end());
    const auto offset = Offset(stream);
    WriteBlock(stream, kChunk, payload);
    index.push_back({chunk_first, chunk_last, offset, payload.size() + 16, chunk_count});
    record_count += chunk_count;
    chunk.clear();
    chunk_count = 0;
  }

  void Finish() {
    WriteChunk();
    const auto index_offset = Offset(stream);
    Bytes payload;
    Put(payload, index.size(), 8);
    for (const auto& entry : index) {
      Put(payload, entry.first, 8); Put(payload, entry.last, 8);
      Put(payload, entry.offset, 8); Put(payload, entry.bytes, 8); Put(payload, entry.count, 4);
    }
    WriteBlock(stream, kIndex, payload);
    payload.clear();
    Put(payload, index_offset, 8);
    Put(payload, record_count, 8);
    WriteBlock(stream, kEnd, payload);
    FlushStream(stream);
    stream.close();
    if (!stream) Bad("disk close failed");
  }

  void Run() noexcept {
    try {
      SnapshotRecord scratch;
      scratch.snapshot.players.resize(queue.front().snapshot.players.size());
      for (;;) {
        std::unique_lock lock(mutex);
        available.wait(lock, [&] { return error || size != 0 || closing || flush_requested != flush_completed; });
        if (error) return;
        if (size != 0) {
          scratch = queue[read_index];
          read_index = (read_index + 1) % queue.size();
          --size;
          space.notify_one();
          lock.unlock();
          if (chunk_count == 0) chunk_first = scratch.stamp.step_index;
          chunk_last = scratch.stamp.step_index;
          EncodeRecord(chunk, scratch);
          if (++chunk_count == options.chunk_frames) WriteChunk();
          continue;
        }
        if (closing) {
          lock.unlock();
          Finish();
          return;
        }
        const auto requested = flush_requested;
        lock.unlock();
        WriteChunk();
        FlushStream(stream);
        lock.lock();
        flush_completed = requested;
        flushed.notify_all();
      }
    } catch (...) {
      std::lock_guard lock(mutex);
      if (!error) error = std::current_exception();
      space.notify_all();
      flushed.notify_all();
    }
  }
};

SnapshotArchive::SnapshotArchive() = default;
SnapshotArchive::~SnapshotArchive() { try { Close(); } catch (...) {} }

void SnapshotArchive::Open(const std::filesystem::path& path, const SnapshotMetadata& metadata,
                           SnapshotArchiveOptions options) {
  if (IsOpen()) throw std::logic_error("snapshot archive is already open");
  auto impl = std::make_unique<Impl>(path, metadata, options);
  impl->worker = std::thread([p = impl.get()] { p->Run(); });
  impl_ = std::move(impl);
}

bool SnapshotArchive::IsOpen() const noexcept { return impl_ && !impl_->closed; }

void SnapshotArchive::Append(const SnapshotRecord& record) {
  if (!IsOpen()) throw std::logic_error("snapshot archive is not open");
  auto& impl = *impl_;
  std::unique_lock lock(impl.mutex);
  if (impl.error) std::rethrow_exception(impl.error);
  if (record.snapshot.players.size() != impl.queue.front().snapshot.players.size())
    throw std::invalid_argument("snapshot archive player count mismatch");
  if (impl.latest && !Ordered(*impl.latest, record.stamp))
    throw std::logic_error("snapshot archive stamps must increase");
  if (impl.size == impl.queue.size() && impl.options.queue_full_policy == QueueFullPolicy::Fail) {
    impl.error = std::make_exception_ptr(std::runtime_error("snapshot archive queue overflow; session incomplete"));
    impl.available.notify_one();
    std::rethrow_exception(impl.error);
  }
  impl.space.wait(lock, [&] { return impl.error || impl.size < impl.queue.size(); });
  if (impl.error) std::rethrow_exception(impl.error);
  impl.queue[impl.write_index] = record;
  impl.write_index = (impl.write_index + 1) % impl.queue.size();
  ++impl.size;
  impl.latest = record.stamp;
  impl.available.notify_one();
}

void SnapshotArchive::Flush() {
  if (!IsOpen()) throw std::logic_error("snapshot archive is not open");
  auto& impl = *impl_;
  std::unique_lock lock(impl.mutex);
  if (impl.error) std::rethrow_exception(impl.error);
  const auto request = ++impl.flush_requested;
  impl.available.notify_one();
  impl.flushed.wait(lock, [&] { return impl.error || impl.flush_completed >= request; });
  if (impl.error) std::rethrow_exception(impl.error);
}

void SnapshotArchive::Close() {
  if (!impl_) return;
  auto& impl = *impl_;
  if (!impl.closed) {
    {
      std::lock_guard lock(impl.mutex);
      impl.closing = true;
      impl.available.notify_one();
    }
    if (impl.worker.joinable()) impl.worker.join();
    impl.closed = true;
    // Flush the successfully written prefix on failure, without an index/footer.
    if (impl.stream.is_open()) impl.stream.close();
  }
  if (impl.error) std::rethrow_exception(impl.error);
}

struct SnapshotArchiveReader::Impl {
  std::ifstream stream;
  SnapshotMetadata metadata;
  std::vector<IndexEntry> index;
  std::uint64_t file_size = 0, record_count = 0, data_offset = 0;
  bool complete = false;

  explicit Impl(const std::filesystem::path& path) : stream(path, std::ios::binary) {
    if (!stream) Bad("cannot open input file");
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0) Bad("cannot obtain file size");
    file_size = static_cast<std::uint64_t>(size);
    Seek(stream, 0);
    std::array<char, 8> magic{};
    stream.read(magic.data(), magic.size());
    if (!stream || magic != kMagic) Bad("bad file magic");
    const auto header = ReadBlock(stream, 8, file_size);
    if (header.tag != kHeader) Bad("missing header");
    metadata = DecodeMetadata(header.payload);
    data_offset = 8 + header.size;
  }

  void LoadIndex() {
    if (file_size < data_offset + kFooterBytes) Bad("missing completion footer");
    const auto footer_offset = file_size - kFooterBytes;
    const auto footer = ReadBlock(stream, footer_offset, file_size);
    if (footer.tag != kEnd || footer.size != kFooterBytes) Bad("missing completion footer");
    Cursor end(footer.payload);
    const auto index_offset = end.Get(8), total = end.Get(8);
    if (index_offset < data_offset || index_offset >= footer_offset) Bad("invalid index offset");
    const auto block = ReadBlock(stream, index_offset, file_size);
    if (block.tag != kIndex || block.size != footer_offset - index_offset) Bad("invalid chunk index");
    Cursor cursor(block.payload);
    const auto count = cursor.Get(8);
    if (count > (kMaxBlockBytes - 8) / 36 || block.payload.size() != 8 + count * 36)
      Bad("invalid index entry count");
    std::uint64_t offset = data_offset, records = 0;
    std::vector<IndexEntry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
      IndexEntry entry;
      entry.first = cursor.Get(8); entry.last = cursor.Get(8);
      entry.offset = cursor.Get(8); entry.bytes = cursor.Get(8);
      entry.count = static_cast<std::uint32_t>(cursor.Get(4));
      if (entry.first > entry.last || (!entries.empty() && entry.first <= entries.back().last) ||
          entry.count == 0 || entry.bytes != 36 + std::uint64_t(entry.count) * RecordBytes(metadata.player_ids.size()) ||
          entry.offset != offset || offset > index_offset || entry.bytes > index_offset - offset)
        Bad("invalid index entry");
      offset += entry.bytes;
      records += entry.count;
      entries.push_back(entry);
    }
    if (offset != index_offset || records != total) Bad("index total mismatch");
    cursor.End();
    index = std::move(entries);
    record_count = total;
    complete = true;
  }

  void Recover() {
    index.clear();
    record_count = 0;
    std::uint64_t offset = data_offset;
    std::optional<SnapshotStamp> previous;
    while (offset < file_size) {
      try {
        const auto block = ReadBlock(stream, offset, file_size);
        if (block.tag != kChunk) break;
        const auto records = DecodeChunk(block, metadata.player_ids.size());
        if (previous && !Ordered(*previous, records.front().stamp)) break;
        const auto count = static_cast<std::uint32_t>(records.size());
        index.push_back({records.front().stamp.step_index, records.back().stamp.step_index,
                         offset, block.size, count});
        previous = records.back().stamp;
        record_count += count;
        offset += block.size;
      } catch (const std::runtime_error&) {
        break;
      }
    }
    complete = false;
  }
};

SnapshotArchiveReader::SnapshotArchiveReader() = default;
SnapshotArchiveReader::~SnapshotArchiveReader() = default;
void SnapshotArchiveReader::Open(const std::filesystem::path& path,
                                 std::optional<std::uint64_t> expected_animation_hash,
                                 ArchiveReadMode mode) {
  auto impl = std::make_unique<Impl>(path);
  if (expected_animation_hash && impl->metadata.animation_library_hash != *expected_animation_hash)
    Bad("animation library hash mismatch");
  try {
    impl->LoadIndex();
    if (mode == ArchiveReadMode::RecoverPrefix) {
      // Explicit recovery also validates data, not just a surviving footer.
      std::optional<SnapshotStamp> previous;
      for (const auto& entry : impl->index) {
        const auto block = ReadBlock(impl->stream, entry.offset, impl->file_size);
        const auto records = DecodeChunk(block, impl->metadata.player_ids.size(), &entry);
        if (previous && !Ordered(*previous, records.front().stamp))
          Bad("nonmonotonic inter-chunk stamps");
        previous = records.back().stamp;
      }
    }
  } catch (const std::runtime_error&) {
    if (mode != ArchiveReadMode::RecoverPrefix) throw;
    impl->Recover();
  }
  impl_ = std::move(impl);
}
const SnapshotMetadata& SnapshotArchiveReader::Metadata() const {
  if (!impl_) throw std::logic_error("snapshot archive reader is not open");
  return impl_->metadata;
}
bool SnapshotArchiveReader::Complete() const {
  if (!impl_) throw std::logic_error("snapshot archive reader is not open");
  return impl_->complete;
}
std::uint64_t SnapshotArchiveReader::RecordCount() const {
  if (!impl_) throw std::logic_error("snapshot archive reader is not open");
  return impl_->record_count;
}
std::vector<SnapshotRecord> SnapshotArchiveReader::ReadRange(std::uint64_t first_step,
                                                            std::uint64_t last_step) {
  if (!impl_) throw std::logic_error("snapshot archive reader is not open");
  if (last_step < first_step) throw std::invalid_argument("snapshot archive reversed range");
  auto& impl = *impl_;
  std::vector<SnapshotRecord> output;
  auto entry = std::lower_bound(impl.index.begin(), impl.index.end(), first_step,
      [](const IndexEntry& e, std::uint64_t step) { return e.last < step; });
  std::optional<SnapshotStamp> previous;
  for (; entry != impl.index.end() && entry->first <= last_step; ++entry) {
    const auto block = ReadBlock(impl.stream, entry->offset, impl.file_size);
    auto records = DecodeChunk(block, impl.metadata.player_ids.size(), &*entry);
    if (previous && !Ordered(*previous, records.front().stamp)) Bad("nonmonotonic inter-chunk stamps");
    previous = records.back().stamp;
    for (auto& record : records) {
      if (record.stamp.step_index >= first_step && record.stamp.step_index <= last_step)
        output.push_back(std::move(record));
    }
  }
  return output;
}

}  // namespace football::app::recording
