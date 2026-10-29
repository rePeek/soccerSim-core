#include "runtime/simulation.hpp"
#include "football/pitch/pitch.hpp"
#include "player/player.hpp"

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
#include "event/touch_query.hpp"
#include "observation/mentalimage_sampling.hpp"
#include "observation/world_state_builder.hpp"
#include "runtime/player_contact.hpp"
#include "runtime/team_possession.hpp"
#include "referee/goal.hpp"
#include "referee/period.hpp"
#include "referee/referee.hpp"
#include "runtime/formation_build.hpp"
#include "runtime/possession.hpp"

#include "football/pitch/pitch.hpp"
using football::ball::Ball;

namespace {
// Resolve only unspecified runtime appearance, never import or invent profiles.
// Deterministic default: profiles without an explicit skin colour cycle through
// the four declared colours in roster order, with no RNG stream involved.
void ResolveAppearance(football::sim::PreparedTeam& prepared) {
  int index = 0;
  for (auto& player : prepared.definition.players) {
    const int skin_color = 1 + (index % 4);
    if (!player.appearance.skin_color) player.appearance.skin_color = skin_color;
    ++index;
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

football::sim::rules::RefereeTeamFact ToRefereeTeamFact(
    const football::sim::TeamView& team) {
  football::sim::rules::RefereeTeamFact fact;
  fact.team_id = team.team_id;
  fact.side = team.side;
  fact.defending_direction = team.defending_direction;
  fact.fading_possession_amount = team.fading_possession_amount();
  return fact;
}

// Derive the Referee's own value facts from the Runtime fact bundle. The
// Referee never sees a Ball, TeamView, Player or RestartContext.
football::sim::rules::RefereeTickFacts ToRefereeTickFacts(
    const football::sim::RuntimeTickFacts& runtime) {
  return football::sim::rules::RefereeTickFacts{
      runtime.now,
      runtime.phase,
      runtime.play_authorized,
      runtime.set_piece_active,
      runtime.goal_scored,
      {runtime.ball.state().position, runtime.ball.state().velocity,
       runtime.ball.config().radius(), runtime.previous_ball_position},
      runtime.pitch,
      runtime.regulation,
      ToRefereeTeamFact(runtime.home),
      ToRefereeTeamFact(runtime.away),
      runtime.first_team,
      runtime.last_touch_team,
      runtime.last_goal_team};
}

football::sim::rules::RestartRuleFacts ToRestartRuleFacts(
    const football::sim::RestartContext& restart) {
  football::sim::rules::RestartRuleFacts facts;
  facts.active = restart.active;
  facts.scheduled = restart.state.has_value();
  facts.phase = restart.Phase();
  facts.kind = restart.kind;
  facts.team_id = restart.team_id;
  facts.taker = restart.taker;
  return facts;
}

// Law 11 play classification: a controlled kick/control is a deliberate play,
// an uncontrolled contact is a deflection, and a keeper/save action is a
// deliberate save. Runtime owns this classification; the rules only consume it.
bool StopsShotAtGoal(const football::math::Vector3& ball_position,
                     const football::math::Vector3& ball_velocity,
                     const football::math::Vector3& goal) {
  const football::math::Vector3 to_goal = goal.Get2D() - ball_position.Get2D();
  if (to_goal.GetLength() > 40.0f) return false;
  if (ball_velocity.Get2D().GetLength() < 1.0f) return false;
  return ball_velocity.Get2D().GetNormalized(football::math::Vector3(1, 0, 0))
             .GetDotProduct(to_goal.GetNormalized(football::math::Vector3(1, 0, 0))) >
         0.5f;
}

// Law 11 play classification: a controlled kick/control is a deliberate play,
// an uncontrolled contact is a deflection, and a keeper action is only a
// deliberate save when the ball was actually heading at the defended goal.
football::sim::rules::BallPlayKind PlayKindOf(
    const football::sim::event::AcceptedTouch& touch,
    const football::math::Vector3& ball_position,
    const football::math::Vector3& ball_velocity,
    const football::math::Vector3& defended_goal) {
  using football::sim::rules::BallPlayKind;
  if (touch.type == e_TouchType_Accidental) return BallPlayKind::Deflection;
  switch (static_cast<e_FunctionType>(touch.action_type)) {
    case e_FunctionType_Deflect:
    case e_FunctionType_Catch:
      return StopsShotAtGoal(ball_position, ball_velocity, defended_goal)
                 ? BallPlayKind::DeliberateSave
                 : BallPlayKind::DeliberatePlay;
    case e_FunctionType_BallControl:
    case e_FunctionType_Trap:
    case e_FunctionType_ShortPass:
    case e_FunctionType_LongPass:
    case e_FunctionType_HighPass:
    case e_FunctionType_Shot:
      return BallPlayKind::DeliberatePlay;
    default:
      return BallPlayKind::Deflection;
  }
}

// Map the taker's animation action into the rule-facing release family, so the
// Referee judges a named rule value instead of an animation type.
football::sim::rules::RestartReleaseAction ToReleaseAction(e_FunctionType type) {
  using football::sim::rules::RestartReleaseAction;
  switch (type) {
    case e_FunctionType_ShortPass: return RestartReleaseAction::ShortPass;
    case e_FunctionType_LongPass: return RestartReleaseAction::LongPass;
    case e_FunctionType_HighPass: return RestartReleaseAction::HighPass;
    case e_FunctionType_Shot: return RestartReleaseAction::Shot;
    case e_FunctionType_BallControl: return RestartReleaseAction::BallControl;
    default: return RestartReleaseAction::Other;
  }
}

// Law 12 obvious-goal-scoring-opportunity radius. Deterministic geometry, not
// a probability: the victim must be close to the goal, close to the ball and
// facing it, with no covering outfield opponent.
constexpr float kDeniedGoalRange = 30.0f;
constexpr float kDeniedGoalBallRange = 10.0f;

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

football::sim::RuntimeTickFacts Simulation::RefereeFacts() const {
  if (!ball_) throw std::logic_error("simulation has no match");
  const football::sim::TeamView home = TeamViewOf(0);
  const football::sim::TeamView away = TeamViewOf(1);
  // The Referee reads the single authoritative Stadium Frame directly; there
  // is no processing-frame conversion.
  return {GetTimelineTick(), phase_, play_authorized_, set_piece_active_, goal_scored_,
      *ball_, referee_previous_ball_, pitch_, GetRegulationTime(), home, away, first_team_,
      GetLastTouchTeam(),
      last_goal_team_};
}

football::sim::PlayerTickContext Simulation::PlayerTickFacts(const Player& actor) {
  if (!ball_) throw std::logic_error("simulation has no match");
  const int own_team = actor.GetTeamID();
  const int rival_team = 1 - own_team;
  const int processing_slot = own_team == second_team_ ? 1 : 0;
  // Borrowed views: player slices plus the runtime-owned team match state.
  const PlayerTeamView own_view{own_team,
                                team_orientations_[own_team].defending_direction,
                                TeamOf(own_team).Players(),
                                TeamOf(rival_team).Players(),
                                team_states_[own_team].goalie,
                                &team_states_[own_team]};
  const PlayerTeamView opponent_view{rival_team,
                                     team_orientations_[rival_team].defending_direction,
                                     TeamOf(rival_team).Players(),
                                     TeamOf(own_team).Players(),
                                     team_states_[rival_team].goalie,
                                     &team_states_[rival_team]};
  return {GetTimelineTick(), play_authorized_, set_piece_active_, IsBallInPlay(),
          clock_->IsHalfUnderway(), *ball_, ball_retainer_,
          designated_possession_player_, GetLastTouchPlayer(), touches_,
          restarts_.context(), pitch_, own_view, opponent_view, processing_slot,
          restarts_.NeedsSimulation(),
          active_candidate_capture_ ? active_touch_shadow_sink_.get() : nullptr,
          nullptr, send_off_sink_.get(), &authorization_port_};
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
  for (const auto& team : teams_) {
    if (!team) continue;
    for (Player* player : team->Players()) {
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
  // the first processing team's frame with Ball.
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
    auto actual = player.GetKinematicState();
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
  const football::sim::RuntimeTickFacts runtime = RefereeFacts();
  const football::sim::rules::RefereeTickFacts tick = ToRefereeTickFacts(runtime);
  Player* player = FindPlayerById(touch.player);
  // Recognize behavior first, then let the referee judge the same touch. Both
  // read the same immutable record and the same admitted instant.
  recognizer_.Consume(touch,
      football::sim::event::EventView{tick.play_authorized, tick.set_piece_active, snapshot_step_, reset_sequence_});
  CommitEventTransitions(touch.touched_at);
  if (player != nullptr) {
    // Value rule facts sampled at this exact touch boundary. Offside needs the
    // active players as values; release needs the taker's action state now.
    std::vector<Player*> active_players;
    if (options_.offsides) {
      GetActiveTeamPlayers(first_team_, active_players);
      GetActiveTeamPlayers(second_team_, active_players);
    }
    std::vector<football::sim::rules::RefereePlayerFact> active;
    active.reserve(active_players.size());
    for (Player* actor : active_players) {
      active.push_back({actor->GetID(), actor->GetTeamID(), actor->GetPosition()});
    }
    football::sim::rules::BallTouchFacts facts;
    facts.now = touch.touched_at;
    facts.touch_player = touch.player;
    facts.touch_team_id = player->GetTeamID();
    facts.touch_position = player->GetPitchPosition();
    const bool touches_home = player->GetTeamID() == tick.home.team_id;
    facts.touch_team = ToRefereeTeamFact(touches_home ? runtime.home : runtime.away);
    facts.defending_team = ToRefereeTeamFact(touches_home ? runtime.away : runtime.home);
    facts.in_play = tick.play_authorized;
    facts.in_set_piece = tick.set_piece_active;
    facts.offsides_enabled = options_.offsides;
    facts.ball = {ball_->state().position, ball_->state().velocity,
                  ball_->config().radius()};
    facts.pitch = &pitch_;
    facts.all_active_players = active;
    const football::sim::RestartContext& restart = restarts_.context();
    facts.restart = ToRestartRuleFacts(restart);
    const football::math::Vector3 defended_goal(
        static_cast<float>(team_orientations_[player->GetTeamID()].defending_direction) *
            pitch_.half_length(),
        0.0f, 0.0f);
    facts.play_kind = PlayKindOf(touch, ball_->state().position,
                                 ball_->state().velocity, defended_goal);
    // Release verdict inputs, sampled at this exact boundary (never earlier).
    if (restart.active && restart.state &&
        restart.state->phase == football::sim::RestartPhase::Ready &&
        restart.taker != football::player::kInvalidPlayerId &&
        restart.taker == touch.player) {
      const auto& action = restart.taker_actor->GetSimulationActionState();
      facts.release.eligible = true;
      facts.release.kind = restart.kind;
      facts.release.action = ToReleaseAction(action.type);
      facts.release.scheduled_contact = action.contact && action.elapsed == *action.contact;
      facts.release.physical_kick = touch.physical_kick;
      facts.release.intentional_kick =
          restart.taker_actor->GetLastTouchType() == e_TouchType_Intentional_Kicked;
      facts.release.ball_speed = ball_->state().velocity.GetLength();
    }
    referee_->ConsumeBallTouch(facts, *ruling_sink_);
  }
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
        touches_, touch_team, TeamOf(touch_team).Players());
  }
  event_log_.Record(football::sim::event::GoalScoredEvent{
      GetTimelineTick(), ruling.team,
      last_goal_scorer_ ? last_goal_scorer_->GetID() : football::player::kInvalidPlayerId,
      own_goal});
}

void Simulation::ApplyRestartRuling(
    const football::sim::event::AwardRestartRuling& ruling) {
  // Runtime owns the restart context and is the single event publisher. A foul
  // or offside cause is published as its own confirmed event, never inferred
  // from the restart kind.
  for (auto& event : restarts_.Award(ruling, GetTimelineTick())) {
    event_log_.Record(std::move(event));
  }
  switch (ruling.cause.kind) {
    case football::sim::event::RestartCauseKind::Foul:
      // The offending team is the opponent of the team awarded the restart.
      event_log_.Record(football::sim::event::FoulConfirmedEvent{
          GetTimelineTick(),
          static_cast<football::sim::TeamSide>(1 - static_cast<int>(ruling.team)),
          ruling.cause.offender, ruling.cause.victim});
      break;
    case football::sim::event::RestartCauseKind::Offside:
      event_log_.Record(football::sim::event::OffsideConfirmedEvent{
          GetTimelineTick(),
          static_cast<football::sim::TeamSide>(1 - static_cast<int>(ruling.team)),
          ruling.cause.offender});
      break;
    case football::sim::event::RestartCauseKind::None:
      break;
  }
}

void Simulation::ApplyBallInPlayRuling(
    const football::sim::event::BallInPlayRuling& ruling) {
  (void)ruling;
  // The rules confirmed a legal release; Runtime owns the transition, clock
  // and the formal RestartTakenEvent.
  if (auto taken = restarts_.ConfirmRelease(GetTimelineTick())) {
    event_log_.Record(std::move(*taken));
  }
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
  // A sent-off goalkeeper hands the role to the first remaining active player.
  if (team_states_[sent_off_team].goalie == &player) {
    team_states_[sent_off_team].goalie = nullptr;
    for (Player* teammate : TeamOf(sent_off_team).Players()) {
      if (teammate->IsActive()) {
        team_states_[sent_off_team].goalie = teammate;
        break;
      }
    }
  }
  football::sim::RefreshDesignatedTeamPossessionPlayer(
      TeamOf(sent_off_team).Players(), team_orientations_[sent_off_team],
      team_states_[sent_off_team], *ball_);
}

void Simulation::InitTeamPlayers(int team_id, football::team::Team& team,
                                 bool explicit_lineup,
                                 std::uint8_t first_schedule_phase) {
  // Team traversal supplies order; phases repeat every ten players. The
  // sequence (create, Add, publish facts, activate) is the historical one.
  const football::team::TeamDefinition& definition = team.Definition();
  const std::vector<FormationEntry> formation =
      BuildFormation(definition, explicit_lineup);
  active_lineups_[team_id].slots = definition.lineup.starters;
  formations_[team_id].clear();
  std::uint8_t schedule_phase = first_schedule_phase;
  const auto count = explicit_lineup ? definition.players.size() : formation.size();
  for (std::size_t i = 0; i < count; ++i) {
    std::optional<std::size_t> slot;
    for (std::size_t s = 0; s < football::team::kFormationSlots; ++s) {
      if (definition.lineup.starters[s] == definition.players[i].id) slot = s;
    }
    const FormationEntry entry = explicit_lineup
        ? (slot ? formation.at(*slot) : FormationEntry{}) : formation.at(i);
    // Runtime resolves the initial placement; the Player only receives it.
    const int defending = team_orientations_[team_id].defending_direction;
    const Vector3 spawn_position =
        entry.position * 25 * Vector3(-defending, -defending, 0);
    auto player = std::make_unique<Player>(
        team_id, static_cast<football::sim::TeamSide>(team_id),
        definition.players[i], schedule_phase, *animations_, pitch_);
    schedule_phase = (schedule_phase + 1) % 10;
    Player* raw = player.get();
    team.AddPlayer(std::move(player));
    formations_[team_id].push_back(entry);
    if (slot) raw->Activate(spawn_position);
  }
  // Goalkeeper identity is runtime match state, not a Player-cached role.
  team_states_[team_id].goalie = nullptr;
  for (std::size_t i = 0; i < formations_[team_id].size(); ++i) {
    Player* player = team.Players()[i];
    if (player->IsActive() && formations_[team_id][i].role == e_PlayerRole_GK) {
      team_states_[team_id].goalie = player;
      break;
    }
  }
  team_states_[team_id].SetDesignatedPlayer(nullptr);
  for (Player* player : team.Players()) {
    if (player->IsActive()) {
      team_states_[team_id].SetDesignatedPlayer(player);
      break;
    }
  }
}

football::sim::TeamView Simulation::TeamViewOf(int team_id) const {
  return football::sim::TeamView{
      team_id,
      static_cast<football::sim::TeamSide>(team_id),
      team_orientations_[team_id].defending_direction,
      team_states_[team_id].goalie,
      TeamOf(team_id).Players(),
      formations_[team_id],
      &team_states_[team_id]};
}

void Simulation::ResetTeamSituation(int team_id,
                                    const football::math::Vector3& focus_position,
                                    football::sim::Tick now) {
  football::sim::TeamMatchState& state = team_states_[team_id];
  state.time_to_ball_ms = 100;
  state.has_possession = false;
  state.possession_amount = 1.0f;
  state.fading_possession_amount = 1.0f;
  state.SetDesignatedPlayer(TeamOf(team_id).Players().front());
  for (Player* player : TeamOf(team_id).Players()) {
    if (player->IsActive()) player->ResetSituation(focus_position, now);
  }
}

void Simulation::RelaxTeamFatigue(int team_id, float amount) {
  for (Player* player : TeamOf(team_id).Players()) {
    if (player->IsActive()) player->RelaxFatigue(amount);
  }
}

void Simulation::SwitchTeamEnds(int team_id) {
  // The permanent change of ends flips the only live orientation. Positions
  // are not mirrored: the kickoff ceremony that follows re-places the actors,
  // and the Stadium Frame never rotates.
  team_orientations_[team_id].defending_direction =
      -team_orientations_[team_id].defending_direction;
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

void Simulation::ValidateRulingBatch() const {
  // One logical decision per instant. Two restarts, two goals or a stop plus a
  // restart release in the same batch are a producer bug, not something the
  // executor should silently reorder.
  int restarts = 0, goals = 0, stops = 0, ball_in_play = 0;
  for (const auto& ruling : pending_rulings_) {
    if (std::holds_alternative<football::sim::event::AwardRestartRuling>(ruling)) ++restarts;
    else if (std::holds_alternative<football::sim::event::AwardGoalRuling>(ruling)) ++goals;
    else if (std::holds_alternative<football::sim::event::StopPlayRuling>(ruling)) ++stops;
    else if (std::holds_alternative<football::sim::event::BallInPlayRuling>(ruling)) ++ball_in_play;
  }
  if (restarts > 1 || goals > 1 || stops > 1 || ball_in_play > 1 ||
      (stops > 0 && ball_in_play > 0))
    throw std::logic_error("conflicting referee ruling batch");
}

void Simulation::CommitPendingRulings() {
  // Submit never re-enters execution. Drain only after the rule operation has
  // finished; consume each entry exactly once, in submission order.
  // Validation is complete before any mutation: a single invalid or conflicting
  // ruling leaves the whole queue untouched for a retry. Once validation passes
  // the handlers are simple state mutations and do not throw, so the normal
  // commit path has no partial-batch rollback.
  ValidateRulingBatch();
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
                now, value.id, value.team, value.shooter, value.status, value.goal});
          }
        },
        transition);
  }
}

