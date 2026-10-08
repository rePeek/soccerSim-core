#ifndef FOOTBALL_TEST_RESTART_PLACEMENT_FIXTURE_HPP
#define FOOTBALL_TEST_RESTART_PLACEMENT_FIXTURE_HPP

#include <array>
#include <cstdint>
#include <sstream>

#include "app/fixtures/default_teams.hpp"
#include "sim/match/match.hpp"
#include "sim/rules/restart_placement.hpp"
#include "sim/simulation.hpp"
#include "sim/team/team.hpp"

namespace football::test {
struct RestartCase {
  int roster;
  bool reverse;
  int taking_team;
  e_GameMode mode;
};

inline void SetupRestart(Simulation &simulation, const RestartCase &test) {
  auto home = app::fixtures::MakeDefaultHomeTeam();
  auto away = app::fixtures::MakeDefaultAwayTeam();
  if (test.roster != 0) {
    const int home_count = test.roster == 1 ? 3 : 1;
    const int away_count = test.roster == 1 ? 2 : 1;
    home.players.resize(home_count); home.formation.resize(home_count);
    away.players.resize(away_count); away.formation.resize(away_count);
  }
  MatchOptions options;
  options.reverse_team_processing = test.reverse;
  simulation.Stop();
  simulation.Init(home, away, model::MakeLegacyPitch(), options);
  Match *match = simulation.match();
  simulation.Mirror(test.reverse, !test.reverse, test.reverse);
  blunted::Vector3 focus(0);
  switch (test.mode) {
    case e_GameMode_Corner: focus = blunted::Vector3(55, 36, 0); break;
    case e_GameMode_GoalKick: focus = blunted::Vector3(-50, 0, 0); break;
    case e_GameMode_ThrowIn: focus = blunted::Vector3(10, 36, 0); break;
    case e_GameMode_FreeKick: focus = blunted::Vector3(25, 6, 0); break;
    case e_GameMode_Penalty: focus = blunted::Vector3(44, 0, 0); break;
    default: break;
  }
  simulation.ResetSituation(test.reverse ? -focus : focus);
}

inline std::array<Player *, 2> PositionRestart(Match &match, const RestartCase &test) {
  struct RetainSink final : football::sim::rules::RuleCommandSink {
    Match& match;
    explicit RetainSink(Match& m) : match(m) {}
    void StopPlay() override {}
    void StartPlay() override {}
    void StartSetPiece() override {}
    void StopSetPiece() override {}
    void StartBallInPlay() override {}
    void SetBallRetainer(Player* retainer) override { match.SetBallRetainer(retainer); }
    void ResetSituation(const blunted::Vector3&) override {}
    void ResetBall(const blunted::Vector3&) override {}
    void SetPhase(MatchPhase) override {}
  } commands(match);
  std::array<Player *, 2> takers{};
  for (int side : {match.FirstTeam(), match.SecondTeam()})
    takers[side] = PositionRestartPlayers(match.GetTeam(side), test.mode,
        match.GetTeam(1 - side), test.taking_team, test.taking_team,
        *match.GetBall(), match.GetRegulationTime(), match.options(), match.rng(), commands);
  return takers;
}

// Field-wise exact float/action/RNG fingerprint, never struct padding/pointers.
inline std::uint64_t RestartFingerprint(std::uint64_t hash, Match &match,
                                       std::array<Player *, 2> takers) {
  const auto bytes = [&](const void *source, std::size_t size) {
    const auto *p = static_cast<const unsigned char *>(source);
    for (std::size_t i = 0; i < size; ++i) { hash ^= p[i]; hash *= UINT64_C(1099511628211); }
  };
  const auto value = [&](const auto &v) { bytes(&v, sizeof(v)); };
  const auto vector = [&](const blunted::Vector3 &v) { bytes(v.coords, sizeof(v.coords)); };
  vector(match.GetBall()->Predict(0));
  for (int side = 0; side < 2; ++side) {
    const auto taker = takers[side] ? takers[side]->GetID() : model::kInvalidPlayerId;
    value(taker);
    for (Player *player : match.GetTeam(side)->GetAllPlayers()) {
      vector(player->GetPosition()); vector(player->GetMovement());
      vector(player->GetDirectionVec()); vector(player->GetBodyDirectionVec());
      const auto &action = player->GetSimulationActionState();
      value(action.type); value(action.Frame()); value(action.FrameCount());
      value(static_cast<int>(football::sim::ToMilliseconds(action.elapsed)));
      value(static_cast<int>(football::sim::ToMilliseconds(action.duration)));
      value(action.ContactFrame());
      vector(action.contactPosition);
    }
  }
  const auto retainer = match.GetBallRetainer() ? match.GetBallRetainer()->GetID() : model::kInvalidPlayerId;
  value(retainer);
  std::ostringstream rng;
  rng << match.rng().engine();
  const std::string state = rng.str();
  bytes(state.data(), state.size());
  return hash;
}
inline constexpr std::array<e_GameMode, 6> restart_modes{e_GameMode_KickOff,
    e_GameMode_GoalKick, e_GameMode_ThrowIn, e_GameMode_Corner,
    e_GameMode_FreeKick, e_GameMode_Penalty};
}  // namespace football::test

#endif
