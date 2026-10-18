#pragma once

#include <optional>
#include <span>
#include <vector>

#include "football/team/selection.hpp"

namespace football::team {

// Appointment order, not a guarantee that the captain is currently on field.
// Runtime resolves the current captain against live eligibility; that result is
// match state and is not written back here.
struct Leadership {
  std::optional<football::player::PlayerId> captain;
  std::vector<football::player::PlayerId> vice_captains;
};

// Captain if still eligible, otherwise the first eligible vice-captain. Only
// then does the explicit fallback policy apply.
std::optional<football::player::PlayerId> SelectCaptain(
    const Leadership& leadership, std::span<const EligiblePlayer> candidates,
    SelectionFallback fallback);

}  // namespace football::team
