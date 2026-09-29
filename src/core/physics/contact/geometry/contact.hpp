#ifndef _HPP_CORE_CONTACT_CONTACT
#define _HPP_CORE_CONTACT_CONTACT

#include "core/math/vector3.hpp"

namespace football_sim::contact {

// 7G-1: Contact is a pure geometric fact. Its semantics are frozen here so
// every consumer (Physics, Action, Rules/Events, Replay) shares one meaning.
//
//   point        single world-space contact point on the touching/overlap
//                surface between the two colliders
//   normal       unit vector pointing from A to B
//   penetration  rA + rB - distance (planar):
//                  separated  -> no Contact at all (std::nullopt)
//                  touching   -> Contact(penetration = 0)
//                  overlap    -> Contact(penetration > 0)
//                  coincident center -> deterministic fallback normal
//
// The struct carries no identities, no gameplay bias and no solver result.
// Detection decides "what happened geometrically"; resolution decides "how
// physical state responds". Keeping those two apart is the whole point of 7G.
struct Contact {
  football_sim::math::Vector3 point = football_sim::math::Vector3(0);
  football_sim::math::Vector3 normal = football_sim::math::Vector3(0, 0, 0);  // A -> B
  float penetration = 0.0f;
};

}  // namespace football_sim::contact

#endif  // _HPP_CORE_CONTACT_CONTACT