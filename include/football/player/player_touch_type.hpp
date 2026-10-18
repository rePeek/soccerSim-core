// Touch classification. The actor that produces a touch owns the vocabulary;
// Event/Referee consume it. Kept in a Player-owned value header so the Player
// module never has to include the Event implementation module.
#ifndef FOOTBALL_PLAYER_PLAYER_TOUCH_TYPE_HPP
#define FOOTBALL_PLAYER_PLAYER_TOUCH_TYPE_HPP

enum e_TouchType {
  e_TouchType_Intentional_Kicked,  // goalies can't touch this
  e_TouchType_Intentional_Nonkicked,  // headers and such
  e_TouchType_Accidental,  // collisions
  e_TouchType_None,
  e_TouchType_SIZE
};

#endif  // FOOTBALL_PLAYER_PLAYER_TOUCH_TYPE_HPP
