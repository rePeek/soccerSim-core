// RFE-7B: the Referee rule surface consumes stable value facts and emits only
// RulingSink verdicts. No Player object, Simulation or RuleCommandSink is
// involved, so the foul verdicts are tested at module level.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

#include "football/pitch/pitch.hpp"
#include "football/animation/types.hpp"
#include "referee/offside.hpp"
#include "referee/referee.hpp"
#include "referee/ruling.hpp"

namespace {

using football::pitch::Pitch;
using football::sim::FoulAssessment;
using football::sim::FoulKind;
using football::sim::TeamSide;
using football::sim::Tick;
using football::sim::event::AwardRestartRuling;
using football::sim::event::BallInPlayRuling;
using football::sim::event::CardRuling;
using football::sim::event::RefereeRuling;
using football::sim::event::RestartCauseKind;
using football::sim::event::RulingSink;
using football::sim::event::StopPlayRuling;
using football::sim::rules::BallTouchFacts;
using football::sim::rules::RefereePlayerFact;
using football::sim::rules::RefereeTeamFact;

class Collect final : public RulingSink {
 public:
  void Submit(const RefereeRuling& ruling) override { rulings.push_back(ruling); }
  std::vector<RefereeRuling> rulings;
};

int CountRestarts(const std::vector<RefereeRuling>& rulings) {
  int count = 0;
  for (const RefereeRuling& ruling : rulings) {
    if (std::holds_alternative<AwardRestartRuling>(ruling)) ++count;
  }
  return count;
}

const CardRuling* FirstCard(const std::vector<RefereeRuling>& rulings) {
  for (const RefereeRuling& ruling : rulings) {
    if (const auto* card = std::get_if<CardRuling>(&ruling)) return card;
  }
  return nullptr;
}

FoulAssessment Sliding(Tick at, float severity) {
  FoulAssessment fact;
  fact.kind = FoulKind::SlidingTackle;
  fact.offender = 11;
  fact.victim = 1;
  fact.offender_team_id = 1;
  fact.victim_team_id = 0;
  fact.contacted_at = at;
  fact.position = football::math::Vector3(4, 3, 0);
  fact.offender_action_type = static_cast<int>(e_FunctionType_Sliding);
  fact.ball_position = football::math::Vector3(0, 0, 0);
  fact.victim_position = football::math::Vector3(0, 0, 0);
  fact.offender_last_touch_tick = {};
  fact.severity = severity;
  return fact;
}

FoulAssessment Standing(Tick at) {
  FoulAssessment fact;
  fact.kind = FoulKind::StandingFall;
  fact.offender = 11;
  fact.victim = 1;
  fact.offender_team_id = 1;
  fact.victim_team_id = 0;
  fact.contacted_at = at;
  fact.position = football::math::Vector3(4, 3, 0);
  fact.offender_action_type = static_cast<int>(e_FunctionType_Interfere);
  fact.ball_position = football::math::Vector3(0, 0, 0);
  fact.victim_position = football::math::Vector3(0, 0, 0);
  fact.victim_team_fading_possession = 1.2f;
  return fact;
}

FoulLiveFacts Live(float victim_fade) {
  FoulLiveFacts live;
  live.home = {0, -1, victim_fade};
  live.away = {1, 1, 1.0f};
  return live;
}

void Check(Referee& rules, Tick now, const FoulLiveFacts& live, Collect& sink) {
  rules.CheckFoul(now, Pitch{}, PitchFrameTransform{false}, live, sink);
}

}  // namespace

TEST_CASE("a sliding tackle confirms a foul and a free kick", "[unit][referee]") {
  Referee rules;
  rules.AssessFoul(Sliding(Tick{100}, 1.2f), Tick{100}, false);
  Collect sink;
  Check(rules, Tick{100}, Live(1.2f), sink);
  REQUIRE(sink.rulings.size() == 2);
  REQUIRE(std::holds_alternative<StopPlayRuling>(sink.rulings.at(0)));
  const auto& award = std::get<AwardRestartRuling>(sink.rulings.at(1));
  REQUIRE(award.kind == e_GameMode_FreeKick);
  REQUIRE(award.team == TeamSide::Home);
  REQUIRE(award.cause.kind == RestartCauseKind::Foul);
  REQUIRE(award.cause.offender == 11);
  REQUIRE(award.cause.victim == 1);
}

TEST_CASE("a restart in flight suppresses a new foul", "[unit][referee]") {
  Referee rules;
  rules.AssessFoul(Sliding(Tick{100}, 1.2f), Tick{100}, true);
  Collect sink;
  Check(rules, Tick{100}, Live(1.2f), sink);
  REQUIRE(sink.rulings.empty());
}

