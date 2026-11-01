// Write-only byte primitives for stable catalog content hashing.
// Preserve the historical encoding; no binary resource reads remain.

#include "football/animation/format.hpp"

void SimAnimWriteU32(std::ostream& os, uint32_t v) {
  os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void SimAnimWriteI32(std::ostream& os, int32_t v) {
  os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void SimAnimWriteF32(std::ostream& os, float v) {
  os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void SimAnimWriteString(std::ostream& os, const std::string& s) {
  SimAnimWriteU32(os, static_cast<uint32_t>(s.size()));
  os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

void SimAnimWriteHeader(std::ostream& os, uint32_t clip_count) {
  os.write(kSimAnimMagic, sizeof(kSimAnimMagic));
  SimAnimWriteU32(os, kSimAnimVersion);
  SimAnimWriteU32(os, clip_count);
}
