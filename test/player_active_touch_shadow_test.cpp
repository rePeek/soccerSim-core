#include <array>
#include <catch2/catch_test_macros.hpp>
#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/player/player_active_impulse.hpp"
#include "sim/touch_evidence_classification.hpp"
#include "sim/testing/simulation_access.hpp"
using football::sim::testing::SimulationAccess;
using blunted::Vector3;

TEST_CASE("active shadow computes endpoint impulse without time or constraint integration", "[active-shadow]") {
  football::model::BallConfig config;
  ActiveTouchObservation o;
  o.candidate.action = e_FunctionType_Shot;
  o.candidate.reachable = true;
  o.candidate.target_velocity = {20, 0, 3};
  o.stage = ActiveTouchStage::Executed;
  o.before = {{0, 0, 1}, {2, 0, 0}, Vector3(0), blunted::Quaternion{}};
  o.passive_endpoint = football::ball::BallState{{.02f, 0, 1}, {-5, 0, 0}, {0, 1, 0}, blunted::Quaternion{}};
  o.raw_animation_point = {.02f, -.1f, 1};
  const auto response = CompareActiveTouch(o, config);
  REQUIRE(response.impulse.has_value());
  REQUIRE((response.impulse->impulse - Vector3(25, 0, 3) * config.mass()).GetLength() < 1e-6f);
  REQUIRE(response.response.position == o.passive_endpoint->position);
  REQUIRE(std::fabs(response.response.orientation.GetDotProduct(o.passive_endpoint->orientation) - 1) < 1e-6f);
  REQUIRE(response.velocity_error < 1e-5f);
  REQUIRE(response.surface_error < 1e-6f);
  REQUIRE(response.response.angular_velocity.GetLength() > 0);
  auto mirrored = o;
  mirrored.before.position.Mirror(); mirrored.before.velocity.Mirror();
  mirrored.passive_endpoint->position.Mirror(); mirrored.passive_endpoint->velocity.Mirror();
  mirrored.passive_endpoint->angular_velocity.Mirror();
  mirrored.candidate.target_velocity.Mirror(); mirrored.raw_animation_point.Mirror();
  auto expected = response.response.angular_velocity; expected.Mirror();
  REQUIRE((CompareActiveTouch(mirrored, config).response.angular_velocity - expected).GetLength() < 1e-5f);
  for (auto stage : {ActiveTouchStage::Pending, ActiveTouchStage::Rejected, ActiveTouchStage::NoImpulse, ActiveTouchStage::Constraint}) {
    o.stage = stage;
    REQUIRE_FALSE(CompareActiveTouch(o, config).impulse.has_value());
  }
  o.stage = ActiveTouchStage::Executed;
  o.origin = ActiveTouchOrigin::RetainAnchor;
  REQUIRE_FALSE(CompareActiveTouch(o, config).impulse.has_value());
  o.origin = ActiveTouchOrigin::Scheduled;
  o.raw_animation_point = o.passive_endpoint->position;
  REQUIRE(CompareActiveTouch(o, config).fallback_point);
  ActiveTouchShadowReport report; report.latest.reserve(2);
  report.Record(o, 1, 0, config); report.Record(o, 1, 0, config);
  REQUIRE(report.duplicate_candidates == 1);
  REQUIRE(report.contending_ticks == 1);
  report.Record(o, 2, 1, config);
  REQUIRE(report.latest.size() == 1);
}

