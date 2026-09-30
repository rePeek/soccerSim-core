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

#include "support/io/file.hpp"

#include <fstream>

#include "support/diagnostics/log.hpp"

std::string GetFile(const std::string &fileName) {
  std::ifstream file;
  file.open(fileName.c_str(), std::ios::in);
  std::string str((std::istreambuf_iterator<char>(file)),
      std::istreambuf_iterator<char>());
  file.close();
  return str;
}

void GetFilesRec(fs::path path, const std::string &extension,
                 std::vector<std::string> &files) {
  if (!fs::exists(path) || !fs::is_directory(path)) {
    blunted::Log(blunted::e_Error, "DirectoryParser", "Parse",
        "Could not open directory " + path.string() + " for reading");
  }
  fs::directory_iterator dirIter(path);
  fs::directory_iterator endIter;
  while (dirIter != endIter) {
    if (is_directory(dirIter->status())) {
      fs::path thePath(path);
      thePath /= dirIter->path().filename();
      GetFilesRec(thePath, extension, files);
    } else {
      fs::path thePath(path);
      thePath /= dirIter->path().filename();

      if (thePath.extension() == "." + extension) {
        files.push_back(thePath.string());
      }
    }

    dirIter++;
  }
}

void GetFiles(std::string path, const std::string &extension,
              std::vector<std::string> &files) {
  GetFilesRec(path, extension, files);
}

std::string file_to_string(const std::string& filename) {
  return GetFile(filename);
}

void file_to_vector(const std::string& filename,
                    std::vector<std::string>& destination) {
  const std::string file = GetFile(filename);
  std::string::size_type last_pos = 0;
  std::string::size_type pos = file.find('\n');
  while (pos != std::string::npos) {
    destination.push_back(file.substr(last_pos, pos - last_pos));
    last_pos = pos + 1;
    pos = file.find('\n', last_pos);
  }
  if (last_pos < file.size()) destination.push_back(file.substr(last_pos));
}

std::string get_file_name(const std::string& filename) {
  return std::filesystem::path(filename).filename().string();
}

std::string get_file_extension(const std::string& filename) {
  return filename.substr(filename.find_last_of('.') + 1);
}
