#pragma once

#include <vector>

#include "football/team/selection.hpp"

namespace football::team {

// Declared taker priority per restart category. Runtime combines this with live
// eligibility (on field, not dismissed, legal taker) and owns any current
// restart progress. A player may appear in several categories, never twice in
// one. Shoot-out rotation is deliberately outside this first static model.
struct SetPieceOrders {
  std::vector<football::player::PlayerId> penalties;
  std::vector<football::player::PlayerId> direct_free_kicks;
  std::vector<football::player::PlayerId> indirect_free_kicks;
  std::vector<football::player::PlayerId> corners_left;
  std::vector<football::player::PlayerId> corners_right;
  std::vector<football::player::PlayerId> throw_ins;
};

}  // namespace football::team
