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

// Standalone animation baker. It drives the legacy AnimCollection::Load()/
// _PrepareAnim() importer directly (no runtime environment) and serializes the
// prepared clips into a plain binary "simanim" artifact.

#include "anim_baker.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "animation/animcollection.hpp"
#include "animation/animation.hpp"
#include "sim/animation/clip.hpp"
#include "sim/animation/library.hpp"
#include "animation/extensions/footballanimationextension.hpp"
#include "sim/animation/baked_selector.hpp"
#include "foundation/math/vector3.hpp"
#include "support/text/value_codec.hpp"

using namespace blunted;

namespace {

std::vector<AnimationClip> BakeClips(const std::vector<Animation*>& animations) {
  std::vector<AnimationClip> clips;
  clips.reserve(animations.size());
  for (size_t i = 0; i < animations.size(); ++i) {
    Animation* anim = animations[i];
    AnimationClip clip;
    clip.id = static_cast<uint32_t>(i);
    clip.name = anim->GetName();
    clip.frame_count = static_cast<uint32_t>(anim->GetFrameCount());
    clip.metadata.incoming_velocity = anim->GetIncomingVelocity();
    clip.metadata.outgoing_velocity = anim->GetOutgoingVelocity();
    clip.metadata.incoming_body_angle = anim->GetIncomingBodyAngle();
    clip.metadata.outgoing_body_angle = anim->GetOutgoingBodyAngle();
    clip.metadata.difficulty =
        static_cast<float>(std::atof(anim->GetVariable("animdifficultyfactor").c_str()));
    clip.metadata.touch_frame = std::atoi(anim->GetVariable("touchframe").c_str());
    clip.metadata.quadrant = anim->GetVariableCache().quadrant_id();
    clip.metadata.action_type = static_cast<int32_t>(anim->GetAnimType());
    clip.metadata.last_ditch = anim->GetVariableCache().lastditch();
    clip.metadata.base_animation = anim->GetVariableCache().baseanim();
    clip.metadata.idle_level = anim->GetVariableCache().idlelevel();
    clip.metadata.special_var1 = anim->GetVariableCache().specialvar1();
    clip.metadata.special_var2 = anim->GetVariableCache().specialvar2();
    clip.metadata.outgoing_angle = anim->GetOutgoingAngle();
    clip.metadata.incoming_body_direction = anim->GetIncomingBodyDirection();
    clip.metadata.outgoing_direction = anim->GetOutgoingDirection();
    clip.metadata.incoming_ball_direction =
        GetVectorFromString(anim->GetVariable("incomingballdirection"));
    clip.metadata.outgoing_ball_direction =
        GetVectorFromString(anim->GetVariable("balldirection"));
    clip.metadata.incoming_ball_direction_max_deviation =
        static_cast<float>(std::atof(anim->GetVariable("incomingballdirection_maxdeviation").c_str()));
    clip.metadata.outgoing_ball_direction_max_deviation =
        static_cast<float>(std::atof(anim->GetVariable("outgoingballdirection_maxdeviation").c_str()));
    clip.metadata.trip_type =
        static_cast<int32_t>(std::round(std::atof(anim->GetVariable("triptype").c_str())));
    clip.metadata.current_foot = static_cast<int32_t>(anim->GetCurrentFoot());
    clip.metadata.incoming_retain_state = anim->GetVariableCache().incoming_retain_state();
    clip.metadata.outgoing_retain_state = anim->GetVariable("outgoing_retain_state");
    clip.metadata.incoming_special_state = anim->GetVariableCache().incoming_special_state();
    clip.metadata.forced_foot = anim->GetVariable("forcedfoot");
    clip.metadata.touch_foot = anim->GetVariable("touchfoot");
    clip.metadata.outgoing_special_state =
        anim->GetVariableCache().outgoing_special_state();
    clip.metadata.touch_bodypart = anim->GetVariable("touch_bodypart");
    clip.metadata.touch_max_power_factor = static_cast<float>(
        std::atof(anim->GetVariable("touch_maxpowerfactor").c_str()));
    clip.metadata.touch_difficulty_factor = static_cast<float>(
        std::atof(anim->GetVariable("touch_difficultyfactor").c_str()));
    clip.metadata.priority =
        static_cast<float>(std::atof(anim->GetVariable("priority").c_str()));
    clip.metadata.bump_direction =
        GetVectorFromString(anim->GetVariable("bumpdirection"));
    clip.metadata.incoming_movement = anim->GetIncomingMovement();
    clip.metadata.outgoing_movement = anim->GetOutgoingMovement();
    clip.metadata.translation = anim->GetTranslation();
    clip.metadata.outgoing_foot = static_cast<int32_t>(anim->GetOutgoingFoot());
    // Contact (first touch at the default touchframe).
    auto football = std::static_pointer_cast<FootballAnimationExtension>(
        anim->GetExtension("football"));
    if (clip.metadata.touch_frame >= 0) {
      Vector3 contact_pos;
      if (football->GetTouchPos(clip.metadata.touch_frame, contact_pos)) {
        clip.has_contact = true;
        clip.contact_position = contact_pos;
      }
    }
    // Full touch list, mirroring FootballAnimationExtension::GetTouch.
    const int touch_count = football->GetTouchCount();
    clip.touches.reserve(touch_count);
    for (int t = 0; t < touch_count; ++t) {
      BakedTouch bt;
      Vector3 pos;
      int frame = 0;
      if (football->GetTouch(t, pos, frame)) {
        bt.frame = frame;
        bt.position = pos;
        clip.touches.push_back(bt);
      }
    }

    clip.root_positions.reserve(clip.frame_count);
    clip.poses.reserve(clip.frame_count);
    for (uint32_t frame = 0; frame < clip.frame_count; ++frame) {
      // Root motion (player body part, z zeroed for 2D simulation).
      Quaternion orientation;
      Vector3 position;
      anim->GetKeyFrame(player, static_cast<int>(frame), orientation, position);
      position.coords[2] = 0.0f;
      clip.root_positions.push_back(position);

      // Full per-joint pose.
      PoseFrame pose;
      for (size_t b = 0; b < kBodyPartCount; ++b) {
        Quaternion o;
        Vector3 p;
        anim->GetKeyFrame(static_cast<BodyPart>(b), static_cast<int>(frame), o, p);
        pose.orientations[b] = o;
        pose.positions[b] = p;
      }
      clip.poses.push_back(std::move(pose));
    }

    clips.push_back(std::move(clip));
  }
  return clips;
}

void Serialize(std::ostream& os, const std::vector<AnimationClip>& clips) {
  SimAnimWriteHeader(os, static_cast<uint32_t>(clips.size()));
  for (const AnimationClip& clip : clips) {
    clip.Serialize(os);
  }
}

std::vector<char> SerializeToBuffer(const std::vector<AnimationClip>& clips) {
  std::ostringstream oss(std::ios::binary);
  Serialize(oss, clips);
  const std::string s = oss.str();
  return std::vector<char>(s.begin(), s.end());
}

int Export(const std::string& path, const std::vector<AnimationClip>& clips) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  if (!os) {
    std::cerr << "anim_baker: cannot open " << path << " for writing\n";
    return 1;
  }
  Serialize(os, clips);
  os.flush();
  if (!os) {
    std::cerr << "anim_baker: failed writing " << path << "\n";
    return 1;
  }
  std::cout << "anim_baker: wrote " << clips.size() << " clips to " << path
            << "\n";
  return 0;
}

