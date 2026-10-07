// Read-only native restart diagnostics; not a runtime clock, scheduler or policy.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
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

void Run(unsigned seed, TickSpan half_duration, bool reverse) {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  const auto pitch = football::model::MakeLegacyPitch();
  MatchOptions options;
  options.game_engine_random_seed = seed;
  options.half_duration = half_duration;
  options.reverse_team_processing = reverse;
  Simulation simulation;
  simulation.Init(home, away, pitch, options);
  const football::ai::DefaultAI policy(home, away, pitch);
  std::optional<Event> current;
  std::map<Key, Samples> ordinary, ceremonies;
  std::array<TickSpan, 2> regulation{}, effective{}, event_dead_time{};
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
  for (int half = 1; half <= 2; ++half)
    std::cout << "half," << seed << ',' << reverse << ',' << half << ','
              << regulation[half - 1].value << ',' << effective[half - 1].value << ','
              << (regulation[half - 1] - effective[half - 1]).value << '\n';
  for (bool ceremony : {false, true}) {
    for (const auto& [key, sample] : ceremony ? ceremonies : ordinary) {
      std::cout << "distribution," << seed << ',' << reverse << ',' << key.first << ','
                << ceremony << ',' << key.second << ',' << sample.count << ','
                << sample.censored << ',' << sample.timeouts;
      PrintDistribution("prepare", sample.preparation);
      PrintDistribution("ready", sample.ready);
      PrintDistribution("total", sample.total);
      std::cout << '\n';
    }
  }
  std::cout << "match," << seed << ',' << reverse << ',' << result.home_score << ','
            << result.away_score << ',' << result.duration_ticks << ',' << world.tick << ','
            << world.regulation_time.value << ',' << world.ball_in_play_time.value << '\n';
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc > 4) throw std::invalid_argument("usage: football_restart_metrics [seed [half_ticks [reverse_0_or_1]]]");
    const auto seed = argc > 1 ? Parse(argv[1]) : 42;
    if (seed > UINT32_MAX) throw std::invalid_argument("seed exceeds uint32");
    const auto half = argc > 2 ? TickSpan{Parse(argv[2])} : football::sim::Minutes(45);
    const auto reverse = argc > 3 ? Parse(argv[3]) : 0;
    if (reverse > 1) throw std::invalid_argument("reverse must be 0 or 1");
    Run(static_cast<unsigned>(seed), half, reverse != 0);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_restart_metrics: " << error.what() << '\n';
    return 1;
  }
}
