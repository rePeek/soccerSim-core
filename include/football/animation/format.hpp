#pragma once

#include <cstdint>
#include <ostream>
#include <string>

// Write-only byte encoding used by the catalog content hash. The historical
// SIMANIM v7 prefix and field layout remain solely to keep hashes stable;
// there is no binary asset reader or runtime cache format API.

constexpr char kSimAnimMagic[8] = {'S', 'I', 'M', 'A', 'N', 'I', 'M', '1'};
constexpr uint32_t kSimAnimVersion = 7;

void SimAnimWriteU32(std::ostream& os, uint32_t v);
void SimAnimWriteI32(std::ostream& os, int32_t v);
void SimAnimWriteF32(std::ostream& os, float v);
void SimAnimWriteString(std::ostream& os, const std::string& s);

void SimAnimWriteHeader(std::ostream& os, uint32_t clip_count);
