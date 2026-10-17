// S20 acceptance: the exported public contract must be self-contained.
//
// This TU includes only football/env.hpp. Its target links the exported
// contract targets and never adds the src/ implementation include root, so the
// build fails if any public header reaches back into src/.
#include "football/env.hpp"

namespace {

using TeamSetup = football::sim::TeamSetup;
using MatchOptionsValue = MatchOptions;
using MatchResultValue = MatchResult;
using SnapshotRecordValue = football::sim::observation::SnapshotRecord;
using EventTrajectoryValue = football::sim::event::EventTrajectory;

static_assert(sizeof(TeamSetup) > 0);
static_assert(sizeof(MatchOptionsValue) > 0);
static_assert(sizeof(MatchResultValue) > 0);
static_assert(sizeof(SnapshotRecordValue) > 0);
static_assert(sizeof(EventTrajectoryValue) > 0);

}  // namespace