int Check(const std::string& path, const std::vector<AnimationClip>& baked) {
  AnimationLibrary library;
  if (!library.Load(path)) {
    std::cerr << "anim_baker: CHECK FAILED: cannot load " << path << "\n";
    return 1;
  }
  if (library.Size() != baked.size()) {
    std::cerr << "anim_baker: CHECK FAILED: clip count mismatch\n";
    return 1;
  }

  // Re-derive the expected bytes from the freshly baked clips and compare.
  const std::vector<char> expected = SerializeToBuffer(baked);
  std::ifstream is(path, std::ios::binary);
  if (!is) return 1;
  is.seekg(0, std::ios::end);
  const std::streamoff size = is.tellg();
  if (size < 0 || static_cast<std::uintmax_t>(size) != expected.size()) {
    std::cerr << "anim_baker: CHECK FAILED: artifact size mismatch\n";
    return 1;
  }
  is.seekg(0, std::ios::beg);
  std::vector<char> on_disk(expected.size());
  is.read(on_disk.data(), size);
  if (!is || on_disk != expected) {
    std::cerr << "anim_baker: CHECK FAILED: artifact does not match a fresh bake\n";
    return 1;
  }
  std::cout << "anim_baker: CHECK PASSED (" << baked.size() << " clips, "
            << expected.size() << " bytes, byte-identical + readable)\n";
  return 0;
}