TEST_CASE("real action observation preserves snapshots touches rules RNG and end-change replay", "[active-shadow][sim]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    MatchOptions options; options.snapshot_capacity = 2; options.reverse_team_processing = reverse;
    Simulation observed, control;
    observed.Init(home, away, football::model::Pitch{}, options);
    control.Init(home, away, football::model::Pitch{}, options);
    SimulationAccess::EnableBodyPhysicsShadow(observed, true);
    SimulationAccess::EnableActiveTouchShadow(observed, true);
    football::ai::DefaultAI policy(home, away, football::model::Pitch{});
    for (int step = 0; step < 1600; ++step) {
      if (step == 800) {
        SimulationAccess::RequestChangeOfEnds(observed);
        SimulationAccess::RequestChangeOfEnds(control);
      }
      PlayerControlSet controls; policy.Update(observed.Observe(), controls);
      observed.Step(controls); control.Step(controls);
      REQUIRE(SimulationAccess::RngOf(observed).engine() == SimulationAccess::RngOf(control).engine());
      const auto& a = observed.Snapshots().Latest()->snapshot;
      const auto& b = control.Snapshots().Latest()->snapshot;
      REQUIRE(a.ball.position == b.ball.position);
      REQUIRE(a.ball.velocity == b.ball.velocity);
      REQUIRE(a.ball.angular_velocity == b.ball.angular_velocity);
      REQUIRE(a.players.size() == b.players.size());
      for (std::size_t i = 0; i < a.players.size(); ++i) {
        REQUIRE(a.players[i].position == b.players[i].position);
        REQUIRE(a.players[i].velocity == b.players[i].velocity);
        REQUIRE(a.players[i].frame == b.players[i].frame);
        REQUIRE(a.players[i].animation_id == b.players[i].animation_id);
        REQUIRE(a.players[i].has_possession == b.players[i].has_possession);
      }
      const auto& ta = SimulationAccess::RecordedTouchesOf(observed);
      const auto& tb = SimulationAccess::RecordedTouchesOf(control);
      REQUIRE(ta.size() == tb.size());
      if (!ta.empty()) {
        REQUIRE(ta.back().player == tb.back().player);
        REQUIRE(ta.back().type == tb.back().type);
        REQUIRE(ta.back().action_type == tb.back().action_type);
      }
      REQUIRE(observed.Observe().in_play == control.Observe().in_play);
    }
    const auto& report = SimulationAccess::ActiveTouchShadowReportOf(observed);
    std::uint64_t frames = 0, executed = 0;
    for (const auto& a : report.actions) { frames += a.frames; executed += a.executed; }
    REQUIRE(frames > 0); REQUIRE(executed > 0);
    REQUIRE(report.dropped_details == 0);
    // P5c-1 reconciliation: the pure model sees the same reach distance as the
    // legacy gate on every real contact frame. The residual decision difference
    // is the height gate reading the legacy Predict(0) cache instead of the
    // authoritative endpoint state, so it must be a small minority.
    REQUIRE(report.model_reach_checks > 0);
    REQUIRE(report.model_reach_gap_max == 0.0f);
    REQUIRE(report.model_reach_disagreements * 4 <= report.model_reach_checks);
    // P5d: every tick with a real candidate is decided exactly once by the
    // authority rule, using the tick's own passive evidence.
    const auto& arb = SimulationAccess::ActiveArbitrationOf(observed);
    REQUIRE(arb.ticks_with_candidates > 0);
    REQUIRE(arb.active_wins + arb.passive_wins == arb.ticks_with_candidates);
    REQUIRE(arb.candidates >= arb.ticks_with_candidates);
    // P6a: every unified contact is either a mapped body contact with an owner
    // identity or a pitch collider, and the three evidence kinds partition the
    // mapped set exactly.
    const auto& rt = SimulationAccess::BodyPhysicsShadowReportOf(observed).rule_touch;
    REQUIRE(rt.contact_ticks == rt.body_contacts + rt.unmapped_contacts);
    REQUIRE(rt.geometric_only + rt.physical_only + rt.physical_accepted == rt.body_contacts);
    REQUIRE(rt.body_contacts > 0);
    REQUIRE(SimulationAccess::ActiveTouchShadowReportOf(control).latest.empty());
    SimulationAccess::EnableActiveTouchShadow(observed, false);
    REQUIRE(SimulationAccess::ActiveTouchShadowReportOf(observed).latest.empty());
  }
}

