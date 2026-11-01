// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
