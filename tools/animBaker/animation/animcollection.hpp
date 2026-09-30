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

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifndef _HPP_FOOTBALL_ONTHEPITCH_ANIMCOLLECTION
#define _HPP_FOOTBALL_ONTHEPITCH_ANIMCOLLECTION

#include "animation/animation.hpp"

#include "animation/import_hierarchy.hpp"
#include "animation/import_loader.hpp"

#include "animation/quadrant.hpp"
#include "animation/selection_math.hpp"
#include "animation/selection_query.hpp"

using namespace blunted;

// Body-part names -> imported transform. See import_hierarchy.hpp.
void BuildImportNodeMap(ImportNode *targetNode, ImportNodeMap &nodeMap);

// Resolved source-asset locations. Passed explicitly so the loader does not
// depend on the runtime global GameConfig.
struct AnimationSourcePaths {
  std::string animation_dir;
  std::string template_dir;
  std::string player_object;
};

class AnimCollection {

  public:
    // scene3D for debugging pilon
    AnimCollection();
    virtual ~AnimCollection();

    void Clear();
    void Load(const AnimationSourcePaths& paths);

    const std::vector < Animation* > &GetAnimations() const;

    void CrudeSelection(DataSet &dataSet, const CrudeSelectionQuery &query);

    inline Animation* GetAnim(int index) {
      return animations.at(index);
    }

    inline const Quadrant &GetQuadrant(int id) {
      return quadrants.at(id);
    }

    int GetQuadrantID(Animation *animation, const Vector3 &movement, radian angle) const;

    void ProcessState(EnvState* state);

  protected:

    void _PrepareAnim(Animation *animation,
                      const std::vector<ImportNode *> &bodyParts,
                      const ImportNodeMap &nodeMap,
                      bool convertAngledDribbleToWalk = false);

    bool _CheckFunctionType(e_DefString functionType, e_FunctionType queryFunctionType) const;

    std::vector<Animation*> animations;
    std::vector<Quadrant> quadrants;

    radian maxIncomingBallDirectionDeviation;
    radian maxOutgoingBallDirectionDeviation;

};

#endif
