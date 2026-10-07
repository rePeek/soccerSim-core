// Read-only native restart diagnostics; not a runtime clock, scheduler or policy.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/match.hpp"
#include "sim/simulation.hpp"

namespace {
using football::sim::Tick;
using football::sim::TickSpan;

std::uint64_t Parse(std::string_view text) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    throw std::invalid_argument("expected an unsigned integer");
  return value;
}

struct Event {
  Tick entered;
  std::optional<Tick> authorized;
  std::optional<Tick> contact;
  int half;
  e_GameMode mode;
  bool ceremony;
  bool timeout = false;
};
struct Samples {
  unsigned count = 0, censored = 0, timeouts = 0;
  std::vector<std::uint64_t> preparation, ready, total;
};
// Definition-precise behaviour totals, one row per played half. Contact counts
// are accepted intentional kicks, not completed passes or official shots on target.
struct HalfMetrics {
  std::array<double, 2> open_metres{};
  std::array<unsigned, 2> touches{}, shot_contacts{}, pass_contacts{}, goals{};
};
using Key = std::pair<int, int>; // Half and native game-mode value.

void PrintDistribution(const char* name, std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  double sum = 0;
  for (const auto value : values) sum += static_cast<double>(value);
  std::cout << ',' << name << ',' << values.size();
  if (values.empty()) { std::cout << ",NA,NA,NA,NA"; return; }
  // Lower empirical order statistic, not interpolated quantiles; all units ticks.
  std::cout << ',' << sum / values.size() << ',' << values[(values.size() - 1) / 2]
            << ',' << values[(values.size() - 1) * 9 / 10] << ',' << values.back();
}

