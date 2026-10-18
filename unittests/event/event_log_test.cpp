#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <variant>

#include "event/event_log.hpp"
#include "football/event/match_event.hpp"

using football::sim::event::CardShownEvent;
using football::sim::event::EventLog;
using football::sim::event::GoalScoredEvent;
using football::sim::event::MatchEventCursor;
using football::sim::event::PeriodEndedEvent;

TEST_CASE("EventLog assigns monotonic match-scoped sequences", "[unit][event]") {
  EventLog log;
  REQUIRE(log.empty());
  log.Record(GoalScoredEvent{});
  log.Record(CardShownEvent{});
  REQUIRE(log.size() == 2);
  REQUIRE(log.events()[0].sequence == 1);
  REQUIRE(log.events()[1].sequence == 2);
  REQUIRE(std::holds_alternative<GoalScoredEvent>(log.events()[0].event));
  REQUIRE(std::holds_alternative<CardShownEvent>(log.events()[1].event));
}

TEST_CASE("EventLog EventsSince reads incrementally in order", "[unit][event]") {
  EventLog log;
  MatchEventCursor cursor;
  REQUIRE(log.EventsSince(cursor).empty());

  log.Record(GoalScoredEvent{});
  log.Record(CardShownEvent{});
  const auto first = log.EventsSince(cursor);
  REQUIRE(first.size() == 2);
  REQUIRE(first[0].sequence == 1);
  REQUIRE(first[1].sequence == 2);
  // Nothing new: empty result, cursor unchanged.
  REQUIRE(log.EventsSince(cursor).empty());

  log.Record(PeriodEndedEvent{});
  const auto second = log.EventsSince(cursor);
  REQUIRE(second.size() == 1);
  REQUIRE(second[0].sequence == 3);
  REQUIRE(std::holds_alternative<PeriodEndedEvent>(second[0].event));
  REQUIRE(log.EventsSince(cursor).empty());
}

TEST_CASE("EventLog Clear starts a generation and rebases a stale cursor",
          "[unit][event]") {
  EventLog log;
  MatchEventCursor cursor;
  log.Record(GoalScoredEvent{});
  REQUIRE(log.EventsSince(cursor).size() == 1);
  const std::uint64_t generation = log.generation();

  log.Clear();
  REQUIRE(log.generation() != generation);
  REQUIRE(log.empty());
  log.Record(CardShownEvent{});

  // A cursor from the previous match must read the new match from its first
  // event, never skip a sequence or reinterpret the old one.
  const auto rebased = log.EventsSince(cursor);
  REQUIRE(rebased.size() == 1);
  REQUIRE(rebased[0].sequence == 1);
  REQUIRE(std::holds_alternative<CardShownEvent>(rebased[0].event));
}
