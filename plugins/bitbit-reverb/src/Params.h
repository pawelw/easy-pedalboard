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
inline constexpr const char* engine = "engine"; // Spring / Shimmer / Studio
inline constexpr const char* mix = "mix";       // footer dry/wet

// The Shimmer engine: BitBit Reverb's FDN, which this pedal was on its own
// before it became a module. Still `space.`, the name it had until Studio
// arrived beside it - a saved session keys on the id, so renaming it would
// have lost every setting anyone made on it. Decay is a knob again, clamped
// to ee::fx::ReverbModule::kMinShimmerDecay..kMaxShimmerDecay; the FDN's own
// Shimmer feedback and Reso stay fixed inside ee::fx::ReverbModule::setShimmer
// (see its doc comment) - so what is left is Decay, which octave the feedback
// stacks at, the two cuts, and Damping.
inline constexpr const char* spaceDecay = "space.decay";
inline constexpr const char* spaceOctave = "space.octave";
inline constexpr const char* spaceLoCut = "space.locut";
inline constexpr const char* spaceHiCut = "space.hicut";
inline constexpr const char* spaceDamping = "space.damping";

// BitBit Spring's tank.
inline constexpr const char* springDecay = "spring.decay";
inline constexpr const char* springTension = "spring.tension";
inline constexpr const char* springLoCut = "spring.locut";
inline constexpr const char* springHiCut = "spring.hicut";

// The Studio engine (ee::dsp::SpaceReverb), appended last for the same reason,
// in the order its knobs sit on the face.
inline constexpr const char* studioDecay = "studio.decay";
inline constexpr const char* studioSize = "studio.size";
inline constexpr const char* studioPredelay = "studio.predelay";
inline constexpr const char* studioDamping = "studio.damping";
inline constexpr const char* studioLoCut = "studio.locut";
inline constexpr const char* studioHiCut = "studio.hicut";

} // namespace ee::reverb::id
