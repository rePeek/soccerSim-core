#ifndef FOOTBALL_APP_INPUT_GRF_ACTION_HPP
#define FOOTBALL_APP_INPUT_GRF_ACTION_HPP

namespace football::app::grf {
// Stable wire numbers; this protocol is not part of the simulation contract.
enum class Action {
  Idle = 0,
  Left = 1, TopLeft = 2, Top = 3, TopRight = 4,
  Right = 5, BottomRight = 6, Bottom = 7, BottomLeft = 8,
  LongPass = 9, HighPass = 10, ShortPass = 11, Shot = 12,
  KeeperRush = 13, Sliding = 14, Pressure = 15, TeamPressure = 16,
  Switch = 17, Sprint = 18, Dribble = 19,
  ReleaseDirection = 20,
  ReleaseLongPass = 21, ReleaseHighPass = 22, ReleaseShortPass = 23,
  ReleaseShot = 24, ReleaseKeeperRush = 25, ReleaseSliding = 26,
  ReleasePressure = 27, ReleaseTeamPressure = 28, ReleaseSwitch = 29,
  ReleaseSprint = 30, ReleaseDribble = 31,
  BuiltinAI = 32,
};
}  // namespace football::app::grf
#endif  // FOOTBALL_APP_INPUT_GRF_ACTION_HPP
