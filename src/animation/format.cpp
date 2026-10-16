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

// ANIM-2 A12.0: the primitive .simanim read/write bodies, moved verbatim out of
// format.hpp. Byte layout is unchanged; the offline baker and the runtime reader
// share only these declarations.

#include "football/animation/format.hpp"

#include <cstring>

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

uint32_t SimAnimReadU32(std::istream& is) {
  uint32_t v = 0;
  is.read(reinterpret_cast<char*>(&v), sizeof(v));
  return v;
}
int32_t SimAnimReadI32(std::istream& is) {
  int32_t v = 0;
  is.read(reinterpret_cast<char*>(&v), sizeof(v));
  return v;
}
float SimAnimReadF32(std::istream& is) {
  float v = 0.0f;
  is.read(reinterpret_cast<char*>(&v), sizeof(v));
  return v;
}
std::string SimAnimReadString(std::istream& is) {
  const uint32_t len = SimAnimReadU32(is);
  std::string s(len, '\0');
  if (len > 0) is.read(s.data(), static_cast<std::streamsize>(len));
  return s;
}

void SimAnimWriteHeader(std::ostream& os, uint32_t clip_count) {
  os.write(kSimAnimMagic, sizeof(kSimAnimMagic));
  SimAnimWriteU32(os, kSimAnimVersion);
  SimAnimWriteU32(os, clip_count);
}

bool SimAnimReadHeader(std::istream& is, uint32_t& clip_count) {
  char magic[8] = {};
  is.read(magic, sizeof(magic));
  if (std::memcmp(magic, kSimAnimMagic, sizeof(magic)) != 0) return false;
  const uint32_t version = SimAnimReadU32(is);
  if (version != kSimAnimVersion) return false;
  clip_count = SimAnimReadU32(is);
  return true;
}
