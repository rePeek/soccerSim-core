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

// Shared helper for the D4a invariant: the engine must run, and libgame.so
// must load, without any graphics library being mapped into the process.
//
// This reads /proc/self/maps, so it is Linux-only -- which this project is
// anyway (see the CMakeLists check).
//
// What it does and does not prove:
//   - mapped libEGL/libGLX/libOpenGL/libGL/libSDL2_image => the process has
//     the OpenGL/EGL/GLX stack in its address space. That is the thing D4a
//     removed, and it is checkable without a display.
//   - libX11 and libSDL2_gfx are deliberately NOT forbidden: SDL2 is still
//     linked for the .bmp image loader, SDL2 itself links X11, and
//     Surface::Resize uses SDL2_gfx zoomSurface on the asset import path.
//     Merely mapping X11 is not opening a display.

#ifndef FOOTBALL_HEADLESS_CHECK_HPP_
#define FOOTBALL_HEADLESS_CHECK_HPP_

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace football_headless {

inline const std::vector<std::string>& ForbiddenGraphicsLibraries() {
  static const std::vector<std::string> forbidden = {
      "libEGL.so", "libGLX.so", "libOpenGL.so", "libGL.so",
      "libGLdispatch.so", "libSDL2_image"};
  return forbidden;
}

// Returns the forbidden libraries currently mapped into this process.
inline std::vector<std::string> MappedGraphicsLibraries() {
  std::vector<std::string> found;
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    for (const std::string& name : ForbiddenGraphicsLibraries()) {
      if (line.find(name) != std::string::npos) {
        bool already_seen = false;
        for (const std::string& seen : found) {
          if (seen == name) already_seen = true;
        }
        if (!already_seen) found.push_back(name);
      }
    }
  }
  return found;
}

// Prints and returns true when the process is graphics-library free.
inline bool RequireNoGraphicsLibraries(const std::string& label) {
  const std::vector<std::string> found = MappedGraphicsLibraries();
  if (found.empty()) return true;
  std::cerr << label << ": graphics libraries are mapped into the process:";
  for (const std::string& name : found) std::cerr << ' ' << name;
  std::cerr << '\n';
  return false;
}

}  // namespace football_headless

#endif  // FOOTBALL_HEADLESS_CHECK_HPP_