int Verify(const std::vector<Animation*>& legacy,
          const AnimationLibrary& library) {
  if (legacy.size() != library.Size()) {
    std::cerr << "anim_baker: VERIFY FAILED: clip count " << legacy.size()
              << " vs " << library.Size() << "\n";
    return 1;
  }

  size_t mismatches = 0;
  for (size_t i = 0; i < legacy.size(); ++i) {
    Animation* anim = legacy[i];
    const AnimationClip& clip = library.Clips()[i];

    if (anim->GetName() != clip.name) {
      std::cerr << "VERIFY clip " << i << ": name mismatch\n";
      ++mismatches;
    }
    if (static_cast<uint32_t>(anim->GetFrameCount()) != clip.frame_count) {
      std::cerr << "VERIFY clip " << i << ": frame count mismatch\n";
      ++mismatches;
      continue;
    }

    for (uint32_t f = 0; f < clip.frame_count; ++f) {
      Quaternion o;
      Vector3 p;
      anim->GetKeyFrame(player, static_cast<int>(f), o, p);
      p.coords[2] = 0.0f;
      for (int c = 0; c < 3; ++c) {
        if (p.coords[c] != clip.root_positions[f].coords[c]) {
          std::cerr << "VERIFY clip " << i << " frame " << f
                    << ": root mismatch\n";
          ++mismatches;
          break;
        }
      }
      for (size_t b = 0; b < kBodyPartCount; ++b) {
        anim->GetKeyFrame(static_cast<BodyPart>(b), static_cast<int>(f), o, p);
        for (int e = 0; e < 4; ++e) {
          if (o.elements[e] != clip.poses[f].orientations[b].elements[e]) {
            std::cerr << "VERIFY clip " << i << " frame " << f
                      << " body " << b << ": orientation mismatch\n";
            ++mismatches;
          }
        }
        for (int c = 0; c < 3; ++c) {
          if (p.coords[c] != clip.poses[f].positions[b].coords[c]) {
            std::cerr << "VERIFY clip " << i << " frame " << f
                      << " body " << b << ": position mismatch\n";
            ++mismatches;
          }
        }
      }
    }

    const FootballAnimationMetadata& m = clip.metadata;
    if (m.action_type != static_cast<int32_t>(anim->GetAnimType()) ||
        m.incoming_velocity != anim->GetIncomingVelocity() ||
        m.outgoing_velocity != anim->GetOutgoingVelocity() ||
        m.incoming_body_angle != anim->GetIncomingBodyAngle() ||
        m.outgoing_body_angle != anim->GetOutgoingBodyAngle() ||
        m.difficulty !=
            static_cast<float>(
                std::atof(anim->GetVariable("animdifficultyfactor").c_str())) ||
        m.touch_frame != std::atoi(anim->GetVariable("touchframe").c_str()) ||
        m.quadrant != anim->GetVariableCache().quadrant_id() ||
        m.last_ditch != anim->GetVariableCache().lastditch() ||
        m.base_animation != anim->GetVariableCache().baseanim() ||
        m.idle_level != anim->GetVariableCache().idlelevel() ||
        m.special_var1 != anim->GetVariableCache().specialvar1() ||
        m.special_var2 != anim->GetVariableCache().specialvar2() ||
        m.outgoing_angle != anim->GetOutgoingAngle() ||
        m.incoming_body_direction != anim->GetIncomingBodyDirection() ||
        m.outgoing_direction != anim->GetOutgoingDirection() ||
        m.incoming_ball_direction !=
            GetVectorFromString(anim->GetVariable("incomingballdirection")) ||
        m.outgoing_ball_direction !=
            GetVectorFromString(anim->GetVariable("balldirection")) ||
        m.incoming_ball_direction_max_deviation !=
            static_cast<float>(std::atof(
                anim->GetVariable("incomingballdirection_maxdeviation").c_str())) ||
        m.outgoing_ball_direction_max_deviation !=
            static_cast<float>(std::atof(
                anim->GetVariable("outgoingballdirection_maxdeviation").c_str())) ||
        m.trip_type !=
            static_cast<int32_t>(std::round(
                std::atof(anim->GetVariable("triptype").c_str()))) ||
        m.current_foot != static_cast<int32_t>(anim->GetCurrentFoot()) ||
        m.incoming_retain_state != anim->GetVariableCache().incoming_retain_state() ||
        m.outgoing_retain_state != anim->GetVariable("outgoing_retain_state") ||
        m.incoming_special_state !=
            anim->GetVariableCache().incoming_special_state() ||
        m.forced_foot != anim->GetVariable("forcedfoot") ||
        m.touch_foot != anim->GetVariable("touchfoot") ||
        m.outgoing_special_state !=
            anim->GetVariableCache().outgoing_special_state() ||
        m.touch_bodypart != anim->GetVariable("touch_bodypart") ||
        m.touch_max_power_factor !=
            static_cast<float>(std::atof(
                anim->GetVariable("touch_maxpowerfactor").c_str())) ||
        m.touch_difficulty_factor !=
            static_cast<float>(std::atof(
                anim->GetVariable("touch_difficultyfactor").c_str())) ||
        m.priority !=
            static_cast<float>(std::atof(
                anim->GetVariable("priority").c_str())) ||
        m.bump_direction !=
            GetVectorFromString(anim->GetVariable("bumpdirection")) ||
        m.incoming_movement != anim->GetIncomingMovement() ||
        m.outgoing_movement != anim->GetOutgoingMovement() ||
        m.translation != anim->GetTranslation() ||
        m.outgoing_foot != static_cast<int32_t>(anim->GetOutgoingFoot())) {
      std::cerr << "VERIFY clip " << i << ": metadata mismatch\n";
      ++mismatches;
    }

    Vector3 cp;
    const bool has = std::static_pointer_cast<FootballAnimationExtension>(
                         anim->GetExtension("football"))
                         ->GetTouchPos(m.touch_frame, cp);
    if (has != clip.has_contact ||
        (has && (cp.coords[0] != clip.contact_position.coords[0] ||
                 cp.coords[1] != clip.contact_position.coords[1] ||
                 cp.coords[2] != clip.contact_position.coords[2]))) {
      std::cerr << "VERIFY clip " << i << ": contact mismatch\n";
      ++mismatches;
    }

    const auto football = std::static_pointer_cast<FootballAnimationExtension>(
        anim->GetExtension("football"));
    if (static_cast<int>(clip.touches.size()) != football->GetTouchCount()) {
      std::cerr << "VERIFY clip " << i << ": touch count mismatch\n";
      ++mismatches;
    } else {
      for (int t = 0; t < static_cast<int>(clip.touches.size()); ++t) {
        Vector3 pos;
        int frame = 0;
        football->GetTouch(t, pos, frame);
        if (clip.touches[t].frame != frame ||
            clip.touches[t].position.coords[0] != pos.coords[0] ||
            clip.touches[t].position.coords[1] != pos.coords[1] ||
            clip.touches[t].position.coords[2] != pos.coords[2]) {
          std::cerr << "VERIFY clip " << i << ": touch " << t
                    << " mismatch\n";
          ++mismatches;
          break;
        }
      }
    }
  }

  if (mismatches) {
    std::cerr << "anim_baker: VERIFY FAILED (" << mismatches << " mismatches)\n";
    return 1;
  }
  std::cout << "anim_baker: VERIFY PASSED (" << legacy.size()
            << " clips, all fields equivalent)\n";
  return 0;
}

