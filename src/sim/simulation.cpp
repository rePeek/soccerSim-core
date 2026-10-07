#include "sim/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

#include "sim/animation/library.hpp"
#include "sim/ball/ball_player_contact.hpp"



#include "sim/match/match.hpp"
#include "sim/observation/world_state_builder.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player_contact.hpp"
#include "sim/rules/goal.hpp"
#include "sim/team/possession.hpp"

namespace {

std::vector<FormationEntry> ToLegacyFormation(const football::model::Formation& model) {
  std::vector<FormationEntry> result;
  result.reserve(model.size());
  for (const auto& entry : model) {
    result.emplace_back(entry.position.x, entry.position.y, entry.role,
                        entry.lazy, entry.controllable);
  }
  return result;
}

// Resolve only unspecified runtime appearance, never import or invent profiles.
// Preserve the historical home-then-away draw for every declared profile, even
// profiles omitted by a smaller formation and profiles with explicit appearance.
void ResolveAppearance(football::model::Team& team, blunted::Rng& rng) {
  for (auto& player : team.players) {
    const int skin_color = int(std::round(rng.Uniform(1, 4)));
    if (!player.appearance.skin_color) player.appearance.skin_color = skin_color;
  }
}

// Mirrors the retired ScenarioConfig::LeftTeamOwnsBall(): the team whose nearest
// initial formation player stands closer to the kickoff spot starts in possession.
bool LeftTeamOwnsBall(const std::vector<FormationEntry>& left,
                      const std::vector<FormationEntry>& right,
                      const blunted::Vector3& ball_position) {
  float leftDistance = 1000000;
  float rightDistance = 1000000;
  for (const FormationEntry& player : left) {
    leftDistance = std::min(
        leftDistance, (player.start_position - ball_position).GetLength());
  }
  for (const FormationEntry& player : right) {
    rightDistance = std::min(
        rightDistance, (player.start_position - ball_position).GetLength());
  }
  return leftDistance < rightDistance;
}

// Validate the effective descriptions before profile draws, reseeding or
// actor construction. Caller identities are never generated or renumbered.
void ValidatePlayers(const football::model::Team& home, const football::model::Team& away) {
  if (home.players.empty() || away.players.empty()) {
    throw std::invalid_argument("match requires explicit player rosters");
  }
  const auto player_count = [](const football::model::Team& team) -> std::size_t {
    return !team.formation.empty() ? team.formation.size()
        : !team.tactical_formation.empty() ? team.tactical_formation.size()
        : team.players.size();
  };
  const std::size_t home_count = player_count(home);
  const std::size_t away_count = player_count(away);
  if (home_count > home.players.size() || away_count > away.players.size()) {
    throw std::invalid_argument("formation has no corresponding player profile");
  }
  std::set<football::model::PlayerId> identities;
  for (const football::model::Team* team : {&home, &away}) {
    for (const football::model::Player& player : team->players) {
      if (player.id == football::model::kInvalidPlayerId) {
        throw std::invalid_argument("match player requires an explicit PlayerId");
      }
      if (!identities.insert(player.id).second) {
        throw std::invalid_argument("duplicate PlayerId across match rosters");
      }
    }
  }
}

}  // namespace

Simulation::Simulation() {
  // The pre-match profile draws historically ran on an RNG freshly seeded with
  // 0, before the episode seed was applied in Init(). Keep that exact window.
  rng_.Seed(0);
}

Simulation::~Simulation() {
  Stop();
}

void Simulation::Init(
    const football::model::Team& home, const football::model::Team& away,
    const football::model::Pitch& pitch, MatchOptions options) {
  if (match_) throw std::logic_error("simulation already initialized");
  // Native duration and full-match capacity are checked before any RNG draws.
  if (options.half_duration == football::sim::TickSpan{} ||
      options.half_duration.value > std::numeric_limits<std::uint64_t>::max() / 2) {
    throw std::invalid_argument("invalid regulation duration");
  }
  ValidatePlayers(home, away);
  const std::vector<FormationEntry> left = ToLegacyFormation(home.formation);
  const std::vector<FormationEntry> right = ToLegacyFormation(away.formation);
  football::model::Team home_model = home, away_model = away;
  ResolveAppearance(home_model, rng_);
  ResolveAppearance(away_model, rng_);

  // Derive the kickoff rule once; input selection is not simulation state.
  options.left_team_owns_ball =
      LeftTeamOwnsBall(left, right, options.ball_position);

  // Apply the episode seed after the appearance draws, before constructing
  // actors. Moving those draws across this boundary changes simulation RNG.
  rng_.Seed(options.game_engine_random_seed);

  EnsureAnimationLibrary();
  match_ = std::make_unique<Match>(home_model, away_model, pitch, options, rng_,
                                  animations_, mental_images_);
}

