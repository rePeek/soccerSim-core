#include "sim/simulation.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "sim/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "sim/player/player_ball_contact.hpp"
#include "sim/ball_touch_application.hpp"
#include "sim/event/touch_query.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/observation/world_state_builder.hpp"
#include "sim/player/player_contact.hpp"
#include "sim/player/possession.hpp"
#include "sim/referee/goal.hpp"
#include "sim/referee/period.hpp"
#include "sim/referee/referee.hpp"
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
          referee_->RestartNeedsSimulation(), rng_, active_touch_shadow_sink_.get()};
}

// The single write-only fact-production port. Touch bookkeeping stays synchronous
// so later actors observe earlier touches; every fact then enters the tick buffer.
// The single synchronous accepted-touch port. Touch bookkeeping stays in the
// legacy order so later actors observe earlier touches.
class Simulation::TouchEvents final : public football::sim::AcceptedTouchSink {
 public:
  explicit TouchEvents(Simulation& simulation) : simulation_(simulation) {}
  void OnAcceptedTouch(const football::sim::event::AcceptedTouch& touch) override {
    Player* player = simulation_.FindPlayerById(touch.player);
    if (player != nullptr) {
      player->SetLastTouchTick(touch.touched_at);
      player->SetLastTouchType(touch.type);
    }
    simulation_.touches_.Record(static_cast<int>(touch.team), touch.player, touch.type);
    simulation_.recorded_touches_.push_back(football::sim::event::RecordedTouch{
        simulation_.snapshot_step_.value_or(0), simulation_.reset_sequence_,
        touch.player, touch.type, touch.action_type});
    simulation_.DispatchTouch(touch);
  }
 private:
  Simulation& simulation_;
};

Player* Simulation::FindPlayerById(football::model::PlayerId id) const {
  for (const auto& team : teams_) {
    if (!team) continue;
    for (Player* player : team->GetAllPlayers()) {
      if (player->GetID() == id) return player;
    }
  }
  return nullptr;
}

// P4b observes a simple linear body sweep before the legacy resolver runs.
// It never steps an actor, writes a touch, or passes its colliders to Ball.

void Simulation::BeginBodyCollisionShadow() {
  DiscardBodyCollisionShadow();
  if (!body_shadow_enabled_) return;
  body_shadow_touch_start_ = recorded_touches_.size();
  body_shadow_active_ = true;

  // Fixed match slots, not active-roster order. Both teams currently share
  // the first processing roster's frame with Ball.
  for (std::size_t slot = 0; slot < snapshot_players_.size(); ++slot) {
    Player* player = snapshot_players_[slot];
    if (!player->IsActive()) continue;
    auto prediction = PredictBodyColliderMotions(
        player->GetID(), player->GetKinematicState(), football::sim::TickSpan{1},
        PlayerBodyColliderIdsForSlot(static_cast<std::uint32_t>(slot)));
    prediction.action_state = player->GetSimulationActionState();
    prediction.action = prediction.action_state.type;
    body_shadow_colliders_.insert(body_shadow_colliders_.end(),
                                  prediction.colliders.begin(),
                                  prediction.colliders.end());
    body_shadow_predictions_.push_back(std::move(prediction));
  }

  body_shadow_contact_ = football::ball::FirstContact(
      ball_->state(), body_shadow_colliders_, football::sim::kTickSeconds,
      ball_config_.radius());
  BeginBodyPhysicsShadow();
  if (!body_shadow_contact_) return;

  const auto owner = body_collider_owners_.find(body_shadow_contact_->collider);
  if (owner != body_collider_owners_.end() &&
      owner->second.second == PlayerBodyPart::LowerBody) {
    const Player* player = FindPlayerById(owner->second.first);
    body_shadow_action_phase_conflict_ =
        player != nullptr && player->TouchPending();
  }
}

void Simulation::MeasureBodyShadowEndpoint(const Player& player) {
  if (!body_shadow_active_) return;
  for (auto& prediction : body_shadow_predictions_) {
    if (prediction.player != player.GetID()) continue;
    // Second roster executes in the opposite frame. Rotate only this copy,
    // before pair offsets/other actors can contaminate the Process endpoint.
    auto actual = player.GetKinematicState();
    if (player.GetTeamID() == second_team_) actual.Mirror();
    prediction.endpoint_error = (actual.position - prediction.end.position).GetLength();
    prediction.turned = prediction.start.velocity.GetLength() > .5f &&
        actual.velocity.GetLength() > .5f &&
        std::fabs(actual.velocity.GetAngle2D(prediction.start.velocity)) > .05f;
    break;
  }
}

