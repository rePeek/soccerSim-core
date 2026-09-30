#ifndef FOOTBALL_SUPPORT_TEXT_STRING_UTILS_HPP
#define FOOTBALL_SUPPORT_TEXT_STRING_UTILS_HPP

#include "support/text/string_utils.hpp"
#include <string>
#include <vector>

#include "foundation/math/scalar.hpp"

namespace blunted {

std::string stringchomp(std::string input, char chomp);
void tokenize(const std::string& str, std::vector<std::string>& tokens,
              const std::string& delimiters = " ");
std::string int_to_str(int value);
std::string real_to_str(real value);

}  // namespace blunted

#endif  // FOOTBALL_SUPPORT_TEXT_STRING_UTILS_HPP