void Simulation::EnsureAnimationLibrary() {
  if (animations_) return;
  // Never put resource loading inside assert(): Release must load and reject
  // invalid resources too. Publish only a successfully loaded library.
  auto animations = std::make_shared<AnimationLibrary>();
  if (!animations->Load(GFOOTBALL_BAKED_ANIM_PATH)) {
    throw std::runtime_error("Simulation: cannot load baked animations: "
                             GFOOTBALL_BAKED_ANIM_PATH);
  }
  animations_ = std::move(animations);
}

void Simulation::Step(const PlayerControlSet& controls) {
  if (!match_) throw std::logic_error("simulation has no match");
  Match& match = *match_;
  if (match.Finished()) return;
  if (match.pending_change_of_ends_) {
    match.SwitchEnds();
    match.pending_change_of_ends_ = false;
  }
  match.clock_.CountExecutedStep(match.GetMatchPhase());

  // Frame-local controls are runtime input, not a Match algorithm.
  for (int team_id = 0; team_id < 2; ++team_id) {
    for (Player* player : match.GetTeam(team_id)->GetAllPlayers()) {
      player->ClearControl();
      if (const PlayerControl* control = controls.Get(player->GetID())) {
        player->SetControl(*control);
      }
    }
  }

  const bool reverse = match.options().reverse_team_processing;
  // Ball already shares the first roster's frame; turn only the other roster.
  match.Mirror(reverse, !reverse, false);
  // Period whistles still win over pending contacts, before any RNG draw.
  if (match.IsBallInPlay() && !match.GetReferee()->PeriodElapsed()) {
    std::vector<Player*> players;
    match.GetTeam(match.FirstTeam())->GetActivePlayers(players);
    match.GetTeam(match.SecondTeam())->GetActivePlayers(players);
    const football::sim::BallPlayerContactInputs inputs{
        match.teams, match.FirstTeam(), match.lastTouchTeamID, mental_images_,
        match.GetTimelineTick(), match.last_body_ball_collision_tick_};
    const auto contact = football::sim::ResolveBallPlayerContacts(
        *match.GetBall(), players, inputs);
    if (contact.impulse) {
      match.TouchBall(*contact.impulse);
      // Preserve the three argument-expression RNG draws and refresh-before-spin.
      match.GetBall()->SetRotation(rng_.Uniform(-30, 30), rng_.Uniform(-30, 30),
          rng_.Uniform(-30, 30), contact.rotation_bias, match.GetBallEnvironment());
      match.last_body_ball_collision_tick_ = match.GetTimelineTick();
    }
  }

  // ProcessReferee: before this tick's ball/player movement, in the contact frame.
  match.GetReferee()->Process();
  Vector3 previousBallPos = match.ball->Predict(0);
  match.Mirror(reverse, !reverse, false);
  // Restore the processing frame even on the referee's terminal transition.
  if (match.Finished()) return;
  if (!match.IsInPlay() && !match.GetReferee()->RestartNeedsSimulation() &&
      (match.GetTimelineTick() < match.GetReferee()->GetBuffer().prepare_tick ||
       match.GetReferee()->GetBuffer().prepare_tick + football::sim::TickSpan{1} < match.GetTimelineTick())) {
    // Ceremonies execute only their placement tail; both clocks stay stopped.
    match.AdvanceTime(football::sim::TickSpan{1});
    return;
  }
  // StepBall.
  match.Mirror(false, false, reverse);
  match.ball->Process(match.GetBallEnvironment());
  match.Mirror(false, false, reverse);

  // CaptureHistory: preserve the pre-player-processing capture and sample-zero timing.
  CaptureMentalImage(match);

  // StepPlayers, each in its own execution frame.
  match.Mirror(match.first_team == 1, match.first_team == 0, false);
  match.teams[match.first_team]->Process();
  match.Mirror(true, true, true);
  match.teams[match.second_team]->Process();
  match.Mirror(match.first_team == 0, match.first_team == 1, true);

  // UpdatePossession: retain both per-roster refreshes before arbitration.
  match.Mirror(match.first_team == 1, match.first_team == 0, false);
  match.teams[match.first_team]->UpdatePossessionStats();
  match.Mirror(true, true, true);
  match.teams[match.second_team]->UpdatePossessionStats();
  match.Mirror(match.first_team == 0, match.first_team == 1, true);

  const auto possession = football::sim::EvaluatePossession(
      *match.teams[match.first_team], *match.teams[match.second_team],
      match.designatedPossessionPlayer, match.ballRetainer);
  match.bestPossessionTeam = possession.best_team;
  match.designatedPossessionPlayer = possession.designated_player;

  // ResolvePlayerContacts: live pair mutations, then movement sharing.
  match.Mirror(reverse, !reverse, false);
  std::vector<Player*> players;
  match.GetTeam(match.first_team)->GetActivePlayers(players);
  match.GetTeam(match.second_team)->GetActivePlayers(players);
  football::sim::ResolvePlayerContacts(
      {players, *match.ball, match.designatedPossessionPlayer}, *match.referee_);

  // AdvanceClock precedes goal detection and all goal consequences.
  match.AdvanceTime(football::sim::TickSpan{1});

  bool first_team_goal = false;
  bool second_team_goal = false;
  if (match.IsBallInPlay()) {
    // Retain the per-side legacy lookahead gate; geometry itself needs no Ball.
    const auto crossed_goal = [&](int side) {
      if (fabs(match.ball->Predict(10).coords[0]) < match.pitch_.half_length() - 1.0) return false;
      return football::sim::CrossedGoalLine(
          match.pitch_, side, previousBallPos, match.ball->Predict(0));
    };
    first_team_goal = crossed_goal(match.teams[match.first_team]->GetDynamicSide());
    second_team_goal = crossed_goal(match.teams[match.second_team]->GetDynamicSide());
  }
  bool goal = first_team_goal | second_team_goal;
  match.ballIsInGoal |= goal;
  match.Mirror(reverse, !reverse, false);
  if (match.IsBallInPlay()) {
    if (goal) {
      int team = first_team_goal ? match.second_team : match.first_team;
      ++match.score_[match.teams[team]->GetID()];
      match.SetGoalScored(true);
      match.lastGoalTeam = match.teams[team];
    }
    if (first_team_goal || second_team_goal) {
      bool ownGoal = true;
      if (match.GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == match.GetLastGoalTeam()->GetID() || match.GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == match.GetLastGoalTeam()->GetID()) ownGoal = false;
      if (!ownGoal) {
        match.lastGoalScorer = match.GetLastGoalTeam()->GetLastTouchPlayer();
      } else {
        match.lastGoalScorer = match.teams[abs(match.GetLastGoalTeam()->GetID() - 1)]->GetLastTouchPlayer();
      }
    }
  }
}



