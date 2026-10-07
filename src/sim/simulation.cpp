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
#include "sim/ball/ball_touch_application.hpp"



#include "sim/match/match.hpp"
#include "sim/event/ball_touch_dispatcher.hpp"
#include "sim/observation/world_state_builder.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player_contact.hpp"
#include "sim/player/possession.hpp"
#include "sim/rules/goal.hpp"
#include "sim/rules/period.hpp"
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

// Only the composition root translates write-only rule commands to runtime state.
class Simulation::RuleCommands final : public football::sim::rules::RuleCommandSink {
 public:
  explicit RuleCommands(Simulation& simulation) : simulation_(simulation) {}
  void StopPlay() override { simulation_.match_->StopPlay(); }
  void StartPlay() override { simulation_.match_->StartPlay(); }
  void StartSetPiece() override { simulation_.match_->StartSetPiece(); }
  void StopSetPiece() override { simulation_.match_->StopSetPiece(); }
  void StartBallInPlay() override { simulation_.match_->StartBallInPlay(); }
  void ResetSituation(const Vector3& position) override { simulation_.ResetSituation(position); }
  void ResetBall(const Vector3& position) override { simulation_.match_->GetBall()->ResetSituation(position); }
  void SetPhase(MatchPhase phase) override { simulation_.match_->SetMatchPhase(phase); }
 private:
  Simulation& simulation_;
};

football::sim::rules::RefereeTickFacts Simulation::RefereeFacts() const {
  if (!match_) throw std::logic_error("simulation has no match");
  auto& match = *match_;
  return {match.GetTimelineTick(), match.GetMatchPhase(), match.IsInPlay(),
      match.IsInSetPiece(), match.IsGoalScored(), *match.GetBall(), match.pitch(),
      match.GetRegulationTime(), *match.GetTeam(0), *match.GetTeam(1), match.FirstTeam(),
      match.GetLastTouchTeam(), match.GetLastGoalTeam(), ToHomePitchFrame(match),
      PitchFrameTransform(match.GetTeam(0)->GetStaticSide() != -1)};
}

football::sim::PlayerTickContext Simulation::PlayerTickFacts() const {
  if (!match_) throw std::logic_error("simulation has no match");
  auto& match = *match_;
  return {match.GetTimelineTick(), match.IsInPlay(), match.clock_.IsHalfUnderway(),
          match.GetLastTouchPlayer(), *match.GetBall(), match.rng()};
}

class Simulation::TouchEvents final : public football::sim::BallTouchSink {
 public:
  explicit TouchEvents(Simulation& simulation) : simulation_(simulation) {}
  void OnBallTouched(const football::sim::BallTouchEvent& event) override {
    simulation_.PublishBallTouch(event);
  }
 private:
  Simulation& simulation_;
};

