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
inline constexpr const char* engine = "engine"; // Spring / Shimmer / Modern
inline constexpr const char* mix = "mix";       // footer dry/wet

// The Shimmer engine: BitBit Reverb's FDN, which this pedal was on its own
// before it became a module. Still `space.`, the name it had until Modern
// arrived beside it - a saved session keys on the id, so renaming it would
// have lost every setting anyone made on it. Decay, the FDN's own Shimmer
// feedback and Reso are fixed inside ee::fx::ReverbModule::setShimmer - not
// knobs any more (see its doc comment) - so what is left is which octave the
// feedback stacks at, the two cuts, and Damping.
inline constexpr const char* spaceOctave = "space.octave";
inline constexpr const char* spaceLoCut = "space.locut";
inline constexpr const char* spaceHiCut = "space.hicut";
inline constexpr const char* spaceDamping = "space.damping";

// BitBit Spring's tank.
inline constexpr const char* springDecay = "spring.decay";
inline constexpr const char* springTension = "spring.tension";
inline constexpr const char* springLoCut = "spring.locut";
inline constexpr const char* springHiCut = "spring.hicut";

// The Modern engine (ee::dsp::SpaceReverb), appended last for the same reason,
// in the order its knobs sit on the face.
inline constexpr const char* modernDecay = "modern.decay";
inline constexpr const char* modernPredelay = "modern.predelay";
inline constexpr const char* modernDamping = "modern.damping";
inline constexpr const char* modernLoCut = "modern.locut";
inline constexpr const char* modernHiCut = "modern.hicut";

} // namespace ee::reverb::id