football::sim::rules::RefereeTickFacts Simulation::RefereeValueFacts() const {
  return ToRefereeTickFacts(RefereeFacts());
}

void Simulation::AdvanceRestart(const football::sim::RuntimeTickFacts& facts) {
  const football::sim::RestartPlanningTable planning{
      {team_orientations_[0].defending_direction,
       &teams_[0]->Definition().set_piece_orders,
       &teams_[0]->Definition().formations, &active_lineups_[0],
       teams_[0]->Players(), formations_[0]},
      {team_orientations_[1].defending_direction,
       &teams_[1]->Definition().set_piece_orders,
       &teams_[1]->Definition().formations, &active_lineups_[1],
       teams_[1]->Players(), formations_[1]}};
  football::sim::RestartController::TickOutcome outcome =
      restarts_.Advance(facts, options_, *rule_commands_, planning);
  for (auto& event : outcome.events) event_log_.Record(std::move(event));
  if (outcome.ceremonial_setup) referee_->OnCeremonialSetup();
  if (outcome.restart_taken) referee_->OnRestartTaken();
}

void Simulation::ProcessReferee() {
  const football::sim::RuntimeTickFacts runtime = RefereeFacts();
  const football::sim::rules::RefereeTickFacts tick = ToRefereeTickFacts(runtime);
  football::sim::rules::RefereeView view{tick, false};
  const bool was_restart = restarts_.context().Scheduled();
  AdvanceRestart(runtime);
  // The grace is decremented by the restart tick; boundary settlement must
  // observe the post-advance value, exactly as the legacy single owner did.
  view.post_restart_grace = restarts_.InPostRestartGrace();
  CommitPendingRulings();
  if (was_restart) return;
  if (tick.play_authorized && !tick.set_piece_active) {
    referee_->EvaluateOutOfPlay(view, *ruling_sink_);
    CommitPendingRulings();
    referee_->CheckPendingFoul(view, *ruling_sink_);
    CommitPendingRulings();
  }
}

