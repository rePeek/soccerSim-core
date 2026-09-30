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

// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "foundation/utils.hpp"
#include <filesystem>

#include "foundation/file.h"
#include "foundation/log.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

namespace blunted {


  // string functions

std::string stringchomp(std::string input, char chomp) {
  DO_VALIDATION;
  if (input.find_first_not_of(chomp) < input.length())
    return (input.substr(input.find_first_not_of(chomp)));
  return "";
}

  // tokenizer code from oopweb.com
void tokenize(const std::string &str, std::vector<std::string> &tokens,
              const std::string &delimiters) {
  DO_VALIDATION;
  // Skip delimiters at beginning.
  std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
  // Find first "non-delimiter".
  std::string::size_type pos = str.find_first_of(delimiters, lastPos);

  while (std::string::npos != pos || std::string::npos != lastPos) {
    DO_VALIDATION;
    // Found a token, add it to the vector.
    tokens.push_back(str.substr(lastPos, pos - lastPos));
    // Skip delimiters.  Note the "not_of"
    lastPos = str.find_first_not_of(delimiters, pos);
    // Find next "non-delimiter"
    pos = str.find_first_of(delimiters, lastPos);
  }
}

std::string file_to_string(std::string filename) {
  DO_VALIDATION;
  return GetFile(filename);
}

void file_to_vector(std::string filename,
                    std::vector<std::string> &destination) {
  DO_VALIDATION;
  std::string file = GetFile(filename);
  int last_pos = 0;
  for (int x = 0; x < file.length(); x++) {
    DO_VALIDATION;
    if (file[x] == '\n') {
      DO_VALIDATION;
      destination.push_back(file.substr(last_pos, x - last_pos));
      last_pos = x + 1;
    }
  }
  if (last_pos < file.length()) {
    DO_VALIDATION;
    destination.push_back(file.substr(last_pos, file.length() - last_pos));
  }
}

std::string get_file_name(const std::string &filename) {
  DO_VALIDATION;
  return std::filesystem::path(filename).filename().string();
}

std::string get_file_extension(const std::string &filename) {
  DO_VALIDATION;
  return filename.substr(filename.find_last_of('.') + 1);
}

std::string int_to_str(int i) {
  DO_VALIDATION;
  std::string i_str;
  char i_c[16];
  snprintf(i_c, 16, "%i", i);
  i_str.assign(i_c);
  return i_str;
}

std::string real_to_str(real r) {
  DO_VALIDATION;
  std::string r_str;
  char r_c[32];
  snprintf(r_c, 32, "%f", r);
  r_str.assign(r_c);
  return r_str;
}

std::string GetStringFromVector(const Vector3 &vec) {
  DO_VALIDATION;
  std::string tmp;
  tmp = "";
  char tmpC[1000];
  sprintf(tmpC, "%f, %f, %f", vec.coords[0], vec.coords[1], vec.coords[2]);
  tmp.assign(tmpC);
  return tmp;
}

Vector3 GetVectorFromString(const std::string &vecString) {
  DO_VALIDATION;
  if (vecString.compare("") == 0) {
    DO_VALIDATION;
    printf("vectorfromstring warning, no value\n");
    return Vector3(0.0f);
  }
  std::vector<std::string> tokenizedString;
  std::string delimiter = ",";
  tokenize(vecString, tokenizedString, delimiter);
  assert(tokenizedString.size() > 0);
  assert(tokenizedString.size() <= 3);
  Vector3 vector;
  vector.coords[0] = atof(tokenizedString.at(0).c_str());
  if (tokenizedString.size() > 1)
    vector.coords[1] = atof(tokenizedString.at(1).c_str());
  if (tokenizedString.size() > 2)
    vector.coords[2] = atof(tokenizedString.at(2).c_str());
  return vector;
}

Quaternion GetQuaternionFromString(const std::string &quatString) {
  DO_VALIDATION;
  std::vector<std::string> tokenizedString;
  std::string delimiter = ",";
  tokenize(quatString, tokenizedString, delimiter);
  assert(tokenizedString.size() == 4);
  radian angle;
  Vector3 vector;
  angle = atof(tokenizedString.at(0).c_str()) / 360.0 * 2.0 * pi;
  vector.coords[0] = atof(tokenizedString.at(1).c_str());
  vector.coords[1] = atof(tokenizedString.at(2).c_str());
  vector.coords[2] = atof(tokenizedString.at(3).c_str());
  Quaternion quaternion;
  quaternion.SetAngleAxis(angle, vector);
  return quaternion;
}

  }  // namespace blunted
