#include "support/text/string_utils.hpp"

#include <cstdio>

namespace blunted {

std::string stringchomp(std::string input, char chomp) {
  const std::string::size_type first = input.find_first_not_of(chomp);
  return first == std::string::npos ? "" : input.substr(first);
}

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
