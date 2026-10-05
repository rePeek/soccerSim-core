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

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and
// should generally not be used for anything important. i do not offer support,
// so don't ask. to be used for inspiration :)

#include "sim/gamedefines.hpp"

#include <cmath>

#include "support/diagnostics/log.hpp"
#include "support/text/string_utils.hpp"
#include "support/io/file.hpp"



e_PlayerRole GetRoleFromString(const std::string &roleString) {
  if (roleString.compare("GK") == 0) return e_PlayerRole_GK;
  if (roleString.compare("CB") == 0) return e_PlayerRole_CB;
  if (roleString.compare("LB") == 0) return e_PlayerRole_LB;
  if (roleString.compare("RB") == 0) return e_PlayerRole_RB;
  if (roleString.compare("DM") == 0) return e_PlayerRole_DM;
  if (roleString.compare("CM") == 0) return e_PlayerRole_CM;
  if (roleString.compare("LM") == 0) return e_PlayerRole_LM;
  if (roleString.compare("RM") == 0) return e_PlayerRole_RM;
  if (roleString.compare("AM") == 0) return e_PlayerRole_AM;
  if (roleString.compare("CF") == 0) return e_PlayerRole_CF;
  return e_PlayerRole_CM;  // default
}
