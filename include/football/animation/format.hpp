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

#ifndef _HPP_SIMANIM_FORMAT
#define _HPP_SIMANIM_FORMAT

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

// Baked animation artifact (.simanim) schema, shared between the offline
// baker (writer) and the runtime library (reader). Keep this dependency-free:
// it only knows byte layout and primitive (de)serialization.
//
// ANIM-2 A12.0: the primitive bodies live in src/animation/format.cpp so this
// header only carries the schema constants and declarations.

constexpr char kSimAnimMagic[8] = {'S', 'I', 'M', 'A', 'N', 'I', 'M', '1'};
constexpr uint32_t kSimAnimVersion = 7;

void SimAnimWriteU32(std::ostream& os, uint32_t v);
void SimAnimWriteI32(std::ostream& os, int32_t v);
void SimAnimWriteF32(std::ostream& os, float v);
void SimAnimWriteString(std::ostream& os, const std::string& s);

uint32_t SimAnimReadU32(std::istream& is);
int32_t SimAnimReadI32(std::istream& is);
float SimAnimReadF32(std::istream& is);
std::string SimAnimReadString(std::istream& is);

void SimAnimWriteHeader(std::ostream& os, uint32_t clip_count);
bool SimAnimReadHeader(std::istream& is, uint32_t& clip_count);

#endif