TEST_CASE("active impulse arbitration is unique, deterministic and passive-aware", "[active-shadow]") {
  auto candidate = [](football::model::PlayerId player, PlayerBodyPart part, float speed,
                      bool passive_same_part = false) {
    ActiveImpulseCandidate c;
    c.player = player;
    c.body_part = part;
    c.impulse.impulse = Vector3(speed, 0, 0);
    c.impulse.contact_point = Vector3(.11f, 0, 0);
    c.velocity_change_magnitude = speed;
    c.passive_same_part = passive_same_part;
    return c;
  };
  REQUIRE_FALSE(ArbitrateActiveImpulse({}).has_value());
  // Same owner+part as this tick's passive impact is never doubled.
  const std::array single{candidate(7, PlayerBodyPart::LowerBody, 20.f, true)};
  REQUIRE_FALSE(ArbitrateActiveImpulse(single).has_value());
  // The faster contact wins irrespective of input order.
  const std::array a{candidate(9, PlayerBodyPart::LowerBody, 5.f),
                     candidate(4, PlayerBodyPart::UpperBody, 12.f)};
  const std::array b{a[1], a[0]};
  REQUIRE(ArbitrateActiveImpulse(a)->player == 4);
  REQUIRE(ArbitrateActiveImpulse(b)->player == 4);
  // Equal speeds fall back to the lower PlayerId, then the lower enum value
  // (UpperBody = 0 before LowerBody = 1).
  const std::array tie{candidate(8, PlayerBodyPart::UpperBody, 6.f),
                       candidate(3, PlayerBodyPart::Head, 6.f)};
  REQUIRE(ArbitrateActiveImpulse(tie)->player == 3);
  const std::array parts{candidate(3, PlayerBodyPart::LowerBody, 6.f),
                         candidate(3, PlayerBodyPart::UpperBody, 6.f)};
  REQUIRE(ArbitrateActiveImpulse(parts)->body_part == PlayerBodyPart::UpperBody);
  // A passive same-part suppression never revives a slower loser.
  const std::array mixed{candidate(1, PlayerBodyPart::LowerBody, 30.f, true),
                         candidate(2, PlayerBodyPart::UpperBody, 4.f)};
  const auto winner = ArbitrateActiveImpulse(mixed);
  REQUIRE(winner.has_value());
  REQUIRE(winner->player == 2);
}

TEST_CASE("contact-point takeover preserves linear state and derives spin from the strike",
          "[active-shadow][p5c][sim]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    MatchOptions options; options.snapshot_capacity = 2; options.reverse_team_processing = reverse;
    Simulation legacy, physical;
    legacy.Init(home, away, football::model::Pitch{}, options);
    physical.Init(home, away, football::model::Pitch{}, options);
    SimulationAccess::EnableActiveImpulseProduction(physical, true);
    football::ai::DefaultAI policy_legacy(home, away, football::model::Pitch{});
    football::ai::DefaultAI policy_physical(home, away, football::model::Pitch{});
    bool observed_spin_change = false;
    int observed_at = -1;
    for (int step = 0; step < 4000 && !observed_spin_change; ++step) {
      PlayerControlSet ca, cb;
      policy_legacy.Update(legacy.Observe(), ca);
      policy_physical.Update(physical.Observe(), cb);
      legacy.Step(ca);
      physical.Step(cb);
      const auto& a = legacy.Snapshots().Latest()->snapshot;
      const auto& b = physical.Snapshots().Latest()->snapshot;
      // Everything except the derived spin must agree up to the takeover step.
      REQUIRE(SimulationAccess::RngOf(legacy).engine() == SimulationAccess::RngOf(physical).engine());
      REQUIRE((a.ball.position - b.ball.position).GetLength() < 1e-6f);
      REQUIRE((a.ball.velocity - b.ball.velocity).GetLength() < 1e-4f);
      REQUIRE(a.players.size() == b.players.size());
      for (std::size_t i = 0; i < a.players.size(); ++i) {
        REQUIRE(a.players[i].position == b.players[i].position);
        REQUIRE(a.players[i].velocity == b.players[i].velocity);
        REQUIRE(a.players[i].animation_id == b.players[i].animation_id);
        REQUIRE(a.players[i].frame == b.players[i].frame);
      }
      const auto& ta = SimulationAccess::RecordedTouchesOf(legacy);
      const auto& tb = SimulationAccess::RecordedTouchesOf(physical);
      REQUIRE(ta.size() == tb.size());
      if (!ta.empty()) {
        REQUIRE(ta.back().player == tb.back().player);
        REQUIRE(ta.back().type == tb.back().type);
        REQUIRE(ta.back().action_type == tb.back().action_type);
      }
      if ((a.ball.angular_velocity - b.ball.angular_velocity).GetLength() > 1e-3f) {
        observed_spin_change = true;
        observed_at = step;
      }
    }
    REQUIRE(observed_spin_change);
    REQUIRE(observed_at >= 0);
  }
}

TEST_CASE("contact authority separates real passive impacts from overlap projection",
          "[active-shadow][p4d2]") {
  // No passive impact: a valid active strike owns the contact.
  REQUIRE(DecideContactAuthority({false, false, true}) == ContactAuthority::ActiveTouch);
  REQUIRE(DecideContactAuthority({false, false, false}) == ContactAuthority::None);
  // A real passive impact wins, both for the same owner+part and for another
  // player, so a strike is never double-resolved.
  REQUIRE(DecideContactAuthority({true, true, true}) == ContactAuthority::PassiveImpact);
  REQUIRE(DecideContactAuthority({true, false, true}) == ContactAuthority::PassiveImpact);
  // A pure zero-impulse overlap is not an impact (passive_impact_exists false),
  // so it can never suppress a valid active strike.
  REQUIRE(DecideContactAuthority({false, true, true}) == ContactAuthority::ActiveTouch);
  REQUIRE(DecideContactAuthority({false, true, false}) == ContactAuthority::None);
}

