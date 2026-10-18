#include "runtime/simulation.hpp"
#include "football/pitch/pitch.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "football/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "player/player_ball_contact.hpp"
#include "event/touch_query.hpp"
#include "observation/mentalimage_sampling.hpp"
#include "observation/pitch_frame.hpp"
#include "observation/world_state_builder.hpp"
#include "player/player_contact.hpp"
#include "runtime/roster_possession.hpp"
#include "referee/goal.hpp"
#include "referee/period.hpp"
#include "referee/referee.hpp"
#include "runtime/formation_build.hpp"
#include "runtime/possession.hpp"

#include "football/pitch/pitch.hpp"
using football::ball::Ball;

namespace {
// Resolve only unspecified runtime appearance, never import or invent profiles.
// Preserve the historical home-then-away draw for every declared profile, even
// profiles omitted by a smaller formation and profiles with explicit appearance.
void ResolveAppearance(football::sim::PreparedTeam& prepared,
                        football::math::Rng& rng) {
  for (auto& player : prepared.team.players) {
    const int skin_color = int(std::round(rng.Uniform(1, 4)));
    if (!player.appearance.skin_color) player.appearance.skin_color = skin_color;
  }
}

// Mirrors the retired ScenarioConfig::LeftTeamOwnsBall(): the team whose nearest
// initial formation player stands closer to the kickoff spot starts in possession.
bool LeftTeamOwnsBall(const std::vector<FormationEntry>& left,
                      const std::vector<FormationEntry>& right,
                      const football::math::Vector3& ball_position) {
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
  void SetDesignatedPlayer(int team_id, Player* receiver) override {
    simulation_.SetDesignatedPlayer(team_id, receiver);
  }
  void ResetSituation(const Vector3& position) override { simulation_.ResetSituation(position); }
  void ResetBall(const Vector3& position) override { simulation_.PlaceBallAt(position); }
  void SetPhase(MatchPhase phase) override { simulation_.SetMatchPhase(phase); }
 private:
  Simulation& simulation_;
};

football::sim::rules::RefereeTickFacts Simulation::RefereeFacts() const {
  if (!ball_) throw std::logic_error("simulation has no match");
  const football::sim::TeamView home = TeamViewOf(0);
  const football::sim::TeamView away = TeamViewOf(1);
  // The between-tick ball frame follows the first processing roster.
  const int ball_frame_team = options_.reverse_team_processing ? 1 : 0;
  return {GetTimelineTick(), phase_, play_authorized_, set_piece_active_, goal_scored_,
      *ball_, pitch_, GetRegulationTime(), home, away, first_team_,
      GetLastTouchTeam(),
      last_goal_team_,
      ToHomePitchFrame(static_cast<football::sim::TeamSide>(ball_frame_team),
                       team_frames_[ball_frame_team].dynamic_side),
      PitchFrameTransform(team_frames_[0].static_side != -1)};
}

football::sim::PlayerTickContext Simulation::PlayerTickFacts(const Player& actor) {
  if (!ball_) throw std::logic_error("simulation has no match");
  const int own_team = actor.GetTeamID();
  const int rival_team = 1 - own_team;
  const int processing_slot = own_team == second_team_ ? 1 : 0;
  // Borrowed views: roster slices plus the runtime-owned team match state.
  const PlayerTeamView own_view{own_team, RosterOf(own_team).Players(),
                                RosterOf(rival_team).Players(),
                                RosterOf(own_team).Goalie(),
                                &team_states_[own_team]};
  const PlayerTeamView opponent_view{rival_team, RosterOf(rival_team).Players(),
                                     RosterOf(own_team).Players(),
                                     RosterOf(rival_team).Goalie(),
                                     &team_states_[rival_team]};
  return {GetTimelineTick(), play_authorized_, set_piece_active_, IsBallInPlay(),
          clock_->IsHalfUnderway(), *ball_, ball_retainer_,
          designated_possession_player_, GetLastTouchPlayer(), touches_,
          restarts_.context(), pitch_, own_view, opponent_view, processing_slot,
          restarts_.NeedsSimulation(), rng_,
          active_candidate_capture_ ? active_touch_shadow_sink_.get() : nullptr,
          nullptr, ball_.get(), send_off_sink_.get()};
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

// Write-only send-off port. Player reports that it left the pitch; the
// team-level follow-up (GK role hand-over, possession re-designation) stays
// with Runtime and runs synchronously at the same per-actor point as before.
class Simulation::SendOffEvents final : public football::sim::PlayerSendOffSink {
 public:
  explicit SendOffEvents(Simulation& simulation) : simulation_(simulation) {}
  void OnPlayerSentOff(Player& player) override {
    simulation_.HandlePlayerSentOff(player);
  }
 private:
  Simulation& simulation_;
};

Player* Simulation::FindPlayerById(football::player::PlayerId id) const {
  for (const auto& roster : rosters_) {
    for (Player* player : roster.Players()) {
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

  body_shadow_contact_ = football::collision::FirstHit(
      football::collision::MovingSphere{
          {ball_->state().position, ball_config_.radius()}, ball_->state().velocity},
      body_shadow_colliders_, football::sim::kTickSeconds);
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

  std::optional<football::player::PlayerId> shadow_player;
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
    GetActiveTeamPlayers(first_team_, active);
    GetActiveTeamPlayers(second_team_, active);
  }
  const football::sim::rules::RefereeView view{tick, active, options_.offsides,
      &restarts_.context(), restarts_.InPostRestartGrace()};
  // Recognize behavior first, then let the referee judge the same touch. Both
  // read the same immutable record and the same admitted instant.
  recognizer_.Consume(touch,
      football::sim::event::EventView{tick.play_authorized, tick.set_piece_active, snapshot_step_, reset_sequence_});
  CommitEventTransitions(touch.touched_at);
  referee_->ConsumeBallTouch(touch.touched_at, touch, view, *rule_commands_, *ruling_sink_);
  CommitPendingRulings();
}

void Simulation::ApplyGoalRuling(const football::sim::event::AwardGoalRuling& ruling) {
  if (goal_scored_) return;  // One confirmed goal until the ensuing reset.
  recognizer_.OnGoalConfirmed(GetTimelineTick(), ruling.team, snapshot_step_);
  CommitEventTransitions(GetTimelineTick());
  const int team = static_cast<int>(ruling.team);
  ++score_[team];
  SetGoalScored(true);
  last_goal_team_ = team;
  bool own_goal = true;
  if (GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == team ||
      GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == team) own_goal = false;
  if (ruling.scorer) {
    last_goal_scorer_ = FindPlayerById(*ruling.scorer);
    own_goal = last_goal_scorer_->GetTeamID() != team;
  } else {
    const int touch_team = own_goal ? 1 - team : team;
    last_goal_scorer_ = football::sim::event::LastTouchPlayer(
        touches_, touch_team, rosters_[touch_team].Players());
  }
  event_log_.Record(football::sim::event::GoalScoredEvent{
      GetTimelineTick(), ruling.team,
      last_goal_scorer_ ? last_goal_scorer_->GetID() : football::player::kInvalidPlayerId,
      own_goal});
}

void Simulation::ApplyRestartRuling(
    const football::sim::event::AwardRestartRuling& ruling) {
  // Runtime owns the restart context and publishes the confirmed award event.
  restarts_.Award(ruling, GetTimelineTick(), event_log_);
}

void Simulation::ApplyBallInPlayRuling(
    const football::sim::event::BallInPlayRuling& ruling) {
  (void)ruling;
  // The rules confirmed a legal release; Runtime owns the transition and clock.
  restarts_.ConfirmRelease();
  StartBallInPlay();
}

bool Simulation::CardAlreadyCommitted(const football::sim::event::CardRuling& ruling) const {
  return std::find(committed_card_rulings_.begin(), committed_card_rulings_.end(), ruling) !=
      committed_card_rulings_.end();
}

void Simulation::ApplyCardRuling(const football::sim::event::CardRuling& ruling) {
  if (CardAlreadyCommitted(ruling)) return;
  Player* player = FindPlayerById(ruling.player);  // Validated with the whole batch.
  const bool second_yellow = ruling.type == 2 && player->CardCount() == 1;
  // Count/decision is immediate; Player still executes the send-off at its
  // original delayed card_effective_tick, never at event publication time.
  if (ruling.type == 3) player->GiveRedCard(ruling.effective_at);
  else player->GiveYellowCard(ruling.effective_at);
  event_log_.Record(football::sim::event::CardShownEvent{GetTimelineTick(),
      player->GetTeamSide(), ruling.player, ruling.type == 3 || second_yellow, second_yellow});
  committed_card_rulings_.push_back(ruling);
}

void Simulation::HandlePlayerSentOff(Player& player) {
  const int sent_off_team = player.GetTeamID();
  football::sim::VacateSlot(active_lineups_[sent_off_team], player.GetID());
  // A sent-off goalkeeper hands its role to the first remaining active player.
  if (player.GetFormationEntry().role == e_PlayerRole_GK) {
    const FormationEntry entry = player.GetFormationEntry();
    std::vector<Player*> active_players;
    for (Player* teammate : RosterOf(sent_off_team).Players()) {
      if (teammate->IsActive()) active_players.push_back(teammate);
    }
    assert(!active_players.empty());
    if (!active_players.empty()) active_players.front()->SetFormationEntry(entry);
  }
  // Runtime re-designates possession to the closest remaining teammate.
  football::sim::RefreshDesignatedTeamPossessionPlayer(
      RosterOf(sent_off_team), team_frames_[sent_off_team],
      team_states_[sent_off_team], *ball_);
}

void Simulation::InitTeamPlayers(int team_id, football::sim::Roster& roster,
                                 const football::team::Team& team,
                                 bool explicit_lineup, float ai_difficulty,
                                 std::uint8_t first_schedule_phase) {
  // Roster traversal supplies order; phases repeat every ten players. The
  // sequence (create, Add, publish facts, activate) is the historical one.
  const std::vector<FormationEntry> formation =
      BuildFormation(team, explicit_lineup);
  active_lineups_[team_id].slots = team.lineup.starters;
  std::uint8_t schedule_phase = first_schedule_phase;
  const auto count = explicit_lineup ? team.players.size() : formation.size();
  for (std::size_t i = 0; i < count; ++i) {
    std::optional<std::size_t> slot;
    for (std::size_t s = 0; s < football::team::kFormationSlots; ++s) {
      if (team.lineup.starters[s] == team.players[i].id) slot = s;
    }
    const FormationEntry entry = explicit_lineup
        ? (slot ? formation.at(*slot) : FormationEntry{}) : formation.at(i);
    auto player = std::make_unique<Player>(
        team_id, static_cast<football::sim::TeamSide>(team_id), ai_difficulty,
        team.players[i], entry, schedule_phase, *animations_, pitch_, rng_);
    schedule_phase = (schedule_phase + 1) % 10;
    Player* raw = player.get();
    roster.Add(std::move(player));
    raw->SetTeamContext(TeamFactsFor(team_id));
    if (slot) raw->Activate();
  }
  team_states_[team_id].designated_player = nullptr;
  for (Player* player : roster.Players()) {
    if (player->IsActive()) {
      team_states_[team_id].designated_player = player;
      break;
    }
  }
}

football::sim::TeamView Simulation::TeamViewOf(int team_id) const {
  return football::sim::TeamView{
      team_id,
      static_cast<football::sim::TeamSide>(team_id),
      team_frames_[team_id].dynamic_side,
      team_frames_[team_id].static_side,
      rosters_[team_id].Goalie(),
      rosters_[team_id].Players(),
      &team_states_[team_id],
      &team_frames_[team_id]};
}

PlayerTeamFacts Simulation::TeamFactsFor(int team_id) const {
  return football::sim::TeamFactsOf(team_frames_[team_id], team_states_[team_id]);
}

void Simulation::PublishTeamFacts(int team_id) {
  football::sim::PublishTeamFacts(RosterOf(team_id), team_frames_[team_id],
                                  team_states_[team_id]);
}

void Simulation::ResetTeamSituation(int team_id,
                                    const football::math::Vector3& focus_position,
                                    football::sim::Tick now) {
  football::sim::TeamMatchState& state = team_states_[team_id];
  state.time_to_ball_ms = 100;
  state.has_possession = false;
  state.possession_amount = 1.0f;
  state.fading_possession_amount = 1.0f;
  state.designated_player = RosterOf(team_id).Players().front();
  // Publication stays inside the loop, in roster order, exactly as the legacy
  // Team's previous reset owner did; batching would change the timing.
  const PlayerTeamFacts facts = TeamFactsFor(team_id);
  for (Player* player : RosterOf(team_id).Players()) {
    if (player->IsActive()) player->ResetSituation(focus_position, now);
    player->SetTeamContext(facts);
  }
}

void Simulation::RelaxTeamFatigue(int team_id, float amount) {
  for (Player* player : RosterOf(team_id).Players()) {
    if (player->IsActive()) player->RelaxFatigue(amount);
  }
}

void Simulation::MirrorTeam(int team_id) {
  football::sim::TeamFrame& frame = team_frames_[team_id];
  frame.dynamic_side *= -1;
  frame.mirrored = !frame.mirrored;
  const PlayerTeamFacts facts = TeamFactsFor(team_id);
  for (Player* player : RosterOf(team_id).Players()) {
    player->Mirror();
    player->SetTeamContext(facts);
  }
}

void Simulation::SwitchTeamEnds(int team_id) {
  football::sim::TeamFrame& frame = team_frames_[team_id];
  // Historical order: the persistent direction flips first, then the frame.
  frame.static_side = -frame.static_side;
  frame.dynamic_side = -frame.dynamic_side;
  const PlayerTeamFacts facts = TeamFactsFor(team_id);
  for (Player* player : RosterOf(team_id).Players()) {
    player->Mirror();
    player->SetTeamContext(facts);
  }
}

void Simulation::ValidateRuling(const football::sim::event::RefereeRuling& ruling) const {
  const auto valid_team = [](football::sim::TeamSide side) {
    return side == football::sim::TeamSide::Home || side == football::sim::TeamSide::Away;
  };
  const bool valid = std::visit([&](const auto& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, football::sim::event::StopPlayRuling>) {
      return true;
    } else if constexpr (std::is_same_v<T, football::sim::event::AwardGoalRuling>) {
      return valid_team(value.team) && (!value.scorer || FindPlayerById(*value.scorer));
    } else if constexpr (std::is_same_v<T, football::sim::event::AwardRestartRuling>) {
      bool supported = false;
      switch (value.kind) {
        case e_GameMode_KickOff: case e_GameMode_GoalKick: case e_GameMode_FreeKick:
        case e_GameMode_Corner: case e_GameMode_ThrowIn: case e_GameMode_Penalty:
          supported = true; break;
        default: break;
      }
      return supported && valid_team(value.team) &&
          std::isfinite(value.position.coords[0]) && std::isfinite(value.position.coords[1]) &&
          std::isfinite(value.position.coords[2]);
    } else if constexpr (std::is_same_v<T, football::sim::event::CardRuling>) {
      Player* player = FindPlayerById(value.player);
      return player && (value.type == 2 || value.type == 3) &&
          (CardAlreadyCommitted(value) || (player->IsActive() && value.effective_at >= GetTimelineTick()));
    } else if constexpr (std::is_same_v<T, football::sim::event::BallInPlayRuling>) {
      return true;
    } else {
      static_assert(std::is_same_v<T, void>, "unvalidated referee ruling");
    }
  }, ruling);
  if (!valid) throw std::logic_error("invalid referee ruling batch");
}

void Simulation::CommitRuling(const football::sim::event::RefereeRuling& ruling) {
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, football::sim::event::AwardGoalRuling>) {
          ApplyGoalRuling(value);
        } else if constexpr (std::is_same_v<T, football::sim::event::StopPlayRuling>) {
          StopPlay();
        } else if constexpr (std::is_same_v<T, football::sim::event::AwardRestartRuling>) {
          ApplyRestartRuling(value);
        } else if constexpr (std::is_same_v<T, football::sim::event::BallInPlayRuling>) {
          ApplyBallInPlayRuling(value);
        } else if constexpr (std::is_same_v<T, football::sim::event::CardRuling>) {
          ApplyCardRuling(value);
        } else {
          static_assert(std::is_same_v<T, void>, "unhandled referee ruling");
        }
      }, ruling);
}