void Simulation::PublishBallTouch(const football::sim::BallTouchEvent& event) {
  Match& match = *match_;
  football::sim::rules::BallTouchFacts facts;
  facts.now = match.GetTimelineTick();
  facts.defending_team = match.GetTeam(1 - event.team->GetID());
  facts.in_play = match.IsInPlay();
  facts.in_set_piece = match.IsInSetPiece();
  facts.offsides_enabled = match.options().offsides;
  facts.ball = match.GetBall();
  facts.stadium_to_home = PitchFrameTransform(match.GetTeam(0)->GetStaticSide() != -1);
  std::vector<Player*> active;
  if (facts.offsides_enabled) {
    match.GetTeam(match.FirstTeam())->GetActivePlayers(active);
    match.GetTeam(match.SecondTeam())->GetActivePlayers(active);
    facts.all_active_players = active;
  }
  football::sim::event::DispatchBallTouch(event, match.touches_, facts, *referee_, *rule_commands_);
}

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
                                  animations_);
  referee_ = std::make_unique<Referee>(*match_->teams[match_->first_team], options.ball_position);
  match_->referee_ = referee_.get();
  rule_commands_ = std::make_unique<RuleCommands>(*this);
  touch_sink_ = std::make_unique<TouchEvents>(*this);
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
    ApplyChangeOfEnds(match);
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
  Mirror(reverse, !reverse, false);
  // Period whistles still win over pending contacts, before any RNG draw.
  if (match.IsBallInPlay() && !football::sim::rules::PeriodElapsed(
          match.IsHalfUnderway(), match.GetMatchPhase(),
          match.GetRegulationTime(), match.options().half_duration)) {
    std::vector<Player*> players;
    match.GetTeam(match.FirstTeam())->GetActivePlayers(players);
    match.GetTeam(match.SecondTeam())->GetActivePlayers(players);
    const football::sim::BallPlayerContactInputs inputs{
        match.teams, match.FirstTeam(), match.touches_.last_team, mental_images_,
        match.GetTimelineTick(), match.last_body_ball_collision_tick_, &*touch_sink_, match.touches_};
    const auto contact = football::sim::ResolveBallPlayerContacts(
        *match.GetBall(), players, inputs);
    if (contact.impulse) {
      football::sim::ApplyBallTouch(*match.ball, match.GetBallEnvironment(), *contact.impulse,
          mental_images_, *match.teams[match.first_team], *match.teams[match.second_team],
          match.GetTimelineTick(), match.ballRetainer);
      // Preserve the three argument-expression RNG draws and refresh-before-spin.
      match.GetBall()->SetRotation(rng_.Uniform(-30, 30), rng_.Uniform(-30, 30),
          rng_.Uniform(-30, 30), contact.rotation_bias, match.GetBallEnvironment());
      match.last_body_ball_collision_tick_ = match.GetTimelineTick();
    }
  }

  // ProcessReferee: before this tick's ball/player movement, in the contact frame.
  if (football::sim::rules::PeriodElapsed(
          match.IsHalfUnderway(), match.GetMatchPhase(),
          match.GetRegulationTime(), match.options().half_duration)) {
    EndPeriod(match);
  } else {
    match.GetReferee()->Process(RefereeFacts(), match.options(), rng_, *rule_commands_);
  }
  Vector3 previousBallPos = match.ball->Predict(0);
  Mirror(reverse, !reverse, false);
  // Restore the processing frame even on the referee's terminal transition.
  if (match.Finished()) return;
  if (!match.IsInPlay() && !match.GetReferee()->RestartNeedsSimulation() &&
      (match.GetTimelineTick() < match.GetReferee()->GetBuffer().prepare_tick ||
       match.GetReferee()->GetBuffer().prepare_tick + football::sim::TickSpan{1} < match.GetTimelineTick())) {
    // Ceremonies execute only their placement tail; both clocks stay stopped.
    const auto admitted = match.clock_.Advance(football::sim::TickSpan{1}, match.matchPhase);
    UpdateRecentPossession(match, admitted);
    return;
  }
  // StepBall.
  Mirror(false, false, reverse);
  match.ball->Process(match.GetBallEnvironment());
  Mirror(false, false, reverse);

  // CaptureHistory: preserve the pre-player-processing capture and sample-zero timing.
  CaptureMentalImage(match);

  // StepPlayers: team estimates bracket roster-ordered actor execution.
  const auto step_team = [&](int id) {
    Team& team = *match.teams[id];
    Team& opponent = *match.teams[1 - id];
    football::sim::player::PrepareTeamPossession(team, opponent, match.IsInPlay(),
        match.IsInSetPiece(), match.ballRetainer, match.bestPossessionTeam);
    for (Player* actor : team.GetAllPlayers()) {
      if (actor->IsActive()) actor->Process(PlayerTickFacts(), mental_images_, *touch_sink_);
    }
    football::sim::player::FinishTeamPossession(team, opponent);
  };
  Mirror(match.first_team == 1, match.first_team == 0, false);
  step_team(match.first_team);
  Mirror(true, true, true);
  step_team(match.second_team);
  Mirror(match.first_team == 0, match.first_team == 1, true);

  // UpdatePossession: retain both per-roster refreshes before arbitration.
  Mirror(match.first_team == 1, match.first_team == 0, false);
  football::sim::player::RefreshTeamPossession(*match.teams[match.first_team],
      *match.teams[match.second_team], *match.ball, match.GetTimelineTick(), match.ballRetainer);
  Mirror(true, true, true);
  football::sim::player::RefreshTeamPossession(*match.teams[match.second_team],
      *match.teams[match.first_team], *match.ball, match.GetTimelineTick(), match.ballRetainer);
  Mirror(match.first_team == 0, match.first_team == 1, true);

  const auto possession = football::sim::EvaluatePossession(
      *match.teams[match.first_team], *match.teams[match.second_team],
      match.designatedPossessionPlayer, match.ballRetainer);
  match.bestPossessionTeam = possession.best_team;
  match.designatedPossessionPlayer = possession.designated_player;

  // ResolvePlayerContacts: live pair mutations, then movement sharing.
  Mirror(reverse, !reverse, false);
  std::vector<Player*> players;
  match.GetTeam(match.first_team)->GetActivePlayers(players);
  match.GetTeam(match.second_team)->GetActivePlayers(players);
  football::sim::ResolvePlayerContacts(
      {match.GetTimelineTick(), players, *match.ball, match.designatedPossessionPlayer},
      *match.referee_);

  // AdvanceClock → recent possession window → goal detection/consequences.
  const auto admitted = match.clock_.Advance(football::sim::TickSpan{1}, match.matchPhase);
  UpdateRecentPossession(match, admitted);

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
  Mirror(reverse, !reverse, false);
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
        match.lastGoalScorer = football::sim::event::LastTouchPlayer(match.touches_, *match.GetLastGoalTeam());
      } else {
        match.lastGoalScorer = football::sim::event::LastTouchPlayer(
            match.touches_, *match.teams[abs(match.GetLastGoalTeam()->GetID() - 1)]);
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

void Simulation::TouchBall(const Vector3& impulse) {
  if (!match_) throw std::logic_error("simulation has no match");
  Match& match = *match_;
  football::sim::ApplyBallTouch(*match.ball, match.GetBallEnvironment(), impulse,
      mental_images_, *match.teams[match.first_team], *match.teams[match.second_team],
      match.GetTimelineTick(), match.ballRetainer);
}

void Simulation::Mirror(bool team_0, bool team_1, bool ball) {
  if (!match_) throw std::logic_error("simulation has no match");
  Match& match = *match_;
  if (team_0) match.teams[0]->Mirror();
  if (team_1) match.teams[1]->Mirror();
  if (ball) {
    match.ball_mirrored = !match.ball_mirrored;
    match.ball->Mirror();
  }
  for (auto& image : mental_images_) {
    image.Mirror(team_0, team_1, ball);
  }
}

void Simulation::ResetSituation(const Vector3& focus_position) {
  if (!match_) throw std::logic_error("simulation has no match");
  Match& match = *match_;
  ++match.reset_sequence_;
  match.SetBallRetainer(0);
  match.SetGoalScored(false);
  mental_images_.clear();
  match.goalScored = false;
  match.ballIsInGoal = false;
  match.touches_.Reset();
  match.lastGoalScorer = 0;
  match.bestPossessionTeam = 0;
  match.last_body_ball_collision_tick_ = {};
  match.ball->ResetSituation(focus_position);
  match.teams[match.first_team]->ResetSituation(focus_position, match.GetTimelineTick());
  match.teams[match.second_team]->ResetSituation(focus_position, match.GetTimelineTick());
}

void Simulation::EndPeriod(Match& match) {
  // Keep the old publication order: stop clocks/play, referee facts, phase,
  // pending end change. The physical change remains at the next Step's entry.
  match.EndHalf();
  match.GetReferee()->OnPeriodEnded(match.GetMatchPhase(), match.GetTimelineTick(),
      match.options().ball_position,
      *match.GetTeam(match.options().left_team_owns_ball ? 1 : 0));
  if (match.GetMatchPhase() == MatchPhase::SecondHalf) {
    match.SetMatchPhase(MatchPhase::Finished);
    return;
  }
  match.SetMatchPhase(MatchPhase::SecondHalf);
  match.RequestChangeOfEnds();
}

void Simulation::ApplyChangeOfEnds(Match& match) {
  // Permanent end change: preserve processing-roster order and canonical flags.
  match.teams[match.first_team]->SwitchEnds();
  match.teams[match.second_team]->SwitchEnds();
  match.ball->Mirror();
  for (auto& image : mental_images_) {
    image.Mirror(true, true, true);
  }
}

void Simulation::UpdateRecentPossession(Match& match, football::sim::TickSpan admitted) {
  if (match.IsBallInPlay() && !match.IsInSetPiece()) {
    // Continuous possession window in SI seconds, derived from admitted ticks.
    const float seconds = football::sim::ToSeconds(admitted);
    if (match.teams[0] == match.designatedPossessionPlayer->GetTeam()) {
      match.possession60seconds_ = std::max(match.possession60seconds_ - seconds, -60.0f);
    } else {
      match.possession60seconds_ = std::min(match.possession60seconds_ + seconds, 60.0f);
    }
  }
}

void Simulation::AdvanceTime(football::sim::TickSpan delta) {
  if (!match_) throw std::logic_error("simulation has no match");
  Match& match = *match_;
  if (match.Finished()) return;
  const auto admitted = match.clock_.Advance(delta, match.matchPhase);
  UpdateRecentPossession(match, admitted);
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
  referee_.reset();
  mental_images_.clear();
  touch_sink_.reset();
  rule_commands_.reset();
  match_.reset();
  return true;
}