void Run(unsigned seed, TickSpan half_duration, bool reverse, bool symmetric) {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  if (symmetric) {
    // Equal descriptions, not just equal difficulty: preserve only distinct IDs.
    const auto away_ids = away.players;
    if (home.players.size() != away_ids.size())
      throw std::logic_error("symmetric diagnostic needs equal roster sizes");
    away = home;
    away.name = "Symmetric away fixture";
    for (std::size_t i = 0; i < away.players.size(); ++i) away.players[i].id = away_ids[i].id;
  }
  const auto pitch = football::model::MakeLegacyPitch();
  MatchOptions options;
  options.game_engine_random_seed = seed;
  options.half_duration = half_duration;
  options.reverse_team_processing = reverse;
  if (symmetric) {
    options.left_team_difficulty = options.right_team_difficulty = 1.0f;
    std::cout << "setup," << seed << ',' << reverse << ",symmetric_home_copy,1,1\n";
  }
  Simulation simulation;
  simulation.Init(home, away, pitch, options);
  const football::ai::DefaultAI policy(home, away, pitch);
  std::optional<Event> current;
  std::map<Key, Samples> ordinary, ceremonies;
  std::array<TickSpan, 2> regulation{}, effective{}, event_dead_time{};
  std::array<HalfMetrics, 2> metrics{};
  const auto roster = [&](int side) -> const std::vector<Player*>& {
    return simulation.match()->GetTeam(side)->GetAllPlayers();
  };
  const auto roster_size = roster(0).size() + roster(1).size();
  std::vector<Tick> previous_touch(roster_size);
  std::uint64_t calls = 0;

  const auto record = [&](const Event& event, Tick end) {
    auto& sample = (event.ceremony ? ceremonies : ordinary)[{event.half, int(event.mode)}];
    ++sample.count;
    sample.timeouts += event.timeout;
    if (!event.ceremony) event_dead_time[event.half - 1] += end - event.entered;
    if (!event.contact) ++sample.censored;
    else {
      if (!event.authorized) throw std::logic_error("contact without authorization");
      sample.preparation.push_back((*event.authorized - event.entered).value);
      sample.ready.push_back((*event.contact - *event.authorized).value);
      sample.total.push_back((*event.contact - event.entered).value);
    }
    std::cout << "restart," << seed << ',' << reverse << ',' << event.half << ','
              << event.ceremony << ',' << int(event.mode) << ',' << event.entered.value << ',';
    if (event.authorized) std::cout << event.authorized->value; else std::cout << "NA";
    std::cout << ',';
    if (event.contact) std::cout << event.contact->value; else std::cout << "NA";
    std::cout << ',' << end.value << ',' << event.timeout << ',' << !event.contact << '\n';
  };

  while (!simulation.Finished()) {
    const auto before = simulation.Observe();
    PlayerControlSet controls;
    policy.Update(before, controls);
    simulation.Step(controls);
    ++calls;
    const auto after = simulation.Observe();
    const int half = after.phase == MatchPhase::SecondHalf || after.phase == MatchPhase::Finished ? 2 : 1;
    const auto admitted = after.regulation_time - before.regulation_time;
    const auto live = after.ball_in_play_time - before.ball_in_play_time;
    if (live > admitted || admitted > TickSpan{1})
      throw std::logic_error("clock increment contract violated");
    regulation[half - 1] += admitted;
    effective[half - 1] += live;
    auto& metrics_half = metrics[half - 1];
    std::size_t index = 0;
    for (int side = 0; side < 2; ++side) {
      for (auto* player : roster(side)) {
        const auto tick = player->GetLastTouchTick();
        if (tick != previous_touch[index]) {
          previous_touch[index] = tick;
          // Live play only: restart/kickoff contacts are not open-play events.
          if (before.ball_in_play && player->GetLastTouchType() == e_TouchType_Intentional_Kicked) {
            ++metrics_half.touches[side];
            const auto action = player->GetCurrentFunctionType();
            if (action == e_FunctionType_Shot) ++metrics_half.shot_contacts[side];
            if (action == e_FunctionType_ShortPass || action == e_FunctionType_LongPass ||
                action == e_FunctionType_HighPass) ++metrics_half.pass_contacts[side];
          }
        }
        ++index;
      }
    }
    if (before.ball_in_play)
      for (std::size_t i = 0; i < before.players.size(); ++i)
        metrics_half.open_metres[static_cast<unsigned>(before.players[i].side)] +=
            (after.players[i].position - before.players[i].position).GetLength();
    for (int side = 0; side < 2; ++side) {
      const auto delta = after.teams[side].score - before.teams[side].score;
      if (delta > 0) metrics_half.goals[side] += static_cast<unsigned>(delta);
    }
    const auto& buffer = simulation.match()->GetReferee()->GetBuffer();
    const bool active = buffer.active && buffer.restart.has_value();
    if (current && (!active || current->entered != buffer.restart->entered_tick)) {
      if (!current->contact) record(*current, Tick{before.tick}); // Period-censored, never a completed wait.
      current.reset();
    }
    if (!active) continue;
    const auto& restart = *buffer.restart;
    if (!current)
      current = Event{restart.entered_tick, {}, {}, half, buffer.desiredSetPiece,
                      !before.half_underway && buffer.desiredSetPiece == e_GameMode_KickOff};
    current->timeout |= restart.used_timeout_placement;
    if (restart.phase == RestartPhase::Ready || restart.phase == RestartPhase::Taken)
      current->authorized = buffer.start_tick;
    if (restart.phase == RestartPhase::Taken && !current->contact) {
      if (!buffer.taker) throw std::logic_error("taken restart without taker");
      current->contact = buffer.taker->GetLastTouchTick();
      record(*current, *current->contact);
    }
  }
  const auto world = simulation.Observe();
  const auto result = simulation.Result();
  if (current && !current->contact) record(*current, Tick{world.tick});
  for (int half = 0; half < 2; ++half)
    if (event_dead_time[half] != regulation[half] - effective[half])
      throw std::logic_error("ordinary event waits disagree with dead-ball clock");
  if (regulation[0] != half_duration || regulation[1] != half_duration ||
      regulation[0] + regulation[1] != world.regulation_time ||
      effective[0] + effective[1] != world.ball_in_play_time ||
      calls != result.duration_ticks || world.tick + 1 != calls)
    throw std::logic_error("final clock/call totals disagree");
  for (int half = 0; half < 2; ++half)
    for (int side = 0; side < 2; ++side)
      if (metrics[half].shot_contacts[side] + metrics[half].pass_contacts[side] >
          metrics[half].touches[side])
        throw std::logic_error("classified contacts exceed accepted kicked touches");
  for (int side = 0; side < 2; ++side) {
    const unsigned total = metrics[0].goals[side] + metrics[1].goals[side];
    const int expected = side == 0 ? result.home_score : result.away_score;
    if (total != static_cast<unsigned>(expected))
      throw std::logic_error("half goal totals disagree with the final score");
  }
  std::cout << std::fixed;
  for (int half = 1; half <= 2; ++half) {
    const auto& value = metrics[half - 1];
    std::cout << "metrics," << seed << ',' << reverse << ',' << half << ',';
    std::cout << std::setprecision(1) << value.open_metres[0] << ',' << value.open_metres[1];
    for (int side = 0; side < 2; ++side)
      std::cout << ',' << value.touches[side] << ',' << value.shot_contacts[side] << ','
                << value.pass_contacts[side];
    for (int side = 0; side < 2; ++side) std::cout << ',' << value.goals[side];
    std::cout << '\n';
  }
  std::cout << std::defaultfloat;
  std::cout << "match," << seed << ',' << reverse << ',' << result.home_score << ','
            << result.away_score << ',' << result.duration_ticks << ',' << world.tick << ','
            << world.regulation_time.value << ',' << world.ball_in_play_time.value << '\n';
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc > 5) throw std::invalid_argument("usage: football_restart_metrics [seed [half_ticks [reverse_0_or_1 [symmetric_0_or_1]]]]");
    const auto seed = argc > 1 ? Parse(argv[1]) : 42;
    if (seed > UINT32_MAX) throw std::invalid_argument("seed exceeds uint32");
    const auto half = argc > 2 ? TickSpan{Parse(argv[2])} : football::sim::Minutes(45);
    const auto reverse = argc > 3 ? Parse(argv[3]) : 0;
    if (reverse > 1) throw std::invalid_argument("reverse must be 0 or 1");
    const auto symmetric = argc > 4 ? Parse(argv[4]) : 0;
    if (symmetric > 1) throw std::invalid_argument("symmetric must be 0 or 1");
    Run(static_cast<unsigned>(seed), half, reverse != 0, symmetric != 0);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_restart_metrics: " << error.what() << '\n';
    return 1;
  }
}
