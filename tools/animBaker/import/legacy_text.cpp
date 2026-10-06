#include "import/legacy_text.hpp"

#include <cstdio>

namespace blunted {

void tokenize(const std::string& str, std::vector<std::string>& tokens,
              const std::string& delimiters) {
  std::string::size_type last_pos = str.find_first_not_of(delimiters);
  std::string::size_type pos = str.find_first_of(delimiters, last_pos);
  while (pos != std::string::npos || last_pos != std::string::npos) {
    tokens.push_back(str.substr(last_pos, pos - last_pos));
    last_pos = str.find_first_not_of(delimiters, pos);
    pos = str.find_first_of(delimiters, last_pos);
  }
}

std::string int_to_str(int value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%i", value);
  return buffer;
}

std::string real_to_str(real value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%f", value);
  return buffer;
}

}  // namespace blunted
