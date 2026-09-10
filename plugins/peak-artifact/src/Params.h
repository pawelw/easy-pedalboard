#pragma once

namespace ee::artifact::id
{

/** Peak Artifact's parameter ids.
 *
 * Each engine's controls are namespaced - `flt.` for Filter, `crush.` for Bit
 * Crush, `ring.` for Ring Mod and `rust.` for Rust - so they never clash and so the face
 * can bind one engine's controls through a `ParamScope` prefix the way Peak
 * Alpine's modules do. Dots are safe in an APVTS id - it is a value in the state
 * tree, not a property name.
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

inline constexpr const char* crushBits = "crush.bits";     // word length, 24..1 bit
inline constexpr const char* crushRate = "crush.rate";     // sample-and-hold rate
inline constexpr const char* crushLp = "crush.lp";         // post low-pass, up = open
inline constexpr const char* crushJitter = "crush.jitter"; // hold-clock jitter

inline constexpr const char* ringFreq = "ring.freq";   // carrier frequency
inline constexpr const char* ringTweak = "ring.tweak"; // meaning set by ringMode
inline constexpr const char* ringLp = "ring.lp";       // post low-pass, up = open
inline constexpr const char* ringMode = "ring.mode";   // Earworm / Green Lantern

inline constexpr const char* rustGrind = "rust.grind"; // corrosion character, grime -> breakup
inline constexpr const char* rustTone = "rust.tone";   // post low-pass, up = open (darker in Contact)
inline constexpr const char* rustMode = "rust.mode";   // Oxide / Contact

} // namespace ee::artifact::id
