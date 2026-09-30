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

#include "support/io/xml_loader.hpp"

#include <algorithm>
#include <cctype>

#include "support/diagnostics/log.hpp"
#include "support/io/file.hpp"

namespace blunted {

XMLLoader::XMLLoader() {}

XMLLoader::~XMLLoader() {}

XMLTree XMLLoader::LoadFile(const std::string& filename) {
  const std::string source = file_to_string(filename);

  XMLTree tree;
  BuildTree(tree, source);

  return tree;
}

XMLTree XMLLoader::Load(const std::string& file) {
  XMLTree tree;
  BuildTree(tree, file);

  return tree;
}

void XMLLoader::BuildTree(XMLTree& tree, const std::string& source) {

  size_t index_end = 0;
  size_t index = source.find('<', 0);

  if (index == std::string::npos) {
    // No tags: this is a value.
    tree.value = source;
    tree.value.erase(
        std::remove_if(tree.value.begin(), tree.value.end(),
                       [](unsigned char c) { return std::isspace(c); }),
        tree.value.end());
    return;
  }

  while (index != std::string::npos) {
    index_end = source.find('>', index);
    std::string tag = source.substr(index + 1, index_end - index - 1);
    index = index_end;

    int recurse_counter = 1;
    size_t index_nexttag_open = 0;
    size_t index_nexttag_close = 0;
    while (recurse_counter != 0) {
      index_nexttag_open = source.find("<" + tag + ">", index_end + 1);
      index_nexttag_close = source.find("</" + tag + ">", index_end + 1);
      if (index_nexttag_open > index_nexttag_close ||
          index_nexttag_open == std::string::npos) {
        recurse_counter--;
        index_end = index_nexttag_close;
      } else {
        recurse_counter++;
        index_end = index_nexttag_open;
      }
      if (index_end == std::string::npos) {
        Log(e_FatalError, "XMLLoader", "BuildTree",
            "No closing tag found for <" + tag + ">");
      }
    }

    std::string data = source.substr(index + 1, index_end - index - 1);

    XMLTree child;
    BuildTree(child, data);
    tree.children.insert(std::make_pair(tag, child));

    index = source.find('>', index_end);
    index = source.find('<', index);
  }
}

}  // namespace blunted
