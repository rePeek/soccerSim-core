// ANIM-1 A9.4: the animation interrupt reasons.
//
// These were declared inside HumanoidBase's header, but an interrupt is an
// execution-scheduling reason owned by the Player's action flow, not a Humanoid
// type. They live here so Player, PlayerMotion and the selection code can name
// them without depending on a Humanoid header.
#ifndef FOOTBALL_SIM_PLAYER_PLAYER_INTERRUPT_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_INTERRUPT_HPP

enum e_InterruptAnim {
  e_InterruptAnim_None,
  e_InterruptAnim_Switch,
  e_InterruptAnim_Sliding,
  e_InterruptAnim_Bump,
  e_InterruptAnim_Trip,
  e_InterruptAnim_Cheat,
  e_InterruptAnim_Cancel,
  e_InterruptAnim_ReQueue
};

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_INTERRUPT_HPP
