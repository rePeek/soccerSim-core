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

#include "image.hpp"

#include "../cmake/file.h"
#include "../main.hpp"

namespace blunted {

SDL_Surface *LoadImage(const std::string &file) {
  DO_VALIDATION;
  std::string name = GetGameConfig().updatePath(file);
  name = name.substr(0, name.length() - 4) + ".bmp";
  std::string file_data = GetFile(name);
  SDL_RWops *rw = SDL_RWFromConstMem(file_data.data(), file_data.size());
  auto image = SDL_LoadBMP_RW(rw, 1);

  if (image == nullptr) {
    Log(e_FatalError, "LoadImage", "Load", "Could not load " + name);
    return nullptr;
  }

  if (image->format->format == SDL_PIXELFORMAT_ARGB8888) {
    DO_VALIDATION;
    SDL_Surface *tmp =
        SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(image);
    image = tmp;
  } else if (image->format->format == SDL_PIXELFORMAT_BGR24) {
    DO_VALIDATION;
    SDL_Surface *tmp =
        SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(image);
    image = tmp;
  }
  return image;
}

}  // namespace blunted
