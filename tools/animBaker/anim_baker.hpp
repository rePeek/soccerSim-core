#ifndef FOOTBALL_TOOLS_ANIM_BAKER_HPP
#define FOOTBALL_TOOLS_ANIM_BAKER_HPP

#include <filesystem>
#include <vector>

#include "football/animation/clip.hpp"

namespace football::tools {

// Offline operations, shared by the CLI and behavioral tests. No simulation/AI.
std::vector<AnimationClip> BakeAnimations(const std::filesystem::path& data_dir);
// A1b: run the same legacy generator against only the 10 templates, returning
// the 443 non-mirror procedural clips. Mirrors remain virtual.
std::vector<AnimationClip> BakeAutoAnims(
    const std::filesystem::path& template_dir,
    const std::filesystem::path& player_object);
std::vector<char> SerializeAnimations(const std::vector<AnimationClip>& clips);
int WriteAnimations(const std::filesystem::path& artifact,
                    const std::vector<AnimationClip>& clips);
int CheckAnimations(const std::filesystem::path& artifact,
                    const std::vector<AnimationClip>& fresh_bake);
int VerifyAnimations(const std::filesystem::path& data_dir,
                     const std::filesystem::path& artifact);
int VerifyAnimationSelection(const std::filesystem::path& data_dir,
                             const std::filesystem::path& artifact);

}  // namespace football::tools
#endif
