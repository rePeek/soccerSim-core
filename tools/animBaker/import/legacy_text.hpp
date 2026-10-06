#ifndef FOOTBALL_ANIMBAKER_IMPORT_LEGACY_TEXT_HPP
#define FOOTBALL_ANIMBAKER_IMPORT_LEGACY_TEXT_HPP

#include <string>
#include <vector>

#include "foundation/math/scalar.hpp"

namespace blunted {

// Only the tokenization/decimal formatting required by the legacy importer.
void tokenize(const std::string& str, std::vector<std::string>& tokens,
              const std::string& delimiters = " ");
std::string int_to_str(int value);
std::string real_to_str(real value);

}  // namespace blunted

#endif  // FOOTBALL_ANIMBAKER_IMPORT_LEGACY_TEXT_HPP
