#include "football/animation/library.hpp"

bool AnimationLibrary::Load(const std::filesystem::path& manifest) {
  return LoadCatalog(manifest);
}
