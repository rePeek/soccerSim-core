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

// Loads libgame.so the way a host application would -- dlopen, RTLD_NOW --
// and asserts that merely loading it does not drag the graphics stack into
// the process.
//
// This binary is deliberately NOT linked against libgame.so, so the dlopen is
// the only thing that loads it. RTLD_NOW makes the load fail on any
// unresolved symbol, and the /proc/self/maps check then proves that no
// libEGL/libGLX/libOpenGL/libGL/libSDL2_image is mapped as a result.
//
// Known scope limit: this cannot create a GameEnv through dlopen, because the
// engine exposes no C entry point yet -- that arrives with the host
// integration API. The full lifecycle (create, reset, step, destroy) in a
// graphics-free process is covered by football_smoke, which links the library
// and checks the same invariant from inside the running process.

#include <dlfcn.h>

#include <iostream>
#include <string>

#include "headless_check.hpp"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <path-to-libgame.so>\n";
    return 2;
  }
  const std::string library = argv[1];

  if (!football_headless::RequireNoGraphicsLibraries("before dlopen")) {
    return 1;
  }

  void* handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    std::cerr << "dlopen(" << library << ") failed: " << dlerror() << '\n';
    return 1;
  }

  const bool ok = football_headless::RequireNoGraphicsLibraries(
      "after dlopen(" + library + ")");

  dlclose(handle);

  if (!ok) return 1;
  std::cout << "football_headless_load: PASS (" << library
            << " loaded without graphics libraries)\n";
  return 0;
}