void Simulation::CommitPendingRulings() {
  // Submit never re-enters execution. Drain only after the rule operation has
  // finished; consume each entry exactly once, in submission order.
  // A foul's stop/award/card are one logical unit. Fail loudly before any
  // runtime mutation/event, rather than silently dropping an invalid card.
  for (const auto& ruling : pending_rulings_) ValidateRuling(ruling);
  for (const auto& ruling : pending_rulings_) CommitRuling(ruling);
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

void Simulation::AdvanceRestart(const football::sim::rules::RefereeView& view) {
  const football::sim::RestartController::TickOutcome outcome =
      restarts_.Advance(view.tick, options_, rng_, *rule_commands_, event_log_);
  if (outcome.ceremonial_setup) referee_->OnCeremonialSetup();
  if (outcome.restart_taken) referee_->OnRestartTaken();
}

void Simulation::ProcessReferee() {
  const auto tick = RefereeFacts();
  std::vector<Player*> active;
  if (options_.offsides) {
    GetActiveTeamPlayers(first_team_, active);
    GetActiveTeamPlayers(second_team_, active);
  }
  football::sim::rules::RefereeView view{tick, active, options_.offsides,
      &restarts_.context(), false};
  const bool was_restart = restarts_.context().Scheduled();
  AdvanceRestart(view);
  // The grace is decremented by the restart tick; boundary settlement must
  // observe the post-advance value, exactly as the legacy single owner did.
  view.post_restart_grace = restarts_.InPostRestartGrace();
  CommitPendingRulings();
  if (was_restart) return;
  if (tick.play_authorized && !tick.set_piece_active) {
    referee_->EvaluateOutOfPlay(view, *rule_commands_, *ruling_sink_);
    CommitPendingRulings();
    referee_->CheckPendingFoul(view, *rule_commands_, *ruling_sink_);
    CommitPendingRulings();
  }
}

Simulation::Simulation() {}

const football::team::Team& Simulation::TeamOf(int id) const {
  if (!ball_) throw std::logic_error("simulation has no team");
  return teams_.at(id);
}

Simulation::~Simulation() {
  Stop();
}

void Simulation::Init(
    const football::sim::TeamSetup& home, const football::sim::TeamSetup& away,
    const football::pitch::Pitch& pitch, MatchOptions options,
    const football::ball::BallModel& ball_config) {
  if (ball_) throw std::logic_error("simulation already initialized");
  try {
  // Native duration and full-match capacity are checked before any RNG draws.
  if (options.half_duration == football::sim::TickSpan{} ||
      options.half_duration.value > std::numeric_limits<std::uint64_t>::max() / 2) {
    throw std::invalid_argument("invalid regulation duration");
  }
  if (options.snapshot_capacity == 0)
    throw std::invalid_argument("snapshot capacity must be positive");
  std::array<football::sim::PreparedTeam, 2> models{football::sim::PrepareTeam(home),
                                                   football::sim::PrepareTeam(away)};
  football::team::ValidateTeams(models[0].team, models[1].team);
  for (const auto& team : models) {
    if (std::ranges::none_of(team.team.lineup.starters,
                            [](const auto& id) { return id.has_value(); }))
      throw std::invalid_argument("runtime requires an on-field player per team");
  }
  auto& home_model = models[0];
  auto& away_model = models[1];
  // Appearance is not competition physics: isolate its seed-0 stream so a
  // second Init never depends on the previous match's residual RNG state.
  football::math::Rng appearance_rng;
  appearance_rng.Seed(0);
  ResolveAppearance(home_model, appearance_rng);
  ResolveAppearance(away_model, appearance_rng);

  // Effective formation: the same fallback BuildFormation uses (formation ->
  // tactical_formation -> players.size()), so kickoff possession agrees with the
  // rosters that are actually built.
  const std::vector<FormationEntry> left =
      BuildFormation(home_model.team, home_model.explicit_lineup);
  const std::vector<FormationEntry> right =
      BuildFormation(away_model.team, away_model.explicit_lineup);
  const auto starters_only = [](const auto& team, const auto& formation) {
    if (!team.explicit_lineup) return formation;
    std::vector<FormationEntry> result;
    for (std::size_t s = 0; s < football::team::kFormationSlots; ++s)
      if (team.team.lineup.starters[s]) result.push_back(formation.at(s));
    return result;
  };
  options.left_team_owns_ball = LeftTeamOwnsBall(
      starters_only(home_model, left), starters_only(away_model, right), options.ball_position);

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
  last_goal_team_ = -1;
  last_goal_scorer_ = nullptr;
  ball_retainer_ = nullptr;
  designated_possession_player_ = nullptr;
  best_possession_team_.reset();
  reset_sequence_ = 0;
  pending_change_of_ends_ = false;
  last_body_ball_collision_tick_ = {};
  ball_mirrored_ = false;
  touches_.Reset();
  mental_images_.clear();
  teams_ = {};
  rosters_[0].Clear();
  rosters_[1].Clear();
  active_lineups_ = {};
  for (int i = 0; i < 2; ++i) {
    team_frames_[i].dynamic_side = -1;
    team_frames_[i].static_side = i == 0 ? -1 : 1;
    team_frames_[i].mirrored = false;
    team_states_[i] = football::sim::TeamMatchState{};
    team_states_[i].time_to_ball_ms = 100;
    team_states_[i].possession_amount = 1.0f;
    team_states_[i].fading_possession_amount = 1.0f;
  }

  EnsureAnimationLibrary();
  clock_.emplace(options.half_duration);
  pending_rulings_.clear();
  committed_card_rulings_.clear();
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

  // Static Team configuration is published before actor construction. Players
  // copy their definitions, so Team owns no Player and no definition borrow.
  auto ball = std::make_unique<Ball>(ball_config_, pitch_);
  const float difficulties[] = {options.left_team_difficulty,
                               options.right_team_difficulty};
  teams_[0] = std::move(models[0].team);
  teams_[1] = std::move(models[1].team);
  // Historical creation order and scheduler stagger, including reversed order.
  InitTeamPlayers(first_team_, RosterOf(first_team_), teams_[first_team_],
                  models[first_team_].explicit_lineup, difficulties[first_team_], 0);
  InitTeamPlayers(second_team_, RosterOf(second_team_), teams_[second_team_],
                  models[second_team_].explicit_lineup, difficulties[second_team_],
                  static_cast<std::uint8_t>(rosters_[first_team_].Players().size() % 10));

  std::vector<Player*> active_players;
  GetActiveTeamPlayers(first_team_, active_players);
  Player* designated = active_players.at(0);

  auto referee = std::make_unique<Referee>();
  restarts_.BeginMatch(first_team_, options.ball_position);
  auto commands = std::make_unique<RuleCommands>(*this);
  auto touches = std::make_unique<TouchEvents>(*this);
  auto rulings = std::make_unique<RulingEvents>(*this);

  std::vector<Player*> snapshot_players;
  football::sim::observation::SnapshotMetadata metadata;
  football::sim::observation::PlayerSlotTable player_slots;
  metadata.pitch = pitch_;
  metadata.animation_library_hash = animations_->ContentHash();
  // Slot order 0 then 1, exactly as the legacy local teams array iterated.
  for (const auto& roster : rosters_) {
    for (Player* player : roster.Players()) {
      snapshot_players.push_back(player);
      metadata.player_ids.push_back(player->GetID());
      player_slots.player_ids.push_back(player->GetID());
      player_slots.team_sides.push_back(player->GetTeamSide());
    }
  }
  std::map<football::geometry::ColliderId,
           std::pair<football::player::PlayerId, PlayerBodyPart>>
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
  referee_ = std::move(referee);
  player_runtime_sink_ = commands.get();
  rule_commands_ = std::move(commands);
  touch_sink_ = std::move(touches);
  send_off_sink_ = std::make_unique<SendOffEvents>(*this);
  ruling_sink_ = std::move(rulings);
  snapshot_players_ = std::move(snapshot_players);
  snapshot_metadata_ = std::move(metadata);
  player_slots_ = std::move(player_slots);
  snapshot_scratch_ = std::move(scratch);
  snapshot_history_ = std::move(history);
  body_collider_owners_ = std::move(body_collider_owners);

  designated_possession_player_ = designated;
  ball_retainer_ = nullptr;
  last_goal_team_ = -1;
  last_goal_scorer_ = nullptr;
  best_possession_team_.reset();
  SetMatchPhase(MatchPhase::PreMatch);
  // P7a: the unified production path is the default authority. A single entry
  // point enables Prepared Tick + body CCD + baked bodies after every owner is
  // committed, so normal execution never depends on diagnostic Enable calls.
  InitializeProductionPhysics();
  CaptureSnapshot();
  } catch (...) {
    ClearComposition();
    throw;
  }
}

void Simulation::InitializeProductionPhysics() {
  if (!ball_) throw std::logic_error("simulation has no match");
  // Order: prepared tick needs body physics for passive precedence; baked
  // bodies additionally require prepared+body. Enable in dependency order so
  // each prerequisite check sees the already-established authority.
  EnableBodyPhysicsProduction(true);
  EnablePreparedTickProduction(true);
  EnableBakedBodyProduction(true);
}

void Simulation::ReleaseProductionPhysics() {
  // Tear down in reverse dependency order, then drop the per-match caches.
  // Enable(false) entries require ball_ to still be alive, so this must run
  // before Stop releases the owners.
  if (baked_body_production_) EnableBakedBodyProduction(false);
  if (prepared_tick_production_) EnablePreparedTickProduction(false);
  if (body_physics_production_) EnableBodyPhysicsProduction(false);
  preparation_ball_.reset();
  committed_ball_tick_.reset();
  pending_active_impulse_.reset();
  tick_active_candidates_.clear();
  rule_touches_.clear();
  // EnablePreparedTickProduction(false) re-derives this from the (soon dead)
  // owner map; drop it so a second Init rebuilds it cleanly.
  rule_contact_episodes_.clear();
  body_physics_production_colliders_.clear();
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
  // Never erase an uncommitted verdict at tick entry.
  CommitPendingRulings();
  foul_assessments_.clear();
  committed_ball_tick_.reset();
  rule_touches_.clear();
  // A winner is evidence for this executed Tick only, including early exits.
  pending_active_impulse_.reset();
  tick_active_candidates_.clear();

  // Frame-local controls are runtime input, not an orchestration algorithm.
  for (int team_id = 0; team_id < 2; ++team_id) {
    for (Player* player : rosters_[team_id].Players()) {
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

  // ProcessReferee: before this tick's ball/player movement, in the contact frame.
  if (football::sim::rules::PeriodElapsed(
          IsHalfUnderway(), phase_, GetRegulationTime(), options_.half_duration)) {
    EndPeriod();
  } else {
    ProcessReferee();
  }
  Vector3 previous_ball_pos = ball_->state().position;
  Mirror(reverse, !reverse, false);
  // Restore the processing frame even on the referee's terminal transition.
  if (Finished()) {
    DiscardBodyCollisionShadow();
    return;
  }
  if (ShouldSkipPlayerSimulation()) {
    // Opening/half-time ceremonies execute only their placement tail (the
    // placement instant itself is not skipped); both clocks stay stopped and
    // no player/ball simulation runs.
    const auto admitted = clock_->Advance(football::sim::TickSpan{1}, phase_);
    UpdateRecentPossession(admitted);
    DiscardBodyCollisionShadow();
    return;
  }
  RunPreparedPlayerTick();

  // UpdatePossession: retain both per-roster refreshes before arbitration.
  Mirror(first_team_ == 1, first_team_ == 0, false);
  football::sim::RefreshTeamPossession(RosterOf(first_team_),
      team_frames_[first_team_], team_states_[first_team_],
      team_states_[second_team_], *ball_, GetTimelineTick(), ball_retainer_);
  Mirror(true, true, true);
  football::sim::RefreshTeamPossession(RosterOf(second_team_),
      team_frames_[second_team_], team_states_[second_team_],
      team_states_[first_team_], *ball_, GetTimelineTick(), ball_retainer_);
  Mirror(first_team_ == 0, first_team_ == 1, true);

  const auto possession = football::sim::EvaluatePossession(
      static_cast<football::sim::TeamSide>(first_team_),
      team_states_[first_team_],
      static_cast<football::sim::TeamSide>(second_team_),
      team_states_[second_team_], designated_possession_player_, ball_retainer_);
  best_possession_team_ = possession.best_team;
  designated_possession_player_ = possession.designated_player;

  // ResolvePlayerContacts: live pair mutations, then movement sharing.
  Mirror(reverse, !reverse, false);
  std::vector<Player*> players;
  GetActiveTeamPlayers(first_team_, players);
  GetActiveTeamPlayers(second_team_, players);
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
      if (fabs(ball_->Predict(football::sim::TickSpan{1}).position.coords[0]) < pitch_.half_length() - 1.0) return false;
      return football::sim::CrossedGoalLine(
          pitch_, side, previous_ball_pos, ball_->state().position);
    };
    first_team_goal = crossed_goal(team_frames_[first_team_].dynamic_side);
    second_team_goal = crossed_goal(team_frames_[second_team_].dynamic_side);
  }
  bool goal = first_team_goal | second_team_goal;
  ball_in_goal_ |= goal;
  // The referee maps each crossed goal-mouth side to the scoring opponent and
  // submits an AwardGoalRuling; no goal-mouth fact travels through the buffer.
  if (first_team_goal) {
    referee_->GoalMouthCrossed(team_frames_[first_team_].dynamic_side, RefereeFacts(),
                               ruling_sink_.get());
  }
  if (second_team_goal) {
    referee_->GoalMouthCrossed(team_frames_[second_team_].dynamic_side, RefereeFacts(),
                               ruling_sink_.get());
  }
  // Both rosters are still in the common frame used at Begin; compare only
  // after their real Process calls, never by running animation a second time.
  CompleteBodyCollisionShadow();
  Mirror(reverse, !reverse, false);
  CommitPendingRulings();
}

bool Simulation::IsInPlay() const {
  return ball_ && play_authorized_;
}

football::sim::PlayState Simulation::CurrentPlayState() const {
  if (play_authorized_) {
    return set_piece_active_ ? football::sim::PlayState::RestartReady
                             : football::sim::PlayState::Playing;
  }
  if (restarts_.NeedsSimulation()) return football::sim::PlayState::RestartSetup;
  return football::sim::PlayState::Stopped;
}

bool Simulation::ShouldSkipPlayerSimulation() const {
  if (play_authorized_) return false;
  // A pending restart must keep simulating: the heuristic AI walks the sides
  // into position and readiness is polled from real actor state.
  if (restarts_.NeedsSimulation()) return false;
  const football::sim::Tick prepare = restarts_.context().prepare_tick;
  return GetTimelineTick() < prepare ||
      prepare + football::sim::TickSpan{1} < GetTimelineTick();
}

WorldState Simulation::Observe() const {
  if (!ball_) throw std::logic_error("simulation has no match");
  const int first_team = options_.reverse_team_processing ? 1 : 0;
  auto world = BuildWorldState({GetTimelineTick(), phase_,
      GetRegulationTime(), GetBallInPlayTime(), clock_->IsHalfUnderway(),
      IsBallInPlay(), reset_sequence_, *ball_, pitch_,
      play_authorized_, set_piece_active_, restarts_.context(), ball_retainer_,
      score_[0], score_[1], TeamViewOf(0), TeamViewOf(1),
      first_team});
  world.active_lineups = active_lineups_;
  // The binding is runtime-owned; eligibility is projected from actual actors.
  // Diagnostic deactivation also leaves a vacancy, never a compressed lineup.
  for (auto& lineup : *world.active_lineups) {
    for (auto& id : lineup.slots) if (id) {
      Player* player = FindPlayerById(*id);
      if (!player || !player->IsActive()) id.reset();
    }
  }
  return world;
}

void Simulation::TouchBall(const Vector3& impulse) {
  if (!ball_) throw std::logic_error("simulation has no match");
  // Diagnostic escape: a centre point impulse through the unified kernel.
  football::ball::BallTickInput input{};
  input.active_impulse = football::ball::BallImpulse{impulse, ball_->state().position};
  ball_->Step(input);
}
void Simulation::Mirror(bool team_0, bool team_1, bool ball) {
  if (!ball_) throw std::logic_error("simulation has no match");
  if (team_0) MirrorTeam(0);
  if (team_1) MirrorTeam(1);
  if (ball) {
    ball_mirrored_ = !ball_mirrored_;
    ball_->Mirror();
  }
  for (auto& image : mental_images_) {
    image.Mirror(team_0, team_1, ball);
  }
}

void Simulation::PlaceBallAt(const Vector3& focus) {
  // Match semantics (kickoff/free-kick placement) live here, not in Ball.
  // Position is lifted by the ball radius so the sphere rests on the ground.
  ball_->Reset(football::ball::BallState{
      focus + Vector3(0, 0, ball_config_.radius()), Vector3(0), Vector3(0),
      football::math::Quaternion{}});
}

void Simulation::ResetSituation(const Vector3& focus_position) {
  if (!ball_) throw std::logic_error("simulation has no match");
  if (body_shadow_active_) ++body_shadow_report_.discarded_ticks;
  DiscardBodyCollisionShadow();
  body_geometry_episodes_.Break();
  body_impact_episodes_.Break();
  ++reset_sequence_;
  active_touch_shadow_report_.latest.clear();
  pending_active_impulse_.reset();
  tick_active_candidates_.clear();
  committed_ball_tick_.reset();
  rule_touches_.clear();
  for (auto& [id, active] : rule_contact_episodes_) active = false;
  // Situation preparation is not a verdict-queue lifecycle boundary.
  foul_assessments_.clear();
  ball_retainer_ = nullptr;
  SetGoalScored(false);
  mental_images_.clear();
  goal_scored_ = false;
  ball_in_goal_ = false;
  touches_.Reset();
  last_goal_scorer_ = nullptr;
  best_possession_team_.reset();
  last_body_ball_collision_tick_ = {};
  PlaceBallAt(focus_position);
  ResetTeamSituation(first_team_, focus_position, GetTimelineTick());
  ResetTeamSituation(second_team_, focus_position, GetTimelineTick());
}

void Simulation::SetMatchPhase(MatchPhase newPhase) {
  phase_ = newPhase;
  if (Finished()) { EndHalf(); return; }
  RelaxTeamFatigue(first_team_, 1.0f);
  RelaxTeamFatigue(second_team_, 1.0f);
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
      options_.left_team_owns_ball ? 1 : 0);
  restarts_.OnPeriodEnded(phase_, GetTimelineTick(), options_.ball_position,
                          options_.left_team_owns_ball ? 1 : 0);
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
  // A queued active impulse belongs to the old frame; never carry it across.
  pending_active_impulse_.reset();
  tick_active_candidates_.clear();
  committed_ball_tick_.reset();
  rule_touches_.clear();
  for (auto& [id, active] : rule_contact_episodes_) active = false;
  // Permanent end change: preserve processing-roster order and canonical flags.
  SwitchTeamEnds(first_team_);
  SwitchTeamEnds(second_team_);
  ball_->Mirror();
  for (auto& image : mental_images_) {
    image.Mirror(true, true, true);
  }
}