TEST_CASE("severity selects yellow and red cards", "[unit][referee]") {
  {
    Referee rules;
    rules.AssessFoul(Sliding(Tick{100}, 1.6f), Tick{100}, false);
    Collect sink;
    Check(rules, Tick{100}, Live(1.2f), sink);
    REQUIRE(FirstCard(sink.rulings) != nullptr);
    REQUIRE(FirstCard(sink.rulings)->type == 2);
  }
  {
    Referee rules;
    rules.AssessFoul(Sliding(Tick{100}, 2.2f), Tick{100}, false);
    Collect sink;
    Check(rules, Tick{100}, Live(1.2f), sink);
    REQUIRE(FirstCard(sink.rulings) != nullptr);
    REQUIRE(FirstCard(sink.rulings)->type == 3);
  }
}

TEST_CASE("an advantage is kept, recalled or expires", "[unit][referee]") {
  {
    // Not yet rechecked: no verdict.
    Referee rules;
    rules.AssessFoul(Standing(Tick{100}), Tick{100}, false);
    Collect sink;
    Check(rules, Tick{100}, Live(1.2f), sink);
    REQUIRE(sink.rulings.empty());
  }
  {
    // Possession did not recover, so the advantage is recalled.
    Referee rules;
    rules.AssessFoul(Standing(Tick{100}), Tick{100}, false);
    Collect sink;
    Check(rules, Tick{200}, Live(0.5f), sink);
    REQUIRE(CountRestarts(sink.rulings) == 1);
  }
  {
    // Possession held for long enough: the foul is cancelled, not awarded.
    Referee rules;
    rules.AssessFoul(Standing(Tick{100}), Tick{100}, false);
    Collect sink;
    Check(rules, Tick{500}, Live(1.2f), sink);
    REQUIRE(CountRestarts(sink.rulings) == 0);
  }
}

TEST_CASE("the offside line is the one-but-deepest defender", "[unit][referee]") {
  const Pitch pitch;
  const std::vector<football::sim::rules::RefereePlayerFact> players = {
      {1, 1, football::math::Vector3(40, 0, 0)},
      {2, 1, football::math::Vector3(30, 0, 0)},
      {3, 1, football::math::Vector3(20, 0, 0)},
      {4, 0, football::math::Vector3(10, 0, 0)},
  };
  // Defending team 1 defends +x, so the one-but-deepest defender is at x=30.
  CHECK(football::sim::rules::GetOffsideLine(
            players, football::math::Vector3(0, 0, 0), pitch, 1, 1) ==
        Catch::Approx(30.0f));
  // The ball itself becomes the line when it is further forward.
  CHECK(football::sim::rules::GetOffsideLine(
            players, football::math::Vector3(45, 0, 0), pitch, 1, 1) ==
        Catch::Approx(45.0f));
  // A single defender leaves the legacy origin line, then clamps at halfway.
  const std::vector<football::sim::rules::RefereePlayerFact> one = {
      {1, 1, football::math::Vector3(-10, 0, 0)},
  };
  CHECK(football::sim::rules::GetOffsideLine(
            one, football::math::Vector3(-20, 0, 0), pitch, 1, 1) ==
        Catch::Approx(0.0f));
}

