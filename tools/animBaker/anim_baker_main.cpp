#include <iostream>
#include <string>

#include "anim_baker.hpp"

int main(int argc, char** argv) {
  std::string input_dir, out_path, check_path, verify_path, verify_selection_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--input" && i + 1 < argc) input_dir = argv[++i];
    else if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (arg == "--check" && i + 1 < argc) check_path = argv[++i];
    else if (arg == "--verify" && i + 1 < argc) verify_path = argv[++i];
    else if (arg == "--verify-selection" && i + 1 < argc) verify_selection_path = argv[++i];
    else {
      std::cerr << "usage: " << argv[0]
                << " --input DIR [--out FILE] [--check FILE] [--verify FILE]"
                << " [--verify-selection FILE]\n";
      return 2;
    }
  }
  if (input_dir.empty()) {
    std::cerr << "anim_baker: --input DIR is required\n";
    return 2;
  }
  if (out_path.empty() && check_path.empty() && verify_path.empty() && verify_selection_path.empty()) {
    std::cerr << "anim_baker: specify --out, --check, --verify and/or --verify-selection\n";
    return 2;
  }

  using namespace football::tools;
  int status = 0;
  if (!out_path.empty() || !check_path.empty()) {
    const auto clips = BakeAnimations(input_dir);
    if (!out_path.empty()) status |= WriteAnimations(out_path, clips);
    if (!check_path.empty()) status |= CheckAnimations(check_path, clips);
  }
  if (!verify_path.empty()) status |= VerifyAnimations(input_dir, verify_path);
  if (!verify_selection_path.empty()) status |= VerifyAnimationSelection(input_dir, verify_selection_path);
  return status;
}
