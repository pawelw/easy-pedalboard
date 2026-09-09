#pragma once

namespace ee::artifact::id
{

/** Peak Artifact's parameter ids.
 *
 * The Filter engine's controls are namespaced `flt.` so a future Ring Mod / Bit
 * Crush engine can carry its own `ring.` / `crush.` set without a name clash,
 * and so the face can bind one engine's controls through a `ParamScope` prefix
 * the way Peak Alpine's modules do. Dots are safe in an APVTS id - it is a value
 * in the state tree, not a property name.
 */

inline constexpr const char* on = "on";         // module power
inline constexpr const char* engine = "engine"; // Ring Mod / Bit Crush / Filter
inline constexpr const char* mix = "mix";       // footer dry/wet

inline constexpr const char* fltFreq = "flt.freq";
inline constexpr const char* fltQ = "flt.q";
inline constexpr const char* fltRange = "flt.range";
inline constexpr const char* fltTime = "flt.time"; // meaning set by fltSync
inline constexpr const char* fltSync = "flt.sync"; // false = ms, true = tempo
inline constexpr const char* fltWave = "flt.wave"; // Triangle / Ramp / Square
inline constexpr const char* fltStereo = "flt.stereo";

} // namespace ee::artifact::id
