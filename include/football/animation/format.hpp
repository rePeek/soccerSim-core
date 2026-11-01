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

#endif
