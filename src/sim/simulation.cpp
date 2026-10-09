#include "sim/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

#include "sim/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "sim/player/player_ball_contact.hpp"
#include "sim/ball_touch_application.hpp"
#include "sim/event/ball_touch_dispatcher.hpp"
#include "sim/event/touch_query.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/observation/world_state_builder.hpp"
#include "sim/player/player_contact.hpp"
#include "sim/player/possession.hpp"
#include "sim/rules/goal.hpp"
#include "sim/rules/period.hpp"
#include "sim/rules/referee.hpp"
#include "sim/team/formation.hpp"
#include "sim/team/possession.hpp"
#include "sim/team/team.hpp"

using football::ball::Ball;

namespace {
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
class Simulation::RuleCommands final : public football::sim::rules::RuleCommandSink,
                                       public football::sim::PlayerRuntimeSink {
 public:
  explicit RuleCommands(Simulation& simulation) : simulation_(simulation) {}
  void StopPlay() override { simulation_.StopPlay(); }
  void StartPlay() override { simulation_.StartPlay(); }
  void StartSetPiece() override { simulation_.StartSetPiece(); }
  void StopSetPiece() override { simulation_.StopSetPiece(); }
  void StartBallInPlay() override { simulation_.StartBallInPlay(); }
  void SetBallRetainer(Player* retainer) override { simulation_.SetBallRetainer(retainer); }
  void ResetSituation(const Vector3& position) override { simulation_.ResetSituation(position); }
  void ResetBall(const Vector3& position) override { simulation_.ball_->ResetSituation(position); }
  void SetPhase(MatchPhase phase) override { simulation_.SetMatchPhase(phase); }
 private:
  Simulation& simulation_;
};

football::sim::rules::RefereeTickFacts Simulation::RefereeFacts() const {
  if (!ball_) throw std::logic_error("simulation has no match");
  return {GetTimelineTick(), phase_, play_authorized_, set_piece_active_, goal_scored_,
      *ball_, pitch_, GetRegulationTime(), *teams_[0], *teams_[1], first_team_,
      GetLastTouchTeam(), last_goal_team_,
      ToHomePitchFrame(*teams_[options_.reverse_team_processing ? 1 : 0]),
      PitchFrameTransform(teams_[0]->GetStaticSide() != -1)};
}

football::sim::PlayerTickContext Simulation::PlayerTickFacts(const Player& actor) {
  if (!ball_) throw std::logic_error("simulation has no match");
  Team& own = *teams_[actor.GetTeamID()];
  Team& opponent = *teams_[1 - actor.GetTeamID()];
  Team& first = *teams_[first_team_];
  Team& second = *teams_[second_team_];
  const int processing_slot = actor.GetTeamID() == second_team_ ? 1 : 0;
  return {GetTimelineTick(), play_authorized_, set_piece_active_, IsBallInPlay(),
          clock_->IsHalfUnderway(), *ball_, GetBallEnvironment(), ball_retainer_,
          designated_possession_player_, GetLastTouchPlayer(), touches_,
          referee_->GetBuffer(), pitch_, own, opponent, first, second, processing_slot,
          referee_->RestartNeedsSimulation(), rng_};
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
  football::sim::rules::BallTouchFacts facts;
  facts.now = GetTimelineTick();
  facts.defending_team = teams_[1 - event.team->GetID()].get();
  facts.in_play = play_authorized_;
  facts.in_set_piece = set_piece_active_;
  facts.offsides_enabled = options_.offsides;
  facts.ball = ball_.get();
  facts.pitch = &pitch_;
  std::vector<Player*> active;
  if (facts.offsides_enabled) {
    teams_[first_team_]->GetActivePlayers(active);
    teams_[second_team_]->GetActivePlayers(active);
    facts.all_active_players = active;
  }
  football::sim::event::DispatchBallTouch(event, touches_, facts, *referee_, *rule_commands_);
}

Simulation::Simulation() {}

Simulation::~Simulation() {
  Stop();
}

void Simulation::Init(
    const football::model::Team& home, const football::model::Team& away,
    const football::model::Pitch& pitch, MatchOptions options,
    const football::model::BallConfig& ball_config) {
  if (ball_) throw std::logic_error("simulation already initialized");
  // Native duration and full-match capacity are checked before any RNG draws.
  if (options.half_duration == football::sim::TickSpan{} ||
      options.half_duration.value > std::numeric_limits<std::uint64_t>::max() / 2) {
    throw std::invalid_argument("invalid regulation duration");
  }
  ValidatePlayers(home, away);
  football::model::Team home_model = home, away_model = away;
  // Appearance is not competition physics: isolate its seed-0 stream so a
  // second Init never depends on the previous match's residual RNG state.
  blunted::Rng appearance_rng;
  appearance_rng.Seed(0);
  ResolveAppearance(home_model, appearance_rng);
  ResolveAppearance(away_model, appearance_rng);

  // Effective formation: the same fallback BuildFormation uses (formation ->
  // tactical_formation -> players.size()), so kickoff possession agrees with the
  // rosters that are actually built.
  const std::vector<FormationEntry> left = BuildFormation(home_model);
  const std::vector<FormationEntry> right = BuildFormation(away_model);
  options.left_team_owns_ball = LeftTeamOwnsBall(left, right, options.ball_position);

  // Apply the episode seed after the appearance draws, before constructing
  // actors. Moving those draws across this boundary changes simulation RNG.
  rng_.Seed(options.game_engine_random_seed);

  // A fresh composition starts from a clean competition state, exactly as the
  // former composition constructor did. Re-Init after Stop must not inherit results.
  score_[0] = 0; score_[1] = 0;
  possession_60_seconds_ = 0.0f;
  phase_ = MatchPhase::PreMatch;
  play_authorized_ = false;
  set_piece_active_ = false;
  goal_scored_ = false;
  ball_in_goal_ = false;
  last_goal_team_ = nullptr;
  last_goal_scorer_ = nullptr;
  ball_retainer_ = nullptr;
  designated_possession_player_ = nullptr;
  best_possession_team_ = nullptr;
  reset_sequence_ = 0;
  pending_change_of_ends_ = false;
  last_body_ball_collision_tick_ = {};
  ball_mirrored_ = false;
  touches_.Reset();
  mental_images_.clear();

  EnsureAnimationLibrary();
  clock_.emplace(options.half_duration);
  options_ = options;
  pitch_ = pitch;
  ball_config_ = ball_config;
  first_team_ = options.reverse_team_processing ? 1 : 0;
  second_team_ = options.reverse_team_processing ? 0 : 1;

  // Build the whole composition locally, then publish it only on full success.
  auto ball = std::make_unique<Ball>(ball_config_, pitch_);
  const football::model::Team* descriptions[] = {&home_model, &away_model};
  std::array<std::unique_ptr<Team>, 2> teams;
  teams[first_team_] =
      std::make_unique<Team>(first_team_, *descriptions[first_team_],
          first_team_ ? options.right_team_difficulty : options.left_team_difficulty);
  teams[second_team_] =
      std::make_unique<Team>(second_team_, *descriptions[second_team_],
          second_team_ ? options.right_team_difficulty : options.left_team_difficulty);
  teams[first_team_]->SetOpponent(teams[second_team_].get());
  teams[second_team_]->SetOpponent(teams[first_team_].get());
  // Preserve the historical scheduling stagger across both rosters, including
  // reversed processing. Only a periodic phase is passed, not a creation ID.
  teams[first_team_]->InitPlayers(0, *animations_, pitch_, rng_);
  teams[second_team_]->InitPlayers(static_cast<std::uint8_t>(
      teams[first_team_]->GetAllPlayers().size() % 10), *animations_, pitch_, rng_);

  std::vector<Player*> active_players;
  teams[first_team_]->GetActivePlayers(active_players);
  Player* designated = active_players.at(0);

  auto referee = std::make_unique<Referee>(*teams[first_team_], options.ball_position);
  auto commands = std::make_unique<RuleCommands>(*this);
  auto touch = std::make_unique<TouchEvents>(*this);

  // Commit. Pointer/reference borrows stay valid: the heap objects do not move
  // when their owning unique_ptrs are transferred.
  ball_ = std::move(ball);
  teams_ = std::move(teams);
  referee_ = std::move(referee);
  player_runtime_sink_ = commands.get();
  rule_commands_ = std::move(commands);
  touch_sink_ = std::move(touch);

  designated_possession_player_ = designated;
  ball_retainer_ = nullptr;
  last_goal_team_ = nullptr;
  last_goal_scorer_ = nullptr;
  best_possession_team_ = nullptr;
  SetMatchPhase(MatchPhase::PreMatch);
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
  if (!ball_) throw std::logic_error("simulation has no match");
  if (Finished()) return;
  if (pending_change_of_ends_) {
    ApplyChangeOfEnds();
    pending_change_of_ends_ = false;
  }
  clock_->CountExecutedStep(phase_);

  // Frame-local controls are runtime input, not an orchestration algorithm.
  for (int team_id = 0; team_id < 2; ++team_id) {
    for (Player* player : teams_[team_id]->GetAllPlayers()) {
      player->ClearControl();
      if (const PlayerControl* control = controls.Get(player->GetID())) {
        player->SetControl(*control);
      }
    }
  }

  const bool reverse = options_.reverse_team_processing;
  // Ball already shares the first roster's frame; turn only the other roster.
  Mirror(reverse, !reverse, false);
  // Period whistles still win over pending contacts, before any RNG draw.
  if (IsBallInPlay() && !football::sim::rules::PeriodElapsed(
          IsHalfUnderway(), phase_, GetRegulationTime(), options_.half_duration)) {
    std::vector<Player*> players;
    teams_[first_team_]->GetActivePlayers(players);
    teams_[second_team_]->GetActivePlayers(players);
    Team* roster_ptrs[2] = {teams_[0].get(), teams_[1].get()};
    const football::sim::BallPlayerContactInputs inputs{
        roster_ptrs, first_team_, touches_.last_team, mental_images_,
        GetTimelineTick(), last_body_ball_collision_tick_, &*touch_sink_, touches_};
    const auto contact = football::sim::ResolveBallPlayerContacts(
        *ball_, players, inputs);
    if (contact.impulse) {
      football::sim::ApplyBallTouch(*ball_, GetBallEnvironment(), *contact.impulse,
          mental_images_, *teams_[first_team_], *teams_[second_team_],
          GetTimelineTick(), ball_retainer_);
      // Preserve the three argument-expression RNG draws and refresh-before-spin.
      ball_->SetRotation(rng_.Uniform(-30, 30), rng_.Uniform(-30, 30),
          rng_.Uniform(-30, 30), contact.rotation_bias, GetBallEnvironment());
      last_body_ball_collision_tick_ = GetTimelineTick();
    }
  }

  // ProcessReferee: before this tick's ball/player movement, in the contact frame.
  if (football::sim::rules::PeriodElapsed(
          IsHalfUnderway(), phase_, GetRegulationTime(), options_.half_duration)) {
    EndPeriod();
  } else {
    referee_->Process(RefereeFacts(), options_, rng_, *rule_commands_);
  }
  Vector3 previous_ball_pos = ball_->Predict(0);
  Mirror(reverse, !reverse, false);
  // Restore the processing frame even on the referee's terminal transition.
  if (Finished()) return;
  if (!play_authorized_ && !referee_->RestartNeedsSimulation() &&
      (GetTimelineTick() < referee_->GetBuffer().prepare_tick ||
       referee_->GetBuffer().prepare_tick + football::sim::TickSpan{1} < GetTimelineTick())) {
    // Ceremonies execute only their placement tail; both clocks stay stopped.
    const auto admitted = clock_->Advance(football::sim::TickSpan{1}, phase_);
    UpdateRecentPossession(admitted);
    return;
  }
  // StepBall.
  Mirror(false, false, reverse);
  ball_->Step(football::sim::TickSpan{1}, GetBallEnvironment());
  Mirror(false, false, reverse);

  // CaptureHistory: preserve the pre-player-processing capture and sample-zero timing.
  CaptureMentalImage();

  // StepPlayers: team estimates bracket roster-ordered actor execution.
  const auto step_team = [&](int id) {
    Team& team = *teams_[id];
    Team& opponent = *teams_[1 - id];
    football::sim::player::PrepareTeamPossession(team, opponent, play_authorized_,
        set_piece_active_, ball_retainer_, best_possession_team_);
    for (Player* actor : team.GetAllPlayers()) {
      if (actor->IsActive()) actor->Process(PlayerTickFacts(*actor), mental_images_, *touch_sink_, *player_runtime_sink_);
    }
    football::sim::player::FinishTeamPossession(team, opponent);
  };
  Mirror(first_team_ == 1, first_team_ == 0, false);
  step_team(first_team_);
  Mirror(true, true, true);
  step_team(second_team_);
  Mirror(first_team_ == 0, first_team_ == 1, true);

  // UpdatePossession: retain both per-roster refreshes before arbitration.
  Mirror(first_team_ == 1, first_team_ == 0, false);
  football::sim::player::RefreshTeamPossession(*teams_[first_team_],
      *teams_[second_team_], *ball_, GetTimelineTick(), ball_retainer_);
  Mirror(true, true, true);
  football::sim::player::RefreshTeamPossession(*teams_[second_team_],
      *teams_[first_team_], *ball_, GetTimelineTick(), ball_retainer_);
  Mirror(first_team_ == 0, first_team_ == 1, true);

  const auto possession = football::sim::EvaluatePossession(
      *teams_[first_team_], *teams_[second_team_],
      designated_possession_player_, ball_retainer_);
  best_possession_team_ = possession.best_team;
  designated_possession_player_ = possession.designated_player;

  // ResolvePlayerContacts: live pair mutations, then movement sharing.
  Mirror(reverse, !reverse, false);
  std::vector<Player*> players;
  teams_[first_team_]->GetActivePlayers(players);
  teams_[second_team_]->GetActivePlayers(players);
  football::sim::ResolvePlayerContacts(
      {GetTimelineTick(), players, *ball_, designated_possession_player_, ball_retainer_},
      *referee_);

  // AdvanceClock → recent possession window → goal detection/consequences.
  const auto admitted = clock_->Advance(football::sim::TickSpan{1}, phase_);
  UpdateRecentPossession(admitted);

  bool first_team_goal = false;
  bool second_team_goal = false;
  if (IsBallInPlay()) {
    // Retain the per-side legacy lookahead gate; geometry itself needs no Ball.
    const auto crossed_goal = [&](int side) {
      if (fabs(ball_->Predict(10).coords[0]) < pitch_.half_length() - 1.0) return false;
      return football::sim::CrossedGoalLine(
          pitch_, side, previous_ball_pos, ball_->Predict(0));
    };
    first_team_goal = crossed_goal(teams_[first_team_]->GetDynamicSide());
    second_team_goal = crossed_goal(teams_[second_team_]->GetDynamicSide());
  }
  bool goal = first_team_goal | second_team_goal;
  ball_in_goal_ |= goal;
  Mirror(reverse, !reverse, false);
  if (IsBallInPlay()) {
    if (goal) {
      int team = first_team_goal ? second_team_ : first_team_;
      ++score_[teams_[team]->GetID()];
      SetGoalScored(true);
      last_goal_team_ = teams_[team].get();
    }
    if (first_team_goal || second_team_goal) {
      bool own_goal = true;
      if (GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == last_goal_team_->GetID() ||
          GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == last_goal_team_->GetID()) own_goal = false;
      if (!own_goal) {
        last_goal_scorer_ = football::sim::event::LastTouchPlayer(touches_, *last_goal_team_);
      } else {
        last_goal_scorer_ = football::sim::event::LastTouchPlayer(
            touches_, *teams_[abs(last_goal_team_->GetID() - 1)]);
      }
    }
  }
}

bool Simulation::IsInPlay() const {
  return ball_ && play_authorized_;
}

WorldState Simulation::Observe() const {
  if (!ball_) throw std::logic_error("simulation has no match");
  const int first_team = options_.reverse_team_processing ? 1 : 0;
  return BuildWorldState({GetTimelineTick(), phase_,
      GetRegulationTime(), GetBallInPlayTime(), clock_->IsHalfUnderway(),
      IsBallInPlay(), reset_sequence_, *ball_, pitch_,
      play_authorized_, set_piece_active_, *referee_, ball_retainer_,
      score_[0], score_[1], *teams_[0], *teams_[1],
      first_team});
}

void Simulation::TouchBall(const Vector3& impulse) {
  if (!ball_) throw std::logic_error("simulation has no match");
  football::sim::ApplyBallTouch(*ball_, GetBallEnvironment(), impulse,
      mental_images_, *teams_[first_team_], *teams_[second_team_],
      GetTimelineTick(), ball_retainer_);
}

void Simulation::Mirror(bool team_0, bool team_1, bool ball) {
  if (!ball_) throw std::logic_error("simulation has no match");
  if (team_0) teams_[0]->Mirror();
  if (team_1) teams_[1]->Mirror();
  if (ball) {
    ball_mirrored_ = !ball_mirrored_;
    ball_->Mirror();
  }
  for (auto& image : mental_images_) {
    image.Mirror(team_0, team_1, ball);
  }
}

void Simulation::ResetSituation(const Vector3& focus_position) {
  if (!ball_) throw std::logic_error("simulation has no match");
  ++reset_sequence_;
  ball_retainer_ = nullptr;
  SetGoalScored(false);
  mental_images_.clear();
  goal_scored_ = false;
  ball_in_goal_ = false;
  touches_.Reset();
  last_goal_scorer_ = nullptr;
  best_possession_team_ = nullptr;
  last_body_ball_collision_tick_ = {};
  ball_->ResetSituation(focus_position);
  teams_[first_team_]->ResetSituation(focus_position, GetTimelineTick());
  teams_[second_team_]->ResetSituation(focus_position, GetTimelineTick());
}

void Simulation::SetMatchPhase(MatchPhase newPhase) {
  phase_ = newPhase;
  if (Finished()) { EndHalf(); return; }
  teams_[first_team_]->RelaxFatigue(1.0f);
  teams_[second_team_]->RelaxFatigue(1.0f);
}

void Simulation::StartBallInPlay() {
  if (!play_authorized_ || (phase_ != MatchPhase::FirstHalf &&
                            phase_ != MatchPhase::SecondHalf))
    throw std::logic_error("ball cannot enter play outside an authorized half");
  clock_->BeginHalf();
  clock_->StartBallInPlay();
}

void Simulation::EndPeriod() {
  // Keep the old publication order: stop clocks/play, referee facts, phase,
  // pending end change. The physical change remains at the next Step's entry.
  EndHalf();
  referee_->OnPeriodEnded(phase_, GetTimelineTick(),
      options_.ball_position,
      *teams_[options_.left_team_owns_ball ? 1 : 0]);
  if (phase_ == MatchPhase::SecondHalf) {
    SetMatchPhase(MatchPhase::Finished);
    return;
  }
  SetMatchPhase(MatchPhase::SecondHalf);
  pending_change_of_ends_ = true;
}

void Simulation::ApplyChangeOfEnds() {
  // Permanent end change: preserve processing-roster order and canonical flags.
  teams_[first_team_]->SwitchEnds();
  teams_[second_team_]->SwitchEnds();
  ball_->Mirror();
  for (auto& image : mental_images_) {
    image.Mirror(true, true, true);
  }
}

void Simulation::UpdateRecentPossession(football::sim::TickSpan admitted) {
  if (IsBallInPlay() && !set_piece_active_) {
    // Continuous possession window in SI seconds, derived from admitted ticks.
    const float seconds = football::sim::ToSeconds(admitted);
    if (teams_[0].get() == designated_possession_player_->GetTeam()) {
      possession_60_seconds_ = std::max(possession_60_seconds_ - seconds, -60.0f);
    } else {
      possession_60_seconds_ = std::min(possession_60_seconds_ + seconds, 60.0f);
    }
  }
}

void Simulation::AdvanceTime(football::sim::TickSpan delta) {
  if (!ball_) throw std::logic_error("simulation has no match");
  if (Finished()) return;
  const auto admitted = clock_->Advance(delta, phase_);
  UpdateRecentPossession(admitted);
}

void Simulation::CaptureMentalImage() {
  if (mental_images_.empty() ||
      GetTimelineTick().value % football::sim::observation::kMentalImageCadence.value == 0) {
    std::vector<Player*> players;
    teams_[first_team_]->GetActivePlayers(players);
    teams_[second_team_]->GetActivePlayers(players);
    mental_images_.insert(mental_images_.begin(),
                          MentalImage(GetTimelineTick(), players, *ball_));
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

Team* Simulation::GetLastTouchTeam() const {
  if (touches_.last_team != -1) return teams_[touches_.last_team].get();
  return teams_[first_team_].get();
}

Player* Simulation::GetLastTouchPlayer() const {
  return football::sim::event::LastTouchPlayer(touches_, *GetLastTouchTeam());
}

void Simulation::GetActiveTeamPlayers(int team_id, std::vector<Player*>& players) {
  teams_[team_id]->GetActivePlayers(players);
}

bool Simulation::Finished() const {
  return ball_ && phase_ == MatchPhase::Finished;
}

MatchResult Simulation::Result() const {
  if (!ball_) throw std::logic_error("simulation has no final result");
  if (!Finished()) throw std::logic_error("match result requires full time");
  const auto outcome = score_[0] > score_[1] ? MatchOutcome::HomeWin
      : score_[0] < score_[1] ? MatchOutcome::AwayWin : MatchOutcome::Draw;
  return {score_[0], score_[1], outcome, clock_->ExecutedTicks()};
}

bool Simulation::Stop() {
  if (!ball_) return false;
  teams_[first_team_]->Exit(GetTimelineTick());
  teams_[second_team_]->Exit(GetTimelineTick());
  // Clear every actor borrow before their owners go away: an uninitialized
  // Simulation must hold no dangling pointers.
  last_goal_team_ = nullptr;
  last_goal_scorer_ = nullptr;
  ball_retainer_ = nullptr;
  designated_possession_player_ = nullptr;
  best_possession_team_ = nullptr;
  referee_.reset();
  mental_images_.clear();
  touch_sink_.reset();
  player_runtime_sink_ = nullptr;
  rule_commands_.reset();
  ball_.reset();
  teams_[0].reset();
  teams_[1].reset();
  clock_.reset();
  return true;
}