TEST_CASE("passive body production switch applies exactly the unified kernel result",
          "[active-shadow][p4d2][sim]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    MatchOptions options; options.snapshot_capacity = 2; options.reverse_team_processing = reverse;
    Simulation legacy, production;
    legacy.Init(home, away, football::model::Pitch{}, options);
    production.Init(home, away, football::model::Pitch{}, options);
    SimulationAccess::EnableBodyPhysicsProduction(production, true);
    football::ai::DefaultAI policy_legacy(home, away, football::model::Pitch{});
    football::ai::DefaultAI policy_production(home, away, football::model::Pitch{});
    std::uint64_t exact = 0, with_contact = 0;
    for (int step = 0; step < 4000; ++step) {
      PlayerControlSet ca, cb;
      policy_legacy.Update(legacy.Observe(), ca);
      policy_production.Update(production.Observe(), cb);
      legacy.Step(ca);
      production.Step(cb);
      const auto& evidence = SimulationAccess::BodyPhysicsShadowLatestOf(production);
      if (!evidence) continue;
      REQUIRE(evidence->production_observed);
      // The production ball must be exactly the unified kernel result: the new
      // path is the only authority, so no legacy impulse can be added twice.
      REQUIRE(evidence->production.position == evidence->unified.state.position);
      REQUIRE(evidence->production.velocity == evidence->unified.state.velocity);
      REQUIRE(evidence->production.angular_velocity == evidence->unified.state.angular_velocity);
      ++exact;
      if (!evidence->unified.contacts.empty()) ++with_contact;
    }
    REQUIRE(exact > 0);
    // The native tape must actually exercise passive body contact for the check
    // to be meaningful.
    REQUIRE(with_contact > 0);
    // The frozen legacy simulation is untouched by the switch existing.
    REQUIRE(SimulationAccess::BodyPhysicsShadowLatestOf(legacy) == std::nullopt);
  }
}

TEST_CASE("touch evidence keeps geometry impact and rule fact separate",
          "[active-shadow][p6]") {
  using football::sim::ClassifyTouchEvidence;
  using football::sim::NeedsAuthoritativeRecord;
  using football::sim::TouchEvidence;
  REQUIRE(ClassifyTouchEvidence({}) == TouchEvidence::None);
  REQUIRE(ClassifyTouchEvidence({true, false, false}) == TouchEvidence::GeometricOverlapOnly);
  REQUIRE(ClassifyTouchEvidence({true, true, false}) == TouchEvidence::PhysicalImpactOnly);
  REQUIRE(ClassifyTouchEvidence({true, false, true}) == TouchEvidence::AcceptedTouchOnly);
  REQUIRE(ClassifyTouchEvidence({true, true, true}) == TouchEvidence::PhysicalImpactAccepted);
  // A zero-impulse projection must never be promoted to a rule touch.
  REQUIRE(ClassifyTouchEvidence({true, false, false}) != TouchEvidence::AcceptedTouchOnly);
  REQUIRE_FALSE(NeedsAuthoritativeRecord(TouchEvidence::None));
  REQUIRE_FALSE(NeedsAuthoritativeRecord(TouchEvidence::GeometricOverlapOnly));
  REQUIRE(NeedsAuthoritativeRecord(TouchEvidence::AcceptedTouchOnly));
  REQUIRE(NeedsAuthoritativeRecord(TouchEvidence::PhysicalImpactOnly));
  REQUIRE(NeedsAuthoritativeRecord(TouchEvidence::PhysicalImpactAccepted));
}