Simulation::Simulation()
    : authorization_port_(touches_, restarts_.context()) {}

football::team::Team& Simulation::TeamOf(int id) {
  if (!teams_.at(id)) throw std::logic_error("simulation has no team");
  return *teams_.at(id);
}

const football::team::Team& Simulation::TeamOf(int id) const {
  if (!teams_.at(id)) throw std::logic_error("simulation has no team");
  return *teams_.at(id);
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
  // Native duration and full-match capacity are checked before any actor is built.
  if (options.half_duration == football::sim::TickSpan{} ||
      options.half_duration.value > std::numeric_limits<std::uint64_t>::max() / 2) {
    throw std::invalid_argument("invalid regulation duration");
  }
  if (options.snapshot_capacity == 0)
    throw std::invalid_argument("snapshot capacity must be positive");
  std::array<football::sim::PreparedTeam, 2> models{football::sim::PrepareTeam(home),
                                                   football::sim::PrepareTeam(away)};
  football::team::ValidateTeamDefinitions(models[0].definition,
                                          models[1].definition);
  for (const auto& team : models) {
    if (std::ranges::none_of(team.definition.lineup.starters,
                            [](const auto& id) { return id.has_value(); }))
      throw std::invalid_argument("runtime requires an on-field player per team");
  }
  auto& home_model = models[0];
  auto& away_model = models[1];
  // Appearance is not competition physics and is fully deterministic.
  ResolveAppearance(home_model);
  ResolveAppearance(away_model);

  // Effective formation: the same fallback BuildFormation uses (formation ->
  // tactical_formation -> players.size()), so kickoff possession agrees with the
  // players that are actually built.
  const std::vector<FormationEntry> left =
      BuildFormation(home_model.definition, home_model.explicit_lineup);
  const std::vector<FormationEntry> right =
      BuildFormation(away_model.definition, away_model.explicit_lineup);
  const auto starters_only = [](const auto& team, const auto& formation) {
    if (!team.explicit_lineup) return formation;
    std::vector<FormationEntry> result;
    for (std::size_t s = 0; s < football::team::kFormationSlots; ++s)
      if (team.definition.lineup.starters[s]) result.push_back(formation.at(s));
    return result;
  };
  options.left_team_owns_ball = LeftTeamOwnsBall(
      starters_only(home_model, left), starters_only(away_model, right), options.ball_position);


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
  touches_.Reset();
  mental_images_.clear();
  teams_ = {};
  active_lineups_ = {};
  for (auto& formation : formations_) formation.clear();
  for (int i = 0; i < 2; ++i) {
    team_orientations_[i].defending_direction = i == 0 ? -1 : 1;
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

  // The Team entities are created before actor construction. Player copies
  // its own definition, so Team's definition storage is never borrowed.
  auto ball = std::make_unique<Ball>(ball_config_, pitch_);
  teams_[0] = std::make_unique<football::team::Team>(std::move(models[0].definition));
  teams_[1] = std::make_unique<football::team::Team>(std::move(models[1].definition));
  // Historical creation order and scheduler stagger, including reversed order.
  InitTeamPlayers(first_team_, *teams_[first_team_],
                  models[first_team_].explicit_lineup, 0);
  InitTeamPlayers(second_team_, *teams_[second_team_],
                  models[second_team_].explicit_lineup,
                  static_cast<std::uint8_t>(teams_[first_team_]->Players().size() % 10));

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
  for (const auto& team : teams_) {
    if (!team) continue;
    for (Player* player : team->Players()) {
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
  // The baked artifact is immutable once loaded (Load validates it and nothing
  // mutates the library afterwards), so it is parsed once per process and
  // shared by every Simulation. A match re-creates its Simulation freely - a
  // re-Init, a restored replay or a test harness - without paying for a 20 MB
  // deserialization each time; the content hash is identical by construction.
  static const std::shared_ptr<const AnimationLibrary> shared_library = [] {
    // Never put resource loading inside assert(): Release must load and reject
    // invalid resources too. Publish only a successfully loaded library.
    auto animations = std::make_shared<AnimationLibrary>();
    if (!animations->Load(GFOOTBALL_BAKED_ANIM_PATH)) {
      throw std::runtime_error("Simulation: cannot load baked animations: " +
                               std::string(GFOOTBALL_BAKED_ANIM_PATH));
    }
    return std::shared_ptr<const AnimationLibrary>(std::move(animations));
  }();
  animations_ = shared_library;
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
    // The only permanent end change of the match. EndPeriod only published the
    // whistle and set the flag; the physical transaction runs here, once, at
    // the entry of the first tick after it, before that tick is counted.
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
    for (Player* player : teams_[team_id]->Players()) {
      player->ClearControl();
      if (const PlayerControl* control = controls.Get(player->GetID())) {
        player->SetControl(*control);
      }
    }
  }

  // The Stadium Frame is the only frame; no processing-frame toggle exists.
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
  referee_previous_ball_ = previous_ball_pos;
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

  // UpdatePossession: retain both per-team refreshes before arbitration.
  football::sim::RefreshTeamPossession(TeamOf(first_team_).Players(),
      team_orientations_[first_team_], team_states_[first_team_],
      team_states_[second_team_], *ball_, GetTimelineTick(), ball_retainer_);
  football::sim::RefreshTeamPossession(TeamOf(second_team_).Players(),
      team_orientations_[second_team_], team_states_[second_team_],
      team_states_[first_team_], *ball_, GetTimelineTick(), ball_retainer_);

  const auto possession = football::sim::EvaluatePossession(
      static_cast<football::sim::TeamSide>(first_team_),
      team_states_[first_team_],
      static_cast<football::sim::TeamSide>(second_team_),
      team_states_[second_team_], designated_possession_player_, ball_retainer_);
  best_possession_team_ = possession.best_team;
  designated_possession_player_ = possession.designated_player;

  // ResolvePlayerContacts: live pair mutations, then movement sharing.
  std::vector<Player*> players;
  GetActiveTeamPlayers(first_team_, players);
  GetActiveTeamPlayers(second_team_, players);
  football::sim::ResolvePlayerContacts(
      {GetTimelineTick(), players, *ball_, designated_possession_player_,
       ball_retainer_, {&team_states_[0], &team_states_[1]}},
      foul_assessments_);
  // Foul verdicts now come from the contact solver's frozen assessments, at
  // the same instant and in the same pair order the trip facts used to.
  for (football::sim::FoulAssessment& assessment : foul_assessments_) {
    assessment.denied_goal_opportunity = DeniedGoalOpportunity(assessment);
    referee_->AssessFoul(assessment, GetTimelineTick(),
                         restarts_.context().active);
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
      return football::sim::CrossedGoalLine(pitch_, side, previous_ball_pos,
                                            ball_->state().position,
                                            ball_config_.radius());
    };
    first_team_goal = crossed_goal(team_orientations_[first_team_].defending_direction);
    second_team_goal = crossed_goal(team_orientations_[second_team_].defending_direction);
  }
  bool goal = first_team_goal | second_team_goal;
  ball_in_goal_ |= goal;
  // The referee maps each crossed goal-mouth side to the scoring opponent and
  // submits an AwardGoalRuling; no goal-mouth fact travels through the buffer.
  if (first_team_goal) {
    referee_->GoalMouthCrossed(team_orientations_[first_team_].defending_direction,
                               ToRefereeTickFacts(RefereeFacts()),
                               ruling_sink_.get());
  }
  if (second_team_goal) {
    referee_->GoalMouthCrossed(team_orientations_[second_team_].defending_direction,
                               ToRefereeTickFacts(RefereeFacts()),
                               ruling_sink_.get());
  }
  // Both teams are still in the common frame used at Begin; compare only
  // after their real Process calls, never by running animation a second time.
  CompleteBodyCollisionShadow();
  CommitPendingRulings();
}


bool Simulation::DeniedGoalOpportunity(
    const football::sim::FoulAssessment& fact) const {
  const int offender_team = fact.offender_team_id;
  if (offender_team < 0 || offender_team > 1) return false;
  // The goal the offender's team defends; the victim attacks it.
  const int defending = team_orientations_[offender_team].defending_direction;
  const Vector3 goal(defending * pitch_.half_length(), 0.0f, 0.0f);
  const float range = (goal - fact.victim_position.Get2D()).GetLength();
  if (range > kDeniedGoalRange) return false;
  if ((fact.victim_position.Get2D() - fact.ball_position.Get2D()).GetLength() >
      kDeniedGoalBallRange)
    return false;
  // The victim must be oriented toward the goal they are attacking.
  if (fact.victim_direction.GetDotProduct((goal - fact.victim_position.Get2D())
          .GetNormalized(0)) < 0.0f)
    return false;
  // No covering outfield opponent is closer to the goal than the victim.
  for (Player* opponent : TeamOf(offender_team).Players()) {
    if (!opponent->IsActive() || opponent->GetID() == fact.offender) continue;
    const auto offender_view = TeamViewOf(offender_team);
    if (const FormationEntry* entry = FormationEntryFor(offender_view, opponent);
        entry != nullptr && entry->role == e_PlayerRole_GK) continue;
    if ((goal - opponent->GetPosition().Get2D()).GetLength() < range) return false;
  }
  return true;
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
  // Diagnostic escape: a centre point impulse through the unified kernel, in
  // the single Stadium Frame.
  football::ball::BallTickInput input{};
  input.active_impulse = football::ball::BallImpulse{
      impulse, ball_->state().position};
  ball_->Step(input);
}

void Simulation::PlaceBallAt(const Vector3& focus) {
  // Match semantics (kickoff/free-kick placement) live here, not in Ball.
  // Position is lifted by the ball radius so the sphere rests on the ground.
  // Both the authority and its derived view are placed consistently.
  const football::ball::BallState placed{
      focus + Vector3(0, 0, ball_config_.radius()), Vector3(0), Vector3(0),
      football::math::Quaternion{}};
  ball_->Reset(placed);
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
  referee_->OnPeriodEnded(phase_);
  restarts_.OnPeriodEnded(phase_, GetTimelineTick(), options_.ball_position,
                          options_.left_team_owns_ball ? 1 : 0);
  // The period really ended: publish it once, before the phase advances.
  event_log_.Record(football::sim::event::PeriodEndedEvent{GetTimelineTick(), phase_});
  if (phase_ == MatchPhase::SecondHalf) {
    SetMatchPhase(MatchPhase::Finished);
    return;
  }
  SetMatchPhase(MatchPhase::SecondHalf);
  pending_change_of_ends_ = true;
}

void Simulation::ApplyChangeOfEnds() {
  // The half-time transaction, applied as one unit: close the old physical
  // contact episodes, discard frame-local transients and swap both teams'
  // ends. The Stadium Frame never rotates, so no stored position changes.
  body_geometry_episodes_.Break();
  body_impact_episodes_.Break();
  pending_active_impulse_.reset();
  tick_active_candidates_.clear();
  committed_ball_tick_.reset();
  rule_touches_.clear();
  for (auto& [id, active] : rule_contact_episodes_) active = false;
  // Permanent end change: preserve processing-team order and canonical flags.
  SwitchTeamEnds(first_team_);
  SwitchTeamEnds(second_team_);
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
  const auto& state = ball_->state();
  auto& ball = snapshot_scratch_.ball;
  // The Snapshot shares the single authoritative Stadium Frame.
  ball.position = state.position;
  ball.velocity = state.velocity;
  ball.angular_velocity = state.angular_velocity;
  for (std::size_t i = 0; i < snapshot_players_.size(); ++i) {
    Player& player = *snapshot_players_[i];
    auto& dst = snapshot_scratch_.players[i];
    const auto& kinematic = player.GetKinematicState();
    dst.position = kinematic.position;
    dst.velocity = kinematic.velocity;
    dst.facing = kinematic.GetFacing();
    dst.body_facing = kinematic.bodyFacing;
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
    std::vector<e_PlayerRole> roles;
    for (const int team_id : {first_team_, second_team_}) {
      const auto roster = TeamOf(team_id).Players();
      const auto& formation = formations_[team_id];
      for (std::size_t i = 0; i < roster.size(); ++i) {
        if (roster[i]->IsActive()) {
          players.push_back(roster[i]);
          roles.push_back(i < formation.size() ? formation[i].role
                                                : e_PlayerRole_CF);
        }
      }
    }
    mental_images_.insert(mental_images_.begin(),
                          MentalImage(GetTimelineTick(), players, roles,
                                      view ? *view : *ball_));
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

// Legacy team slot (-1 touches fall back to the first processing team).
int Simulation::GetLastTouchTeam() const {
  return touches_.last_team != -1 ? touches_.last_team : first_team_;
}

Player* Simulation::GetLastTouchPlayer() const {
  const int team = GetLastTouchTeam();
  return football::sim::event::LastTouchPlayer(touches_, team,
                                              TeamOf(team).Players());
}

void Simulation::GetActiveTeamPlayers(int team_id, std::vector<Player*>& players) {
  for (Player* player : TeamOf(team_id).Players()) {
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
    // Home then away, stable declaration order: preserve the historical Exit window.
    if (!teams_[i]) continue;
    for (Player* player : teams_[i]->Players()) player->Exit(end_tick);
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
  teams_[0].reset();
  teams_[1].reset();
  active_lineups_ = {};
  for (auto& formation : formations_) formation.clear();
  team_orientations_ = {};
  team_states_ = {};
  clock_.reset();
}
