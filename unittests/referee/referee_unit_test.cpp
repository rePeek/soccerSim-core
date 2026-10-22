// RFE-7B: the Referee rule surface consumes stable value facts and emits only
// RulingSink verdicts. No Player object, Simulation or RuleCommandSink is
// involved, so the foul verdicts are tested at module level.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

#include "football/pitch/pitch.hpp"
#include "football/animation/types.hpp"
#include "referee/ball_rules.hpp"
#include "referee/goal.hpp"
#include "referee/foul_sanction.hpp"
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
  rules.CheckFoul(now, Pitch{}, live, sink);
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

RefereeTeamFact TeamFact(int team_id, int defending_direction) {
  RefereeTeamFact team;
  team.team_id = team_id;
  team.side = team_id == 0 ? TeamSide::Home : TeamSide::Away;
  team.defending_direction = defending_direction;
  team.fading_possession_amount = 1.0f;
  return team;
}

struct OffsideScene {
  RefereeTeamFact home = TeamFact(0, -1);
  RefereeTeamFact away = TeamFact(1, 1);
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

TEST_CASE("a deflected or saved ball keeps an earlier offside position",
          "[unit][referee]") {
  for (const auto kind :
       {football::sim::rules::BallPlayKind::Deflection,
        football::sim::rules::BallPlayKind::DeliberateSave}) {
    CAPTURE(static_cast<int>(kind));
    const OffsideScene scene;
    Referee rules;
    Collect sink;
    rules.ConsumeBallTouch(MakeTouch(scene, 41, 0), sink);  // flag 40
    BallTouchFacts deflection = MakeTouch(scene, 11, 1);
    deflection.play_kind = kind;
    rules.ConsumeBallTouch(deflection, sink);
    REQUIRE(FirstRestart(sink.rulings) == nullptr);
    // The still-flagged attacker plays it.
    rules.ConsumeBallTouch(MakeTouch(scene, 40, 0), sink);
    REQUIRE(FirstRestart(sink.rulings) != nullptr);
  }
}

TEST_CASE("a defender's deliberate play resets the offside position",
          "[unit][referee]") {
  const OffsideScene scene;
  Referee rules;
  Collect sink;
  rules.ConsumeBallTouch(MakeTouch(scene, 41, 0), sink);  // flag 40
  // A defender deliberately plays the ball: the reference is reset.
  BallTouchFacts deliberate = MakeTouch(scene, 11, 1);
  deliberate.play_kind = football::sim::rules::BallPlayKind::DeliberatePlay;
  rules.ConsumeBallTouch(deliberate, sink);
  rules.ConsumeBallTouch(MakeTouch(scene, 40, 0), sink);
  CHECK(FirstRestart(sink.rulings) == nullptr);
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

TEST_CASE("the offside line needs two defenders and never falls behind halfway",
          "[unit][referee]") {
  const Pitch pitch;
  using football::math::Vector3;
  const auto line = [&](std::vector<RefereePlayerFact> players, Vector3 ball, int side) {
    return football::sim::rules::GetOffsideLine(players, ball, pitch, 1, side);
  };

  // No defenders: the halfway line, raised to the ball when it is further on.
  CHECK(line({}, Vector3(-5, 0, 0), 1) == Catch::Approx(0.0f));
  CHECK(line({}, Vector3(5, 0, 0), 1) == Catch::Approx(5.0f));
  // One defender is not a second-last opponent: the halfway line.
  CHECK(line({{1, 1, Vector3(30, 0, 0)}}, Vector3(0, 0, 0), 1) == Catch::Approx(0.0f));
  // Two defenders: the second-deepest.
  CHECK(line({{1, 1, Vector3(30, 0, 0)}, {2, 1, Vector3(20, 0, 0)}},
             Vector3(0, 0, 0), 1) == Catch::Approx(20.0f));
  // A level pair is a real second-last opponent, not the origin fallback.
  CHECK(line({{1, 1, Vector3(25, 0, 0)}, {2, 1, Vector3(25, 0, 0)}},
             Vector3(0, 0, 0), 1) == Catch::Approx(25.0f));
  // The ball nearer to the goal than the second-deepest defender is the line.
  CHECK(line({{1, 1, Vector3(30, 0, 0)}, {2, 1, Vector3(20, 0, 0)}},
             Vector3(28, 0, 0), 1) == Catch::Approx(28.0f));
  // Change of ends: the same selection along the defending direction.
  CHECK(line({{1, 1, Vector3(-30, 0, 0)}, {2, 1, Vector3(-20, 0, 0)}},
             Vector3(0, 0, 0), -1) == Catch::Approx(-20.0f));
}

TEST_CASE("a boundary is only crossed when the whole ball is past it",
          "[unit][referee]") {
  const Pitch pitch;
  using football::math::Vector3;
  using football::sim::BallOutOfPlay;
  using football::sim::ClassifyBallOutOfPlay;
  const float r = 0.11f;

  // Centre short of the full-ball limit: still in play.
  CHECK(ClassifyBallOutOfPlay(
            pitch, Vector3(pitch.half_length() + r - 0.01f, 0, r), r) ==
        BallOutOfPlay::None);
  // Centre past the goal line by one radius: out over the goal line.
  CHECK(ClassifyBallOutOfPlay(
            pitch,
            Vector3(pitch.half_length() + pitch.line_half_width() + r + 0.01f, 0, r),
            r) == BallOutOfPlay::GoalLine);
  // Same for the touchline.
  CHECK(ClassifyBallOutOfPlay(
            pitch,
            Vector3(0, pitch.half_width() + pitch.line_half_width() + r + 0.01f, r),
            r) == BallOutOfPlay::Touchline);
  // The radius is really used: a bigger ball needs its centre further out.
  const Vector3 near_line(pitch.half_length() + 0.15f, 0, 0.2f);
  CHECK(ClassifyBallOutOfPlay(pitch, near_line, 0.05f) == BallOutOfPlay::GoalLine);
  CHECK(ClassifyBallOutOfPlay(pitch, near_line, 0.20f) == BallOutOfPlay::None);
}

TEST_CASE("a goal needs the whole ball through the goal-mouth rectangle",
          "[unit][referee]") {
  const Pitch pitch;
  using football::math::Vector3;
  using football::sim::CrossedGoalLine;
  const float r = 0.11f;
  const float goal_x = pitch.half_length() + pitch.line_half_width() + r;

  CHECK(CrossedGoalLine(pitch, 1, Vector3(goal_x - 0.05f, 0, r),
                        Vector3(goal_x + 0.05f, 0, r), r));
  // Through the plane but outside the posts: no goal.
  CHECK_FALSE(CrossedGoalLine(pitch, 1, Vector3(goal_x - 0.05f, 3.7f, r),
                              Vector3(goal_x + 0.05f, 3.7f, r), r));
  // The centre clears the post line but the whole ball does not: no goal.
  const float near_post_y = pitch.goal_half_width() - 0.05f;
  CHECK_FALSE(CrossedGoalLine(pitch, 1, Vector3(goal_x - 0.05f, near_post_y, r),
                              Vector3(goal_x + 0.05f, near_post_y, r), r));
  // Through the plane but above the crossbar: no goal.
  CHECK_FALSE(CrossedGoalLine(pitch, 1, Vector3(goal_x - 0.05f, 0, 2.5f),
                              Vector3(goal_x + 0.05f, 0, 2.5f), r));
  // The centre is under the bar but the whole ball is not: no goal.
  const float near_bar_z = pitch.goal_height() - 0.05f;
  CHECK_FALSE(CrossedGoalLine(pitch, 1, Vector3(goal_x - 0.05f, 0, near_bar_z),
                              Vector3(goal_x + 0.05f, 0, near_bar_z), r));
  // Crossing the opposite goal plane is not a goal on side +1.
  CHECK_FALSE(CrossedGoalLine(pitch, 1, Vector3(-goal_x - 0.05f, 0, r),
                              Vector3(-goal_x + 0.05f, 0, r), r));
  // A step parallel to the goal plane never crosses it.
  CHECK_FALSE(CrossedGoalLine(pitch, 1, Vector3(goal_x - 0.05f, 0, r),
                              Vector3(goal_x - 0.05f, 1, r), r));
  // A ball leaving the goal back into the field is not a new goal.
  CHECK_FALSE(CrossedGoalLine(pitch, 1, Vector3(goal_x + 0.05f, 0, r),
                              Vector3(goal_x - 0.05f, 0, r), r));
}

TEST_CASE("a goal kick, corner or throw-in cannot create an offside offence",
          "[unit][referee]") {
  const OffsideScene scene;  // home attacker 40 is in an offside position
  for (const e_GameMode kind :
       {e_GameMode_GoalKick, e_GameMode_Corner, e_GameMode_ThrowIn}) {
    CAPTURE(static_cast<int>(kind));
    Referee rules;
    Collect sink;
    // A teammate plays the ball and freezes the offside reference.
    rules.ConsumeBallTouch(MakeTouch(scene, 41, 0), sink);
    // The flagged attacker then plays the restart directly: no offence.
    BallTouchFacts touch = MakeTouch(scene, 40, 0);
    touch.restart.active = true;
    touch.restart.scheduled = true;
    touch.restart.kind = kind;
    touch.restart.team_id = 0;
    rules.ConsumeBallTouch(touch, sink);
    CHECK(FirstRestart(sink.rulings) == nullptr);
  }
  // Control: a free kick is not exempt, so the flagged attacker is penalised.
  Referee rules;
  Collect sink;
  rules.ConsumeBallTouch(MakeTouch(scene, 41, 0), sink);
  BallTouchFacts free_kick = MakeTouch(scene, 40, 0);
  free_kick.restart.active = true;
  free_kick.restart.scheduled = true;
  free_kick.restart.kind = e_GameMode_FreeKick;
  free_kick.restart.team_id = 0;
  rules.ConsumeBallTouch(free_kick, sink);
  CHECK(FirstRestart(sink.rulings) != nullptr);
}

TEST_CASE("foul severity maps to named sanctions", "[unit][referee]") {
  using football::sim::FoulKind;
  using football::sim::rules::ClassifyFoul;
  using football::sim::rules::FoulSeverity;
  CHECK(ClassifyFoul(FoulKind::SlidingTackle, 0.9f, false).card == 0);
  const auto careless = ClassifyFoul(FoulKind::SlidingTackle, 1.2f, false);
  CHECK(careless.foul);
  CHECK(careless.severity == FoulSeverity::Careless);
  CHECK(careless.card == 1);
  CHECK(ClassifyFoul(FoulKind::SlidingTackle, 1.6f, false).card == 2);
  CHECK(ClassifyFoul(FoulKind::SlidingTackle, 2.2f, false).card == 3);
  // Denying an obvious goal-scoring opportunity is a send-off on its own.
  CHECK(ClassifyFoul(FoulKind::SlidingTackle, 1.2f, true).card == 3);
  CHECK(ClassifyFoul(FoulKind::SlidingTackle, 1.2f, true).dogso);
}

TEST_CASE("a penalty for a genuine attempt to play the ball is only a caution",
          "[unit][referee]") {
  Referee rules;
  // Careless challenge: the red card exists only because of the DOGSO, so the
  // double-jeopardy rule downgrades it to a caution.
  FoulAssessment fact = Sliding(Tick{100}, 1.2f);
  fact.position = football::math::Vector3(47.5f, 0.0f, 0.0f);
  fact.denied_goal_opportunity = true;
  fact.attempted_to_play_ball = true;
  rules.AssessFoul(fact, Tick{100}, false);
  Collect sink;
  Check(rules, Tick{100}, Live(1.2f), sink);
  const AwardRestartRuling* award = FirstRestart(sink.rulings);
  REQUIRE(award != nullptr);
  CHECK(award->kind == e_GameMode_Penalty);
  const CardRuling* card = FirstCard(sink.rulings);
  REQUIRE(card != nullptr);
  CHECK(card->type == 2);
}

TEST_CASE("excessive force keeps its send-off even with a penalty",
          "[unit][referee]") {
  Referee rules;
  // The red card stands on its own severity: the penalty cannot downgrade it.
  FoulAssessment fact = Sliding(Tick{100}, 2.2f);
  fact.position = football::math::Vector3(47.5f, 0.0f, 0.0f);
  fact.denied_goal_opportunity = true;
  fact.attempted_to_play_ball = true;
  rules.AssessFoul(fact, Tick{100}, false);
  Collect sink;
  Check(rules, Tick{100}, Live(1.2f), sink);
  const AwardRestartRuling* award = FirstRestart(sink.rulings);
  REQUIRE(award != nullptr);
  CHECK(award->kind == e_GameMode_Penalty);
  const CardRuling* card = FirstCard(sink.rulings);
  REQUIRE(card != nullptr);
  CHECK(card->type == 3);
}

TEST_CASE("a DOGSO outside the penalty area is a send-off", "[unit][referee]") {
  Referee rules;
  FoulAssessment fact = Sliding(Tick{100}, 2.2f);
  fact.position = football::math::Vector3(0.0f, 0.0f, 0.0f);
  fact.denied_goal_opportunity = true;
  rules.AssessFoul(fact, Tick{100}, false);
  Collect sink;
  Check(rules, Tick{100}, Live(1.2f), sink);
  const CardRuling* card = FirstCard(sink.rulings);
  REQUIRE(card != nullptr);
  CHECK(card->type == 3);
}

TEST_CASE("the out-of-play restart uses the exact boundary crossing point",
          "[unit][referee]") {
  const Pitch pitch;
  using football::math::Vector3;
  using football::sim::BallOutOfPlay;
  using football::sim::BoundaryCrossingPoint;
  const float r = 0.11f;
  const float limit_y = pitch.half_width() + pitch.line_half_width() + r;
  // The ball crosses the touchline halfway through the step; the restart spot
  // is the crossing (x=22.5), not the tick-end centre (x=25).
  const auto crossing = BoundaryCrossingPoint(
      pitch, Vector3(20.0f, limit_y - 0.05f, r), Vector3(25.0f, limit_y + 0.05f, r),
      r, BallOutOfPlay::Touchline);
  REQUIRE(crossing.has_value());
  CHECK(crossing->coords[0] == Catch::Approx(22.5f).margin(0.05f));
  CHECK(crossing->coords[1] == Catch::Approx(limit_y).margin(0.01f));
  // A step that never reaches the boundary has no crossing point.
  CHECK_FALSE(BoundaryCrossingPoint(pitch, Vector3(0, 0, r), Vector3(1, 1, r), r,
                                    BallOutOfPlay::Touchline)
                  .has_value());
}

TEST_CASE("a diagonal exit is classified by the line the ball crossed first",
          "[unit][referee]") {
  const Pitch pitch;
  using football::math::Vector3;
  using football::sim::BallOutOfPlay;
  using football::sim::DetectBallBoundaryCrossing;
  const float r = 0.11f;
  const float lx = pitch.half_length() + pitch.line_half_width() + r;
  const float ly = pitch.half_width() + pitch.line_half_width() + r;

  // The ball leaves over the touchline first and only later crosses the goal
  // line: the restart is a throw-in, not a goal line decision.
  const auto touchline_first = DetectBallBoundaryCrossing(
      pitch, Vector3(lx - 2.0f, ly - 0.5f, r), Vector3(lx + 0.5f, ly + 1.5f, r), r);
  CHECK(touchline_first.boundary == BallOutOfPlay::Touchline);
  CHECK(touchline_first.fraction < 0.5f);
  CHECK(touchline_first.position.coords[1] == Catch::Approx(ly).margin(0.01f));

  // The same tick but the goal line is crossed first: the corner/goal-kick
  // decision wins instead.
  const auto goal_first = DetectBallBoundaryCrossing(
      pitch, Vector3(lx - 0.5f, ly - 2.0f, r), Vector3(lx + 1.5f, ly + 0.5f, r), r);
  CHECK(goal_first.boundary == BallOutOfPlay::GoalLine);
  CHECK(goal_first.fraction < 0.5f);
  CHECK(goal_first.position.coords[0] == Catch::Approx(lx).margin(0.01f));
}

TEST_CASE("a free kick release captures the offside position",
          "[unit][referee]") {
  const OffsideScene scene;
  Referee rules;
  Collect sink;
  // The taker plays the free kick while the set-piece administration is still
  // active; this touch must freeze the offside position.
  BallTouchFacts release = MakeTouch(scene, 41, 0);
  release.in_set_piece = true;
  release.release.eligible = true;
  release.release.kind = e_GameMode_FreeKick;
  release.release.action_type = static_cast<int>(e_FunctionType_ShortPass);
  release.release.scheduled_contact = true;
  release.release.ball_speed = 5.0f;
  release.restart.active = true;
  release.restart.scheduled = true;
  release.restart.kind = e_GameMode_FreeKick;
  release.restart.team_id = 0;
  rules.ConsumeBallTouch(release, sink);
  REQUIRE(sink.rulings.size() == 1);
  REQUIRE(std::holds_alternative<BallInPlayRuling>(sink.rulings.front()));
  // The attacker flagged at the release now plays it: offside.
  rules.ConsumeBallTouch(MakeTouch(scene, 40, 0), sink);
  const AwardRestartRuling* award = FirstRestart(sink.rulings);
  REQUIRE(award != nullptr);
  CHECK(award->cause.kind == RestartCauseKind::Offside);
}