TEST_CASE("all four production switch combinations stay consistent and deterministic",
          "[active-shadow][p4d2][p5c][sim]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    for (int mask = 0; mask < 4; ++mask) {
      const bool active = (mask & 1) != 0;
      const bool passive = (mask & 2) != 0;
      MatchOptions options; options.snapshot_capacity = 2; options.reverse_team_processing = reverse;
      Simulation run, replay;
      run.Init(home, away, football::model::Pitch{}, options);
      replay.Init(home, away, football::model::Pitch{}, options);
      if (active) {
        SimulationAccess::EnableActiveImpulseProduction(run, true);
        SimulationAccess::EnableActiveImpulseProduction(replay, true);
      }
      if (passive) {
        SimulationAccess::EnableBodyPhysicsProduction(run, true);
        SimulationAccess::EnableBodyPhysicsProduction(replay, true);
      }
      football::ai::DefaultAI policy_a(home, away, football::model::Pitch{});
      football::ai::DefaultAI policy_b(home, away, football::model::Pitch{});
      std::uint64_t evidence_ticks = 0;
      for (int step = 0; step < 1500; ++step) {
        PlayerControlSet ca, cb;
        policy_a.Update(run.Observe(), ca);
        policy_b.Update(replay.Observe(), cb);
        run.Step(ca);
        replay.Step(cb);
        // Deterministic for every combination: an identical run reproduces it.
        REQUIRE(SimulationAccess::RngOf(run).engine() == SimulationAccess::RngOf(replay).engine());
        const auto& a = run.Snapshots().Latest()->snapshot;
        const auto& b = replay.Snapshots().Latest()->snapshot;
        REQUIRE(a.ball.position == b.ball.position);
        REQUIRE(a.ball.velocity == b.ball.velocity);
        REQUIRE(a.ball.angular_velocity == b.ball.angular_velocity);
        // A production endpoint is passive CCD plus this Tick's accepted input,
        // not a diagnostic passive state substituted for the actual commit.
        if (passive) {
          const auto& evidence = SimulationAccess::BodyPhysicsShadowLatestOf(run);
          if (evidence) {
            auto expected = evidence->unified.state;
            const auto& commit = SimulationAccess::CommittedBallTickOf(run);
            if (commit && commit->active_impulse)
              expected = football::ball::ApplyImpulseAtPoint(expected,
                  commit->active_impulse->impulse, commit->active_impulse->contact_point,
                  SimulationAccess::BallOf(run)->config());
            if (commit && commit->endpoint_constraint) {
              REQUIRE_FALSE(commit->active_impulse.has_value());
              expected.position = commit->endpoint_constraint->position;
              expected.velocity = commit->endpoint_constraint->velocity;
              expected.angular_velocity = Vector3(0);
            }
            REQUIRE(evidence->production.position == expected.position);
            REQUIRE(evidence->production.velocity == expected.velocity);
            REQUIRE(evidence->production.angular_velocity == expected.angular_velocity);
            ++evidence_ticks;
          }
        }
      }
      if (passive) REQUIRE(evidence_ticks > 0);
      // Numerically stable for every combination.
      const auto& ball = run.Snapshots().Latest()->snapshot.ball;
      REQUIRE(std::isfinite(ball.position.GetLength()));
      REQUIRE(std::isfinite(ball.velocity.GetLength()));
      REQUIRE(std::isfinite(ball.angular_velocity.GetLength()));
      REQUIRE(ball.position.GetLength() < 200.0f);
    }
  }
}

TEST_CASE("active candidate arbitration is not gated on the diagnostic shadow",
          "[active-shadow][p5e][sim]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    MatchOptions options; options.snapshot_capacity = 2; options.reverse_team_processing = reverse;
    Simulation simulation;
    simulation.Init(home, away, football::model::Pitch{}, options);
    // Only the production switch: no active-touch report, no body shadow report.
    SimulationAccess::EnableActiveImpulseProduction(simulation, true);
    REQUIRE(SimulationAccess::ActiveTouchShadowReportOf(simulation).latest.empty());
    football::ai::DefaultAI policy(home, away, football::model::Pitch{});
    int committed_strikes = 0;
    for (int step = 0; step < 4000; ++step) {
      PlayerControlSet controls;
      policy.Update(simulation.Observe(), controls);
      simulation.Step(controls);
      const auto& commit = SimulationAccess::CommittedBallTickOf(simulation);
      if (commit && commit->active_impulse) {
        ++committed_strikes;
        const auto& winner = SimulationAccess::PendingActiveImpulseOf(simulation);
        REQUIRE(winner.has_value());
        REQUIRE(winner->velocity_change_magnitude > 0);
        REQUIRE(winner->impulse.impulse == commit->active_impulse->impulse);
        REQUIRE(winner->impulse.contact_point == commit->active_impulse->contact_point);
      } else {
        REQUIRE_FALSE(SimulationAccess::PendingActiveImpulseOf(simulation).has_value());
      }
    }
    // Unlike a retained statistical winner, these impulses entered this Tick's Step.
    REQUIRE(committed_strikes > 0);
    // The report stayed disabled the whole time.
    REQUIRE(SimulationAccess::ActiveTouchShadowReportOf(simulation).latest.empty());
    REQUIRE(SimulationAccess::ActiveTouchShadowReportOf(simulation).candidate_ticks == 0);
  }
}