namespace {

RefereeTeamFact TeamFact(int team_id, int defending_direction, int execution_direction) {
  RefereeTeamFact team;
  team.team_id = team_id;
  team.side = team_id == 0 ? TeamSide::Home : TeamSide::Away;
  team.defending_direction = defending_direction;
  team.execution_direction = execution_direction;
  team.fading_possession_amount = 1.0f;
  return team;
}

struct OffsideScene {
  RefereeTeamFact home = TeamFact(0, -1, -1);
  RefereeTeamFact away = TeamFact(1, 1, 1);
  // Home attacks +x. Away defends +x, so its one-but-deepest defender at x=30
  // sets the offside line and the home attacker at x=40 is beyond it.
  std::vector<RefereePlayerFact> players = {
      {41, 0, football::math::Vector3(10, 0, 0)},
      {40, 0, football::math::Vector3(40, 0, 0)},
      {11, 1, football::math::Vector3(40, 0, 0)},
      {12, 1, football::math::Vector3(30, 0, 0)},
      {13, 1, football::math::Vector3(20, 0, 0)},
  };
};

BallTouchFacts MakeTouch(const OffsideScene& scene,
                         football::player::PlayerId toucher, int touch_team_id) {
  BallTouchFacts facts;
  facts.now = Tick{100};
  facts.touch_player = toucher;
  facts.touch_team_id = touch_team_id;
  for (const RefereePlayerFact& player : scene.players) {
    if (player.id == toucher) facts.touch_position = player.position;
  }
  facts.touch_team = touch_team_id == 0 ? scene.home : scene.away;
  facts.defending_team = touch_team_id == 0 ? scene.away : scene.home;
  facts.in_play = true;
  facts.in_set_piece = false;
  facts.offsides_enabled = true;
  facts.ball = {football::math::Vector3(0, 0, 0), football::math::Vector3(0, 0, 0)};
  static const Pitch pitch;
  facts.pitch = &pitch;
  facts.all_active_players = scene.players;
  facts.stadium_to_home = PitchFrameTransform{false};
  return facts;
}

const AwardRestartRuling* FirstRestart(const std::vector<RefereeRuling>& rulings) {
  for (const RefereeRuling& ruling : rulings) {
    if (const auto* award = std::get_if<AwardRestartRuling>(&ruling)) return award;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("an offside reference is kept until the flagged player plays it",
          "[unit][referee]") {
  const OffsideScene scene;
  Referee rules;
  Collect sink;
  // An on-side teammate touches: the attacker at x=40 is flagged, no verdict.
  rules.ConsumeBallTouch(MakeTouch(scene, 41, 0), sink);
  REQUIRE(sink.rulings.empty());
  // The flagged attacker now plays the ball: offside free kick to the defenders.
  rules.ConsumeBallTouch(MakeTouch(scene, 40, 0), sink);
  const AwardRestartRuling* award = FirstRestart(sink.rulings);
  REQUIRE(award != nullptr);
  REQUIRE(award->kind == e_GameMode_FreeKick);
  REQUIRE(award->team == TeamSide::Away);
  REQUIRE(award->cause.kind == RestartCauseKind::Offside);
  REQUIRE(award->cause.offender == 40);
}

TEST_CASE("a reviewed opponent deflection keeps an earlier offside reference",
          "[unit][referee]") {
  const OffsideScene scene;
  Referee rules;
  Collect sink;
  rules.ConsumeBallTouch(MakeTouch(scene, 41, 0), sink);  // flag 40
  BallTouchFacts deflection = MakeTouch(scene, 11, 1);
  deflection.preserve_opponent_offside = true;
  rules.ConsumeBallTouch(deflection, sink);
  REQUIRE(FirstRestart(sink.rulings) == nullptr);
  // The still-flagged attacker plays it.
  rules.ConsumeBallTouch(MakeTouch(scene, 40, 0), sink);
  REQUIRE(FirstRestart(sink.rulings) != nullptr);
}

TEST_CASE("the offside line follows a change of ends", "[unit][referee]") {
  const Pitch pitch;
  const std::vector<RefereePlayerFact> players = {
      {1, 1, football::math::Vector3(-40, 0, 0)},
      {2, 1, football::math::Vector3(-30, 0, 0)},
      {3, 1, football::math::Vector3(-20, 0, 0)},
  };
  // Defending team 1 now defends -x; the one-but-deepest is at x=-30.
  CHECK(football::sim::rules::GetOffsideLine(
            players, football::math::Vector3(0, 0, 0), pitch, 1, -1) ==
        Catch::Approx(-30.0f));
}

TEST_CASE("a legal restart release returns the ball to play", "[unit][referee]") {
  Referee rules;
  BallTouchFacts facts;
  facts.offsides_enabled = false;
  facts.release.eligible = true;
  facts.release.kind = e_GameMode_FreeKick;
  facts.release.action_type = static_cast<int>(e_FunctionType_ShortPass);
  facts.release.scheduled_contact = true;
  facts.release.ball_speed = 5.0f;
  {
    Collect sink;
    rules.ConsumeBallTouch(facts, sink);
    REQUIRE(sink.rulings.size() == 1);
    REQUIRE(std::holds_alternative<BallInPlayRuling>(sink.rulings.front()));
  }
  {
    // A still ball is not a release.
    Collect sink;
    BallTouchFacts still = facts;
    still.release.ball_speed = 0.0f;
    rules.ConsumeBallTouch(still, sink);
    REQUIRE(sink.rulings.empty());
  }
  {
    // A throw-in released with BallControl is not a legal throw.
    Collect sink;
    BallTouchFacts thrown = facts;
    thrown.release.kind = e_GameMode_ThrowIn;
    thrown.release.action_type = static_cast<int>(e_FunctionType_BallControl);
    thrown.release.intentional_kick = false;
    rules.ConsumeBallTouch(thrown, sink);
    REQUIRE(sink.rulings.empty());
  }
  {
    // A non-eligible touch cannot release.
    Collect sink;
    BallTouchFacts ineligible = facts;
    ineligible.release.eligible = false;
    rules.ConsumeBallTouch(ineligible, sink);
    REQUIRE(sink.rulings.empty());
  }
}