void Simulation::CompleteBodyCollisionShadow() {
  if (!body_shadow_active_) return;

  ++body_shadow_report_.predicted_ticks;
  body_shadow_report_.predicted_players += body_shadow_predictions_.size();
  if (body_shadow_contact_) ++body_shadow_report_.first_contacts;
  if (body_shadow_action_phase_conflict_) {
    ++body_shadow_report_.action_phase_conflicts;
  }

  for (const auto& prediction : body_shadow_predictions_) {
    if (!prediction.endpoint_error) continue;
    const float error = *prediction.endpoint_error;
    body_shadow_report_.endpoint_errors.Record(error);
    body_shadow_report_.action_errors[BodyShadowActionCategory(prediction.action)].Record(error);
    if (prediction.turned) body_shadow_report_.turning_errors.Record(error);
  }

  std::optional<football::model::PlayerId> shadow_player;
  if (body_shadow_contact_) {
    const auto owner = body_collider_owners_.find(body_shadow_contact_->collider);
    if (owner != body_collider_owners_.end()) shadow_player = owner->second.first;
  }
  bool matching_touch = false;
  for (std::size_t i = body_shadow_touch_start_; i < recorded_touches_.size(); ++i) {
    const auto& touch = recorded_touches_[i];
    if (touch.type != e_TouchType_Accidental) continue;
    ++body_shadow_report_.accepted_accidental_touches;
    if (shadow_player && touch.player == *shadow_player) {
      matching_touch = true;
      ++body_shadow_report_.matched_touches;
    } else {
      ++body_shadow_report_.missed_touches;
    }
  }
  if (shadow_player && !matching_touch) {
    ++body_shadow_report_.false_positive_contacts;
  }
  CompleteBodyPhysicsShadow();
  DiscardBodyCollisionShadow();
}

void Simulation::DiscardBodyCollisionShadow() {
  body_shadow_predictions_.clear();
  body_shadow_colliders_.clear();
  body_shadow_contact_.reset();
  body_shadow_active_ = false;
  body_shadow_action_phase_conflict_ = false;
  body_physics_shadow_tick_.reset();
  body_physics_shadow_colliders_.clear();
  body_physics_shadow_candidates_.clear();
}

// Collects domain verdicts; Simulation applies them at an explicit boundary.
class Simulation::RulingEvents final : public football::sim::event::RulingSink {
 public:
  explicit RulingEvents(Simulation& simulation) : simulation_(simulation) {}
  void Submit(const football::sim::event::RefereeRuling& ruling) override {
    simulation_.pending_rulings_.push_back(ruling);
  }
 private:
  Simulation& simulation_;
};


void Simulation::DispatchTouch(const football::sim::event::AcceptedTouch& touch) {
  const auto tick = RefereeFacts();
  std::vector<Player*> active;
  if (options_.offsides) {
    teams_[first_team_]->GetActivePlayers(active);
    teams_[second_team_]->GetActivePlayers(active);
  }
  const football::sim::rules::RefereeView view{tick, active, options_.offsides};
  // Recognize behavior first, then let the referee judge the same touch. Both
  // read the same immutable record and the same admitted instant.
  recognizer_.Consume(touch,
      football::sim::event::EventView{tick.play_authorized, tick.set_piece_active, snapshot_step_, reset_sequence_});
  CommitEventTransitions(touch.touched_at);
  referee_->ConsumeBallTouch(touch.touched_at, touch, view, *rule_commands_);
}

void Simulation::ApplyGoalRuling(const football::sim::event::AwardGoalRuling& ruling) {
  recognizer_.OnGoalConfirmed(GetTimelineTick(), ruling.team, snapshot_step_);
  CommitEventTransitions(GetTimelineTick());
  const int team = static_cast<int>(ruling.team);
  ++score_[team];
  SetGoalScored(true);
  last_goal_team_ = teams_[team].get();
}

void Simulation::ApplyRestartRuling(
    const football::sim::event::AwardRestartRuling& ruling) {
  // Restart scheduling is still executed synchronously by the referee; the
  // confirmed verdict is recorded so no ruling kind is silently dropped here.
  event_log_.Record(football::sim::event::RestartAwardedEvent{
      GetTimelineTick(), ruling.team, ruling.kind, ruling.position});
}