std::vector<CrudeSelectionQuery> BuildQueryMatrix() {
  std::vector<CrudeSelectionQuery> queries;

  const e_Velocity velocities[] = {e_Velocity_Idle, e_Velocity_Dribble,
                                    e_Velocity_Walk, e_Velocity_Sprint};
  const e_FunctionType types[] = {
      e_FunctionType_Movement,   e_FunctionType_BallControl,
      e_FunctionType_Trap,       e_FunctionType_ShortPass,
      e_FunctionType_LongPass,   e_FunctionType_HighPass,
      e_FunctionType_Shot,       e_FunctionType_Deflect,
      e_FunctionType_Catch,      e_FunctionType_Interfere,
      e_FunctionType_Trip,       e_FunctionType_Sliding,
      e_FunctionType_Special};
  const Vector3 dirs[] = {Vector3(0, -1, 0), Vector3(1, 0, 0),
                           Vector3(0, 1, 0), Vector3(-1, 0, 0)};

  // byFunctionType x byIncomingVelocity (strict and non-strict).
  for (e_FunctionType t : types) {
    for (e_Velocity v : velocities) {
      for (int strict = 0; strict < 2; ++strict) {
        CrudeSelectionQuery q;
        q.byFunctionType = true;
        q.functionType = t;
        q.byIncomingVelocity = true;
        q.incomingVelocity = v;
        q.incomingVelocity_Strict = strict != 0;
        queries.push_back(q);
      }
    }
  }

  // Velocity force-linearity + no-dribble-to-idle/sprint.
  for (int f = 0; f < 2; ++f) {
    CrudeSelectionQuery q;
    q.byFunctionType = true;
    q.functionType = e_FunctionType_Movement;
    q.byIncomingVelocity = true;
    q.incomingVelocity = e_Velocity_Walk;
    q.incomingVelocity_ForceLinearity = f != 0;
    q.incomingVelocity_NoDribbleToIdle = true;
    q.incomingVelocity_NoDribbleToSprint = true;
    queries.push_back(q);
  }

  // byOutgoingVelocity.
  for (e_Velocity v : velocities) {
    CrudeSelectionQuery q;
    q.byOutgoingVelocity = true;
    q.outgoingVelocity = v;
    queries.push_back(q);
  }

  // bySide.
  for (const Vector3& d : dirs) {
    CrudeSelectionQuery q;
    q.bySide = true;
    q.lookAtVecRel = d;
    queries.push_back(q);
  }

  // byPickupBall.
  for (int p = 0; p < 2; ++p) {
    CrudeSelectionQuery q;
    q.byPickupBall = true;
    q.pickupBall = p != 0;
    queries.push_back(q);
  }

  // allowLastDitchAnims.
  for (int a = 0; a < 2; ++a) {
    CrudeSelectionQuery q;
    q.allowLastDitchAnims = a != 0;
    queries.push_back(q);
  }

  // byIncomingBodyDirection (strict x forceLinearity).
  for (const Vector3& d : dirs) {
    for (int s = 0; s < 2; ++s) {
      for (int f = 0; f < 2; ++f) {
        CrudeSelectionQuery q;
        q.byIncomingBodyDirection = true;
        q.incomingBodyDirection = d;
        q.incomingBodyDirection_Strict = s != 0;
        q.incomingBodyDirection_ForceLinearity = f != 0;
        queries.push_back(q);
      }
    }
  }

  // byIncomingBallDirection. Real runtime only enables this for Trap,
  // Interfere and Deflect (the types that always carry an incoming ball
  // direction).
  const e_FunctionType incoming_ball_types[] = {
      e_FunctionType_Trap, e_FunctionType_Interfere, e_FunctionType_Deflect};
  for (e_FunctionType t : incoming_ball_types) {
    for (const Vector3& d : dirs) {
      CrudeSelectionQuery q;
      q.byFunctionType = true;
      q.functionType = t;
      q.byIncomingBallDirection = true;
      q.incomingBallDirection = d;
      queries.push_back(q);
    }
  }

  // byOutgoingBallDirection (touch types; no fatal on a missing value).
  const e_FunctionType touch_types[] = {
      e_FunctionType_BallControl, e_FunctionType_Trap,   e_FunctionType_ShortPass,
      e_FunctionType_LongPass,   e_FunctionType_HighPass, e_FunctionType_Shot,
      e_FunctionType_Deflect,    e_FunctionType_Catch,    e_FunctionType_Interfere};
  for (e_FunctionType t : touch_types) {
    for (const Vector3& d : dirs) {
      CrudeSelectionQuery q;
      q.byFunctionType = true;
      q.functionType = t;
      q.byOutgoingBallDirection = true;
      q.outgoingBallDirection = d;
      queries.push_back(q);
    }
  }

  // byTripType.
  for (int t = 1; t <= 3; ++t) {
    CrudeSelectionQuery q;
    q.byFunctionType = true;
    q.functionType = e_FunctionType_Trip;
    q.byTripType = true;
    q.tripType = t;
    queries.push_back(q);
  }

  // heedForcedFoot.
  for (e_Foot foot : {e_Foot_Left, e_Foot_Right}) {
    CrudeSelectionQuery q;
    q.heedForcedFoot = true;
    q.strongFoot = foot;
    queries.push_back(q);
  }

  return queries;
}