void Simulation::UpdateRecentPossession(football::sim::TickSpan admitted) {
  if (IsBallInPlay() && !set_piece_active_) {
    // Continuous possession window in SI seconds, derived from admitted ticks.
    const float seconds = football::sim::ToSeconds(admitted);
    if (designated_possession_player_ != nullptr && designated_possession_player_->GetTeamID() == 0) {
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
  const auto ball_frame = ToHomePitchFrame(
      static_cast<football::sim::TeamSide>(first_team_), team_frames_[first_team_].dynamic_side);
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
    const auto frame = ToHomePitchFrame(player.GetTeamSide(), player.GetDynamicSide());
    dst.position = frame.Position(kinematic.position);
    dst.velocity = frame.Direction(kinematic.velocity);
    dst.facing = frame.Direction(kinematic.GetFacing());
    dst.body_facing = frame.Direction(kinematic.bodyFacing);
    dst.active = player.IsActive();
    dst.has_possession = player.HasPossession();
    dst.animation_id = -1;
    dst.frame = 0;
    // Never read the missing humanoid of a never-active bench player.
    if (dst.active) {
      dst.animation_id = player.GetCurrentAnimationId();
      dst.frame = static_cast<std::uint32_t>(player.GetCurrentFrame());
    }
  }
  snapshot_history_.Record({clock_->ExecutedTicks(), GetTimelineTick(), reset_sequence_},
                           snapshot_scratch_);
}

void Simulation::CaptureMentalImage(const football::ball::Ball* view) {
  if (mental_images_.empty() ||
      GetTimelineTick().value % football::sim::observation::kMentalImageCadence.value == 0) {
    std::vector<Player*> players;
    GetActiveTeamPlayers(first_team_, players);
    GetActiveTeamPlayers(second_team_, players);
    mental_images_.insert(mental_images_.begin(),
                          MentalImage(GetTimelineTick(), players, view ? *view : *ball_));
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

// Legacy team slot (-1 touches fall back to the first processing roster).
int Simulation::GetLastTouchTeam() const {
  return touches_.last_team != -1 ? touches_.last_team : first_team_;
}

Player* Simulation::GetLastTouchPlayer() const {
  const int team = GetLastTouchTeam();
  return football::sim::event::LastTouchPlayer(touches_, team,
                                              rosters_[team].Players());
}

void Simulation::GetActiveTeamPlayers(int team_id, std::vector<Player*>& players) {
  for (Player* player : rosters_[team_id].Players()) {
    if (player->IsActive()) players.push_back(player);
  }
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
  const bool initialized = ball_ != nullptr;
  ClearComposition();
  return initialized;
}

void Simulation::ClearComposition() {
  // One teardown for normal Stop and failed Init, including unpublished Ball
  // and partially populated teams. Production adapters stop before actors exit.
  ReleaseProductionPhysics();
  EnableActiveTouchShadow(false);
  const auto end_tick = clock_ ? clock_->now() : football::sim::Tick{};
  for (int i = 0; i < 2; ++i) {
    // Home then away, stable roster order: preserve the historical Exit/RNG window.
    for (Player* player : rosters_[i].Players()) player->Exit(end_tick);
  }
  // Drop all match-level actor borrows / indices while their owners still live.
  last_goal_team_ = -1;
  last_goal_scorer_ = nullptr;
  ball_retainer_ = nullptr;
  designated_possession_player_ = nullptr;
  best_possession_team_.reset();
  recognizer_.Reset();
  event_log_.Clear();
  pending_rulings_.clear();
  foul_assessments_.clear();
  recorded_touches_.clear();
  touches_.Reset();
  mental_images_.clear();
  snapshot_players_.clear();
  body_collider_owners_.clear();
  DiscardBodyCollisionShadow();
  body_physics_shadow_ball_.reset();
  body_physics_shadow_latest_.reset();
  snapshot_metadata_ = {};
  player_slots_ = {};
  snapshot_scratch_ = {};
  snapshot_history_ = football::sim::observation::SnapshotHistory{};
  snapshot_step_.reset();
  // Runtime restart state also borrows actors; drop it before Team ownership.
  restarts_.Clear();
  referee_.reset();
  touch_sink_.reset();
  ruling_sink_.reset();
  pending_rulings_.clear();
  committed_card_rulings_.clear();
  player_runtime_sink_ = nullptr;
  rule_commands_.reset();
  send_off_sink_.reset();
  ball_.reset();
  rosters_[0].Clear();
  rosters_[1].Clear();
  active_lineups_ = {};
  team_frames_ = {};
  team_states_ = {};
  teams_ = {};
  clock_.reset();
}
