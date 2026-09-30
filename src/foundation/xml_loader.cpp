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

#include "foundation/xml_loader.hpp"

#include "foundation/log.hpp"
#include "foundation/utils.hpp"

namespace blunted {

XMLLoader::XMLLoader() { DO_VALIDATION; }

XMLLoader::~XMLLoader() { DO_VALIDATION; }

XMLTree XMLLoader::LoadFile(const std::string& filename) {
  DO_VALIDATION;
  std::string source = file_to_string(filename);

  XMLTree tree;
  BuildTree(tree, source);

  return tree;
}

XMLTree XMLLoader::Load(const std::string& file) {
  DO_VALIDATION;
  XMLTree tree;
  BuildTree(tree, file);

  return tree;
}

void XMLLoader::BuildTree(XMLTree& tree, const std::string& source) {
  DO_VALIDATION;

  size_t index_end = 0;
  size_t index = source.find('<', 0);

  if (index == std::string::npos) {
    DO_VALIDATION;
    // No tags: this is a value.
    tree.value = source;
    tree.value.erase(remove_if(tree.value.begin(), tree.value.end(), isspace),
                     tree.value.end());
    return;
  }

  while (index != std::string::npos) {
    DO_VALIDATION;
    index_end = source.find('>', index);
    std::string tag = source.substr(index + 1, index_end - index - 1);
    index = index_end;

    int recurse_counter = 1;
    size_t index_nexttag_open = 0;
    size_t index_nexttag_close = 0;
    while (recurse_counter != 0) {
      DO_VALIDATION;
      index_nexttag_open = source.find("<" + tag + ">", index_end + 1);
      index_nexttag_close = source.find("</" + tag + ">", index_end + 1);
      if (index_nexttag_open > index_nexttag_close ||
          index_nexttag_open == std::string::npos) {
        DO_VALIDATION;
        recurse_counter--;
        index_end = index_nexttag_close;
      } else {
        recurse_counter++;
        index_end = index_nexttag_open;
      }
      if (index_end == std::string::npos) {
        DO_VALIDATION;
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