int VerifySelection(AnimCollection& legacy,
                    const AnimationLibrary& library) {
  const std::vector<AnimationClip>& clips = library.Clips();
  const std::vector<CrudeSelectionQuery> queries = BuildQueryMatrix();

  size_t mismatches = 0;
  for (size_t qi = 0; qi < queries.size(); ++qi) {
    const CrudeSelectionQuery& q = queries[qi];

    DataSet legacy_set;
    legacy.CrudeSelection(legacy_set, q);

    DataSet baked_set;
    BakedAnimationSelector::CrudeSelection(clips, q, baked_set);

    if (legacy_set.size() != baked_set.size()) {
      std::cerr << "VERIFY-SELECTION query " << qi << ": size "
                << legacy_set.size() << " vs " << baked_set.size() << "\n";
      ++mismatches;
      continue;
    }
    for (size_t k = 0; k < legacy_set.size(); ++k) {
      if (legacy_set[k] != baked_set[k]) {
        std::cerr << "VERIFY-SELECTION query " << qi << " index " << k
                  << ": " << legacy_set[k] << " vs " << baked_set[k] << "\n";
        ++mismatches;
        break;
      }
    }
  }

  if (mismatches) {
    std::cerr << "anim_baker: VERIFY-SELECTION FAILED (" << mismatches
              << " mismatches over " << queries.size() << " queries)\n";
    return 1;
  }
  std::cout << "anim_baker: VERIFY-SELECTION PASSED (" << queries.size()
            << " queries, exact vectors)\n";
  return 0;
}
}  // namespace

