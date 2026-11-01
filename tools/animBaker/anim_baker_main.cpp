#include <exception>
#include <iostream>
#include <string>

#include "anim_baker.hpp"

int main(int argc, char** argv) {
  std::string input_dir, out_path, check_path, verify_path;
  std::string autogen_out, template_dir, player_object;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--input" && i + 1 < argc) input_dir = argv[++i];
    else if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (arg == "--check" && i + 1 < argc) check_path = argv[++i];
    else if (arg == "--verify" && i + 1 < argc) verify_path = argv[++i];
    else if (arg == "--autogen-out" && i + 1 < argc) autogen_out = argv[++i];
    else if (arg == "--template-dir" && i + 1 < argc) template_dir = argv[++i];
    else if (arg == "--player-object" && i + 1 < argc) player_object = argv[++i];
    else {
      std::cerr << "usage: " << argv[0]
                << " --input DIR [--out FILE] [--check FILE] [--verify FILE]"
                << "\n"
                << "       " << argv[0]
                << " --autogen-out FILE --template-dir DIR --player-object FILE\n";
      return 2;
    }
  }
  const bool autogen_mode = !autogen_out.empty();
  if (autogen_mode) {
    if (template_dir.empty() || player_object.empty()) {
      std::cerr << "anim_baker: autogen mode requires --template-dir and --player-object\n";
      return 2;
    }
  } else {
    if (input_dir.empty()) {
      std::cerr << "anim_baker: --input DIR is required\n";
      return 2;
    }
    if (out_path.empty() && check_path.empty() && verify_path.empty()) {
      std::cerr << "anim_baker: specify --out, --check and/or --verify\n";
      return 2;
    }
  }

  try {
    using namespace football::tools;
    int status = 0;
    if (autogen_mode) {
      return WriteAnimations(autogen_out, BakeAutoAnims(template_dir, player_object));
    }
    if (!out_path.empty() || !check_path.empty()) {
      const auto clips = BakeAnimations(input_dir);
      if (!out_path.empty()) status |= WriteAnimations(out_path, clips);
      if (!check_path.empty()) status |= CheckAnimations(check_path, clips);
    }
    if (!verify_path.empty()) status |= VerifyAnimations(input_dir, verify_path);
    return status;
  } catch (const std::exception& error) {
    std::cerr << "anim_baker: " << error.what() << '\n';
    return 1;
  }
}