TEST_CASE("prepared tick commits the arbitrated impulse inside its sole Ball Step", "[prepared-tick][sim]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    MatchOptions options; options.snapshot_capacity = 2; options.reverse_team_processing = reverse;
    Simulation a, b;
    a.Init(home, away, football::model::Pitch{}, options);
    b.Init(home, away, football::model::Pitch{}, options);
    SimulationAccess::EnablePreparedTickProduction(a, true);
    SimulationAccess::EnablePreparedTickProduction(b, true);
    football::ai::DefaultAI policy(home, away, football::model::Pitch{});
    int active_steps = 0;
    for (int step = 0; step < 4000; ++step) {
      PlayerControlSet controls; policy.Update(a.Observe(), controls);
      a.Step(controls); b.Step(controls);
      REQUIRE(SimulationAccess::RngOf(a).engine() == SimulationAccess::RngOf(b).engine());
      const auto& sa = a.Snapshots().Latest()->snapshot;
      const auto& sb = b.Snapshots().Latest()->snapshot;
      REQUIRE(sa.ball.position == sb.ball.position);
      REQUIRE(sa.ball.velocity == sb.ball.velocity);
      REQUIRE(sa.ball.angular_velocity == sb.ball.angular_velocity);
      const auto& commit = SimulationAccess::CommittedBallTickOf(a);
      if (commit) {
        REQUIRE(commit->state.position == SimulationAccess::BallOf(a)->state().position);
        REQUIRE(commit->state.velocity == SimulationAccess::BallOf(a)->state().velocity);
        REQUIRE(commit->state.angular_velocity == SimulationAccess::BallOf(a)->state().angular_velocity);
        if (commit->active_impulse) ++active_steps;
      }
      for (std::size_t i = 0; i < sa.players.size(); ++i) {
        REQUIRE(sa.players[i].position == sb.players[i].position);
        REQUIRE(sa.players[i].velocity == sb.players[i].velocity);
        REQUIRE(sa.players[i].animation_id == sb.players[i].animation_id);
        REQUIRE(sa.players[i].frame == sb.players[i].frame);
      }
    }
    REQUIRE(active_steps > 0);
    REQUIRE(SimulationAccess::ActiveTouchShadowReportOf(a).latest.empty());
  }
}

TEST_CASE("prepared ON ON publishes reviewed CCD touch provenance through the referee sink", "[prepared-tick][rule-touch][sim]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  for (bool reverse : {false, true}) {
    MatchOptions options; options.snapshot_capacity = 2; options.reverse_team_processing = reverse;
    Simulation s; s.Init(home, away, football::model::Pitch{}, options);
    SimulationAccess::EnableBodyPhysicsProduction(s, true);
    SimulationAccess::EnablePreparedTickProduction(s, true);
    football::ai::DefaultAI policy(home, away, football::model::Pitch{});
    int body_touches = 0, strikes = 0;
    for (int step = 0; step < 4000; ++step) {
      PlayerControlSet controls; policy.Update(s.Observe(), controls); s.Step(controls);
      const auto& touches = SimulationAccess::RuleTouchesOf(s);
      REQUIRE(touches.size() <= 1);
      for (const auto& touch : touches) {
        REQUIRE(touch.step == s.Snapshots().Latest()->stamp.step_index);
        const auto& records = SimulationAccess::RecordedTouchesOf(s);
        REQUIRE_FALSE(records.empty());
        REQUIRE(records.back().player == touch.accepted.player);
        REQUIRE(records.back().type == touch.accepted.type);
        if (touch.source == football::sim::event::RuleTouchSource::BodyCCD) {
          ++body_touches;
          REQUIRE(touch.contact.has_value());
          REQUIRE(touch.contact->normal_impulse > 0);
          REQUIRE(touch.accepted.preserve_opponent_offside);
          REQUIRE_FALSE(SimulationAccess::CommittedBallTickOf(s)->active_impulse.has_value());
        } else if (touch.source == football::sim::event::RuleTouchSource::PreparedAction) {
          ++strikes;
          REQUIRE(SimulationAccess::CommittedBallTickOf(s)->active_impulse.has_value());
        }
      }
    }
    REQUIRE(body_touches > 0);
    REQUIRE(strikes > 0);
  }
}