void Simulation::ApplyCardRuling(const football::sim::event::CardRuling& ruling) {
  Player* player = FindPlayerById(ruling.player);
  if (player == nullptr) return;
  if (ruling.type >= 3) {
    player->GiveRedCard(ruling.effective_at);
  } else if (ruling.type == 2) {
    player->GiveYellowCard(ruling.effective_at);
  }
  event_log_.Record(football::sim::event::CardShownEvent{GetTimelineTick(),
      player->GetTeam()->GetTeamSide(), ruling.player, ruling.type >= 3});
}

void Simulation::ApplyPendingRulings() {
  for (const auto& ruling : pending_rulings_) {
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, football::sim::event::AwardGoalRuling>) {
            ApplyGoalRuling(value);
          } else if constexpr (std::is_same_v<T, football::sim::event::StopPlayRuling>) {
            StopPlay();
          } else if constexpr (std::is_same_v<T, football::sim::event::AwardRestartRuling>) {
            ApplyRestartRuling(value);
          } else if constexpr (std::is_same_v<T, football::sim::event::CardRuling>) {
            ApplyCardRuling(value);
          }
        },
        ruling);
  }
  pending_rulings_.clear();
}

void Simulation::CommitEventTransitions(football::sim::Tick now) {
  for (const auto& transition : recognizer_.transitions()) {
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, football::sim::event::PassEnded>) {
            if (value.status == football::sim::event::EventStatus::Completed) {
              event_log_.Record(football::sim::event::PassCompletedEvent{
                  now, value.id, value.team, value.passer, value.receiver});
            }
          } else if constexpr (std::is_same_v<T, football::sim::event::ShotEnded>) {
            event_log_.Record(football::sim::event::ShotEndedEvent{
                now, value.id, value.team, value.shooter, value.goal});
          }
        },
        transition);
  }
}

void Simulation::AdvanceReferee(const football::sim::rules::RefereeView& view) {
  referee_->Advance(view, options_, rng_, *rule_commands_);
}

void Simulation::ProcessReferee() {
  const auto tick = RefereeFacts();
  std::vector<Player*> active;
  if (options_.offsides) {
    teams_[first_team_]->GetActivePlayers(active);
    teams_[second_team_]->GetActivePlayers(active);
  }
  const football::sim::rules::RefereeView view{tick, active, options_.offsides};
  const bool was_restart =
      referee_->GetBuffer().active && referee_->GetBuffer().restart.has_value();
  AdvanceReferee(view);
  if (was_restart) return;
  if (tick.play_authorized && !tick.set_piece_active) {
    referee_->EvaluateOutOfPlay(view, *rule_commands_);
    referee_->CheckPendingFoul(view, *rule_commands_);
  }
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
  if (options.snapshot_capacity == 0)
    throw std::invalid_argument("snapshot capacity must be positive");
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
  pending_rulings_.clear();
  foul_assessments_.clear();
  recorded_touches_.clear();
  DiscardBodyCollisionShadow();
  body_shadow_report_ = {};
  body_physics_shadow_report_ = {};
  body_physics_shadow_latest_.reset();
  body_geometry_episodes_.Break();
  body_impact_episodes_.Break();
  recognizer_.Reset();
  event_log_.Clear();
  snapshot_step_.reset();
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
  auto touches = std::make_unique<TouchEvents>(*this);
  auto rulings = std::make_unique<RulingEvents>(*this);

  std::vector<Player*> snapshot_players;
  football::sim::observation::SnapshotMetadata metadata;
  football::sim::observation::PlayerSlotTable player_slots;
  metadata.pitch = pitch_;
  metadata.animation_library_hash = animations_->ContentHash();
  for (const auto& team : teams) {
    for (Player* player : team->GetAllPlayers()) {
      snapshot_players.push_back(player);
      metadata.player_ids.push_back(player->GetID());
      player_slots.player_ids.push_back(player->GetID());
      player_slots.team_sides.push_back(player->GetTeam()->GetTeamSide());
    }
  }
  std::map<football::ball::ColliderId,
           std::pair<football::model::PlayerId, PlayerBodyPart>>
      body_collider_owners;
  for (std::size_t slot = 0; slot < snapshot_players.size(); ++slot) {
    const PlayerBodyColliderMotionIds ids =
        PlayerBodyColliderIdsForSlot(static_cast<std::uint32_t>(slot));
    for (std::size_t part = 0; part < kPlayerBodyPartCount; ++part) {
      body_collider_owners.emplace(
          ids.values[part],
          std::make_pair(snapshot_players[slot]->GetID(),
                         static_cast<PlayerBodyPart>(part)));
    }
  }
  football::sim::observation::Snapshot scratch;
  scratch.players.resize(snapshot_players.size());
  football::sim::observation::SnapshotHistory history(options.snapshot_capacity);
  history.Reset(snapshot_players.size());
  body_shadow_predictions_.reserve(snapshot_players.size());
  body_shadow_colliders_.reserve(snapshot_players.size() * kPlayerBodyPartCount);

  // Commit. Pointer/reference borrows stay valid: the heap objects do not move
  // when their owning unique_ptrs are transferred.
  ball_ = std::move(ball);
  teams_ = std::move(teams);
  referee_ = std::move(referee);
  player_runtime_sink_ = commands.get();
  rule_commands_ = std::move(commands);
  touch_sink_ = std::move(touches);
  ruling_sink_ = std::move(rulings);
  snapshot_players_ = std::move(snapshot_players);
  snapshot_metadata_ = std::move(metadata);
  player_slots_ = std::move(player_slots);
  snapshot_scratch_ = std::move(scratch);
  snapshot_history_ = std::move(history);
  body_collider_owners_ = std::move(body_collider_owners);

  designated_possession_player_ = designated;
  ball_retainer_ = nullptr;
  last_goal_team_ = nullptr;
  last_goal_scorer_ = nullptr;
  best_possession_team_ = nullptr;
  SetMatchPhase(MatchPhase::PreMatch);
  CaptureSnapshot();
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
  try {
    StepImpl(controls);
  } catch (...) {
    snapshot_step_.reset();
    DiscardBodyCollisionShadow();
    throw;
  }
  snapshot_step_.reset();
  CaptureSnapshot();
  recognizer_.PublishTrajectories(snapshot_history_);
}