bool Simulation::IsInPlay() const {
  return match_ && match_->IsInPlay();
}

WorldState Simulation::Observe() const {
  if (!match_) throw std::logic_error("simulation has no match");
  return BuildWorldState(*match_);
}

void Simulation::CaptureMentalImage(Match& match) {
  if (mental_images_.empty() ||
      match.GetTimelineTick().value % football::sim::observation::kMentalImageCadence.value == 0) {
    std::vector<Player*> players;
    match.GetTeam(match.FirstTeam())->GetActivePlayers(players);
    match.GetTeam(match.SecondTeam())->GetActivePlayers(players);
    mental_images_.insert(mental_images_.begin(),
                          MentalImage(match.GetTimelineTick(), players, *match.GetBall()));
    if (mental_images_.size() > 3) {
      mental_images_.pop_back();
    }
  }
}

MentalImage* Simulation::GetMentalImage(football::sim::TickSpan history) {
  return football::sim::observation::SampleMentalImage(mental_images_, history);
}

MentalImage* Simulation::GetMentalImage(std::chrono::milliseconds history) {
  return football::sim::observation::SampleMentalImage(mental_images_, history);
}

bool Simulation::Finished() const {
  return match_ && match_->Finished();
}

MatchResult Simulation::Result() const {
  if (!match_) throw std::logic_error("simulation has no final result");
  return match_->Result();
}

bool Simulation::Stop() {
  if (!match_) return false;
  match_->Exit();
  mental_images_.clear();
  match_.reset();
  return true;
}