namespace football::tools {
namespace {
AnimationSourcePaths SourcePaths(const std::filesystem::path& input_dir) {
  const auto data_dir = std::filesystem::absolute(input_dir);
  return {(data_dir / "media/animations").string(),
          (data_dir / "media/animations/templates").string(),
          (data_dir / "media/objects/players/player.object").string()};
}
}  // namespace

std::vector<AnimationClip> BakeAnimations(const std::filesystem::path& data_dir) {
  AnimCollection anims;
  anims.Load(SourcePaths(data_dir));
  return BakeClips(anims.GetAnimations());
}

std::vector<char> SerializeAnimations(const std::vector<AnimationClip>& clips) {
  return SerializeToBuffer(clips);
}

int WriteAnimations(const std::filesystem::path& artifact,
                    const std::vector<AnimationClip>& clips) {
  return Export(artifact.string(), clips);
}

int CheckAnimations(const std::filesystem::path& artifact,
                    const std::vector<AnimationClip>& fresh_bake) {
  return Check(artifact.string(), fresh_bake);
}

int VerifyAnimations(const std::filesystem::path& data_dir,
                     const std::filesystem::path& artifact) {
  AnimCollection anims;
  anims.Load(SourcePaths(data_dir));
  AnimationLibrary library;
  if (!library.Load(artifact.string())) return 1;
  return Verify(anims.GetAnimations(), library);
}

int VerifyAnimationSelection(const std::filesystem::path& data_dir,
                             const std::filesystem::path& artifact) {
  AnimCollection anims;
  anims.Load(SourcePaths(data_dir));
  AnimationLibrary library;
  if (!library.Load(artifact.string())) return 1;
  return VerifySelection(anims, library);
}
}  // namespace football::tools