#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "sim/animation/types.hpp"
#include "sim/event/event_recognizer.hpp"

namespace {

using football::sim::Seconds;
using football::sim::Tick;

namespace event = football::sim::event;

event::AcceptedTouch Touch(Tick tick, football::model::PlayerId player,
                         football::model::TeamSide team, int action) {
  event::AcceptedTouch fact;
  fact.touched_at = tick;
  fact.player = player;
  fact.team = team;
  fact.type = e_TouchType_Intentional_Kicked;
  fact.action_type = action;
  return fact;
}

const event::EventView kLive{true, false};

}  // namespace

TEST_CASE("a teammate reception completes a recognized pass",
          "[sim][event][recognizer]") {
  event::EventRecognizer recognizer;
  recognizer.Consume(
      Touch(Tick{100}, 7, football::model::TeamSide::Home, e_FunctionType_ShortPass), kLive);
  REQUIRE(recognizer.HasActivePass());
  REQUIRE(recognizer.transitions().size() == 1);
  REQUIRE(std::holds_alternative<event::PassStarted>(recognizer.transitions().front()));

  recognizer.Consume(
      Touch(Tick{120}, 9, football::model::TeamSide::Home, e_FunctionType_BallControl), kLive);
  REQUIRE_FALSE(recognizer.HasActivePass());
  const auto* ended = std::get_if<event::PassEnded>(&recognizer.transitions().front());
  REQUIRE(ended != nullptr);
  REQUIRE(ended->status == event::EventStatus::Completed);
  REQUIRE(ended->receiver == 9);
}

TEST_CASE("an opponent touch fails the recognized pass", "[sim][event][recognizer]") {
  event::EventRecognizer recognizer;
  recognizer.Consume(
      Touch(Tick{100}, 7, football::model::TeamSide::Home, e_FunctionType_LongPass), kLive);
  recognizer.Consume(
      Touch(Tick{120}, 3, football::model::TeamSide::Away, e_FunctionType_BallControl), kLive);
  const auto* ended = std::get_if<event::PassEnded>(&recognizer.transitions().front());
  REQUIRE(ended != nullptr);
  REQUIRE(ended->status == event::EventStatus::Failed);
}

TEST_CASE("an unresolved behavior times out and a stopped play cancels it",
          "[sim][event][recognizer]") {
  event::EventRecognizer recognizer;
  recognizer.Consume(
      Touch(Tick{100}, 7, football::model::TeamSide::Home, e_FunctionType_ShortPass), kLive);
  recognizer.Advance(Tick{100} + Seconds(5), kLive);
  REQUIRE_FALSE(recognizer.HasActivePass());
  REQUIRE(std::get<event::PassEnded>(recognizer.transitions().front()).status ==
          event::EventStatus::Failed);

  recognizer.Consume(
      Touch(Tick{200}, 7, football::model::TeamSide::Home, e_FunctionType_ShortPass), kLive);
  recognizer.Advance(Tick{210}, event::EventView{false, false});
  REQUIRE(std::get<event::PassEnded>(recognizer.transitions().front()).status ==
          event::EventStatus::Cancelled);
}

TEST_CASE("a confirmed goal completes the pending shot", "[sim][event][recognizer]") {
  event::EventRecognizer recognizer;
  const auto home = football::model::TeamSide::Home;
  recognizer.Consume(Touch(Tick{300}, 11, home, e_FunctionType_Shot), kLive);
  REQUIRE(recognizer.HasActiveShot());
  REQUIRE(std::holds_alternative<event::ShotStarted>(recognizer.transitions().front()));

  recognizer.OnGoalConfirmed(Tick{340}, home);
  REQUIRE_FALSE(recognizer.HasActiveShot());
  const auto* ended = std::get_if<event::ShotEnded>(&recognizer.transitions().front());
  REQUIRE(ended != nullptr);
  REQUIRE(ended->status == event::EventStatus::Completed);
  REQUIRE(ended->goal);
}

TEST_CASE("recognition is suppressed during set pieces", "[sim][event][recognizer]") {
  event::EventRecognizer recognizer;
  recognizer.Consume(
      Touch(Tick{100}, 7, football::model::TeamSide::Home, e_FunctionType_ShortPass),
      event::EventView{true, true});
  REQUIRE_FALSE(recognizer.HasActivePass());
  REQUIRE(recognizer.transitions().empty());
}
