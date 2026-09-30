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

#ifndef _HPP_BASE_UTILS
#define _HPP_BASE_UTILS

#include "foundation/defines.hpp"
#include "foundation/math/bluntmath.hpp"


namespace blunted {

  class Vector3;
  class Quaternion;


  // string functions
  std::string stringchomp(std::string input, char chomp);
  void tokenize(const std::string& str, std::vector<std::string> &tokens, const std::string &delimiters = " ");

  std::string file_to_string(std::string filename);
  void file_to_vector(std::string filename, std::vector<std::string> &destination);

  std::string get_file_name(const std::string &filename);
  std::string get_file_extension(const std::string &filename);

  std::string int_to_str(int i);
  std::string real_to_str(real r);

  std::string GetStringFromVector(const Vector3 &vec);
  Vector3 GetVectorFromString(const std::string &vecString);
  Quaternion GetQuaternionFromString(const std::string &quatString);

  // assumes 10ms input timestep
  template <typename T> class ValueHistory {

    public:
      ValueHistory(unsigned int maxTime_ms = 10000) : maxTime_ms(maxTime_ms) { }
      virtual ~ValueHistory() { }

      void Insert(const T &value) {
        values.push_back(value);
        if (values.size() > maxTime_ms / 10) values.pop_front();
      }

      T GetAverage(unsigned int time_ms) const {
        T total = 0;
        unsigned int count = 0;
        if (!values.empty()) {
          typename std::list<T>::const_iterator iter = values.end();
          iter--;
          while (count <= time_ms / 10) {
            total += (*iter);
            count++;
            if (iter == values.begin()) break; else iter--;
          }
        }
        if (count > 0) total /= (float)count;
        return total;
      }

      void Clear() {
        values.clear();
      }
      unsigned int GetMaxTime_ms() const { return maxTime_ms; }
      const std::list<T>& GetValues() const { return values; }
      void Restore(unsigned int restored_max_time_ms,
                   const std::list<T>& restored_values) {
        maxTime_ms = restored_max_time_ms;
        values = restored_values;
      }

    protected:
      unsigned int maxTime_ms = 0;
      std::list<T> values;

  };



}

#endif