void Simulation::StepImpl(const PlayerControlSet& controls) {
  if (pending_change_of_ends_) {
    ApplyChangeOfEnds();
    pending_change_of_ends_ = false;
  }
  clock_->CountExecutedStep(phase_);
  snapshot_step_ = clock_->ExecutedTicks();
  pending_rulings_.clear();
  foul_assessments_.clear();

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
  // P4b only: capture the common ball/player frame and run read-only dynamic
  // CCD before the legacy resolver can change Ball or emit AcceptedTouch.
  BeginBodyCollisionShadow();
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
    ProcessReferee();
  }
  Vector3 previous_ball_pos = ball_->Predict(0);
  Mirror(reverse, !reverse, false);
  // Restore the processing frame even on the referee's terminal transition.
  if (Finished()) {
    DiscardBodyCollisionShadow();
    return;
  }
  if (!play_authorized_ && !referee_->RestartNeedsSimulation() &&
      (GetTimelineTick() < referee_->GetBuffer().prepare_tick ||
       referee_->GetBuffer().prepare_tick + football::sim::TickSpan{1} < GetTimelineTick())) {
    // Ceremonies execute only their placement tail; both clocks stay stopped.
    const auto admitted = clock_->Advance(football::sim::TickSpan{1}, phase_);
    UpdateRecentPossession(admitted);
    DiscardBodyCollisionShadow();
    return;
  }
  // StepBall.
  Mirror(false, false, reverse);
  ball_->Step(football::sim::TickSpan{1}, GetBallEnvironment());
  Mirror(false, false, reverse);
  if (body_physics_shadow_tick_) {
    body_physics_shadow_tick_->production = ball_->state();
    body_physics_shadow_tick_->production_observed = true;
  }

  // CaptureHistory: preserve the pre-player-processing capture and sample-zero timing.
  CaptureMentalImage();

  // StepPlayers: team estimates bracket roster-ordered actor execution.
  const auto step_team = [&](int id) {
    Team& team = *teams_[id];
    Team& opponent = *teams_[1 - id];
    football::sim::player::PrepareTeamPossession(team, opponent, play_authorized_,
        set_piece_active_, ball_retainer_, best_possession_team_);
    for (Player* actor : team.GetAllPlayers()) {
      if (actor->IsActive()) {
        actor->Process(PlayerTickFacts(*actor), mental_images_, *touch_sink_, *player_runtime_sink_);
        MeasureBodyShadowEndpoint(*actor);
      }
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
      foul_assessments_);
  // Foul verdicts now come from the contact solver's frozen assessments, at
  // the same instant and in the same pair order the trip facts used to.
  for (const football::sim::FoulAssessment& assessment : foul_assessments_) {
    referee_->AssessFoul(FindPlayerById(assessment.victim),
                         FindPlayerById(assessment.offender), assessment,
                         GetTimelineTick());
  }

  // AdvanceClock → recent possession window → goal detection/consequences.
  const auto admitted = clock_->Advance(football::sim::TickSpan{1}, phase_);
  UpdateRecentPossession(admitted);
  // Time out unresolved behavior before a possible goal confirms a shot.
  recognizer_.Advance(GetTimelineTick(),
      football::sim::event::EventView{play_authorized_, set_piece_active_, snapshot_step_, reset_sequence_});
  CommitEventTransitions(GetTimelineTick());

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
  // The referee maps each crossed goal-mouth side to the scoring opponent and
  // submits an AwardGoalRuling; no goal-mouth fact travels through the buffer.
  if (first_team_goal) {
    referee_->GoalMouthCrossed(teams_[first_team_]->GetDynamicSide(), RefereeFacts(),
                               ruling_sink_.get());
  }
  if (second_team_goal) {
    referee_->GoalMouthCrossed(teams_[second_team_]->GetDynamicSide(), RefereeFacts(),
                               ruling_sink_.get());
  }
  // Both rosters are still in the common frame used at Begin; compare only
  // after their real Process calls, never by running animation a second time.
  CompleteBodyCollisionShadow();
  Mirror(reverse, !reverse, false);
  if (IsBallInPlay()) {
    ApplyPendingRulings();
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
      event_log_.Record(football::sim::event::GoalScoredEvent{
          GetTimelineTick(), last_goal_team_->GetTeamSide(),
          last_goal_scorer_ ? last_goal_scorer_->GetID() : football::model::kInvalidPlayerId,
          own_goal});
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
  if (body_shadow_active_) ++body_shadow_report_.discarded_ticks;
  DiscardBodyCollisionShadow();
  body_geometry_episodes_.Break();
  body_impact_episodes_.Break();
  ++reset_sequence_;
  active_touch_shadow_report_.latest.clear();
  pending_rulings_.clear();
  foul_assessments_.clear();
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
  body_geometry_episodes_.Break();
  body_impact_episodes_.Break();
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

void Simulation::CaptureSnapshot() {
  const auto state = ball_->state();
  const auto ball_frame = ToHomePitchFrame(*teams_[first_team_]);
  auto& ball = snapshot_scratch_.ball;
  ball.position = ball_frame.Position(state.position);
  ball.velocity = ball_frame.Direction(state.velocity);
  // Frame conversion is a proper 180-degree rotation, not a reflection, so
  // angular velocity (an axial vector) uses the same rotation as velocity.
  ball.angular_velocity = ball_frame.Direction(state.angular_velocity);
  for (std::size_t i = 0; i < snapshot_players_.size(); ++i) {
    Player& player = *snapshot_players_[i];
    auto& dst = snapshot_scratch_.players[i];
    const auto& kinematic = player.GetKinematicState();
    const auto frame = ToHomePitchFrame(*player.GetTeam());
    dst.position = frame.Position(kinematic.position);
    dst.velocity = frame.Direction(kinematic.velocity);
    dst.facing = frame.Direction(kinematic.facing);
    dst.body_facing = frame.Direction(kinematic.bodyFacing);
    dst.active = player.IsActive();
    dst.has_possession = player.HasPossession();
    dst.animation_id = -1;
    dst.frame = 0;
    // Never dereference the missing humanoid of a never-active bench player.
    if (dst.active) {
      const auto* anim = player.GetCurrentAnim();
      if (anim) {
        dst.animation_id = anim->animationId;
        dst.frame = static_cast<std::uint32_t>(player.GetCurrentFrame());
      }
    }
  }
  snapshot_history_.Record({clock_->ExecutedTicks(), GetTimelineTick(), reset_sequence_},
                           snapshot_scratch_);
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
  EnableActiveTouchShadow(false);
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
  recognizer_.Reset();
  mental_images_.clear();
  snapshot_players_.clear();
  body_collider_owners_.clear();
  DiscardBodyCollisionShadow();
  body_physics_shadow_ball_.reset();
  body_physics_shadow_latest_.reset();
  snapshot_metadata_ = {};
  player_slots_ = {};
  snapshot_scratch_ = {};
  // Stop releases memory; Finished keeps the complete retained window alive.
  snapshot_history_ = football::sim::observation::SnapshotHistory{};
  snapshot_step_.reset();
  touch_sink_.reset();
  ruling_sink_.reset();
  player_runtime_sink_ = nullptr;
  rule_commands_.reset();
  ball_.reset();
  teams_[0].reset();
  teams_[1].reset();
  clock_.reset();
  return true;
}
