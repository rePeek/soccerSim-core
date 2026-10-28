// Shared contact classification. Player produces intentional touches; Contact
// also produces accidental touches. Event/Referee consume the same vocabulary.
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
