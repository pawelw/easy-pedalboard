#pragma once

namespace ee::reverb::id
{

/** BitBit Reverb's parameter ids.
 *
 * BitBit Alpine's Reverb module's, exactly, with the `rev.` taken off - which is
 * what lets one `ReverbFace` bind to both plugins through a ParamScope prefix.
 * The one Alpine id with no counterpart here is `rev.level`: that trim is
 * Alpine's module chrome, not part of the effect. Dots are safe in an APVTS id
 * - it is a value in the state tree, not a property name.
 */

inline constexpr const char* on = "on";         // module power
inline constexpr const char* engine = "engine"; // Space / Spring
inline constexpr const char* mix = "mix";       // footer dry/wet

// BitBit Reverb's FDN, which this pedal was on its own before it became a module.
inline constexpr const char* spaceDecay = "space.decay";
inline constexpr const char* spaceShimmer = "space.shimmer";
inline constexpr const char* spaceLoCut = "space.locut";
inline constexpr const char* spaceReso = "space.reso";

// BitBit Spring's tank.
inline constexpr const char* springDecay = "spring.decay";
inline constexpr const char* springTension = "spring.tension";
inline constexpr const char* springLoCut = "spring.locut";

// Appended after Spring, not slotted into the Space block above: AU addresses
// parameters by index, and BitBit Reverb has shipped, so an existing param's
// index cannot move. See createParameterLayout().
inline constexpr const char* spacePredelay = "space.predelay";
inline constexpr const char* spaceDamping = "space.damping";

} // namespace ee::reverb::id
