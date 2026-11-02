// Canonical catalog orchestration; preprocessing stays inside Animation Import.
#include "import.hpp"

#include <iostream>
#include <set>

bool AnimationLibrary::Load(const std::filesystem::path& manifest) {
  using namespace animation_import;
  try {
    const auto directory = fs::absolute(manifest).parent_path();
    const auto doc = ReadJson(manifest);
    Check(doc.at("schema") == "football_animation_catalog_v1" &&
          doc.at("rig_version") == "football_player_v1" && doc.at("fps") == 100, "unsupported catalog");
    Skeleton skeleton(directory / doc.at("skeleton").get<std::string>());
    const auto& entries = doc.at("clips");
    Check(entries.is_array() && !entries.empty() && entries.size() <= 100000, "invalid catalog size");
    bool procedural = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      Check(Integer(e.at("id")) == static_cast<int>(i), "non-contiguous clip IDs");
      const int kinds = int(e.contains("file")) + int(e.value("procedural", false)) + int(e.contains("mirror_of"));
      Check(kinds == 1, "clip must have exactly one source kind");
      procedural |= e.value("procedural", false);
    }
    std::vector<MotionTemplate> templates;
    if (procedural) templates = ReadTemplates(directory / doc.at("motion_templates").get<std::string>(),
                                              directory / doc.at("generation_keys").get<std::string>());
    std::vector<ImportedAnimationClip> clips(entries.size());
    std::set<fs::path> paths;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      if (e.contains("mirror_of")) continue;
      if (e.value("procedural", false)) clips[i] = Generate(e, templates);
      else {
        const auto path = SourcePath(directory, e.at("file"));
        Check(paths.insert(path).second, "duplicate GLB source path");
        clips[i] = ReadGlb(path, e, skeleton);
      }
    }
    std::vector<int> mirror_partner(entries.size(), -1);
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      if (!e.contains("mirror_of")) continue;
      const int source = Integer(e.at("mirror_of"));
      Check(source >= 0 && std::size_t(source) < entries.size() && source != int(i) &&
            !entries[source].contains("mirror_of"), "invalid mirror reference/chain");
      clips[i] = Mirror(clips[source]);
      MatchMetadata(MetadataJson(clips[i].metadata), MetadataJson(Metadata(e.at("metadata"))));
      ApplyEntry(clips[i], e);
      mirror_partner[i] = source;
      mirror_partner[static_cast<std::size_t>(source)] = static_cast<int>(i);
    }
    for (auto& clip : clips) DeriveFootSupport(clip);
    // Repair only pairs whose full cycle passes the existing strict gate.
    RepairGaitCycles(clips, mirror_partner);
    for (auto& clip : clips) {
      DeriveMotionQuality(clip);
      clip.looping = clip.metadata.action_type == e_DefString_Movement &&
                     clip.motion_quality.loop_continuous;
    }
    std::vector<::AnimationClip> resources;
    resources.reserve(clips.size());
    for (auto& clip : clips) {
      resources.push_back(std::move(static_cast<::AnimationClip&>(clip)));
    }
    clips_ = std::move(resources);
    return true;
  } catch (const std::exception& error) {
    std::cerr << "AnimationLibrary: " << manifest << ": " << error.what() << '\n';
    return false;
  }
}
