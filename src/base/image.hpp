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

#ifndef _hpp_base_image
#define _hpp_base_image

#include <string>

#include "sdl_surface.hpp"

namespace blunted {

// Loads an image from the game data directory. The resource is expected to be
// stored as an uncompressed .bmp file; the requested extension is replaced
// accordingly. This lives outside of the GUI code because the simulation and
// renderer asset loading still depend on it.
SDL_Surface *LoadImage(const std::string &file);

}  // namespace blunted

#endif
