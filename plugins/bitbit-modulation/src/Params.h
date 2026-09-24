#pragma once

namespace ee::modulation::id
{

/** BitBit Modulation's parameter ids.
 *
 * BitBit Alpine's Modulation module's, exactly, with the `mod.` taken off - which
 * is what lets one `ModulationFace` bind to both plugins through a ParamScope
 * prefix. The one Alpine id with no counterpart here is `mod.level`: that trim
 * is Alpine's module chrome, not part of the effect. Dots are safe in an APVTS
 * id - it is a value in the state tree, not a property name.
 */

inline constexpr const char* on = "on";         // module power
inline constexpr const char* engine = "engine"; // Tape / Tremolo / Chorus / Phaser / Filter
inline constexpr const char* mix = "mix";       // footer dry/wet - Tape does not use it
inline constexpr const char* tone = "tone";     // footer tilt, every engine, flat at 0

inline constexpr const char* tapeSat = "tape.sat";
inline constexpr const char* tapeFlutter = "tape.flutter";
inline constexpr const char* tapeWear = "tape.wear";
inline constexpr const char* tapeNoise = "tape.noise";
inline constexpr const char* tapeStereo = "tape.stereo"; // one transport or two

inline constexpr const char* tremAmount = "trem.amount";
inline constexpr const char* tremRate = "trem.rate"; // meaning set by tremSync
inline constexpr const char* tremSync = "trem.sync"; // false = ms, true = tempo
inline constexpr const char* tremShape = "trem.shape";
inline constexpr const char* tremTube = "trem.tube";
inline constexpr const char* tremAttack = "trem.attack"; // per-note swell, ms, 0 = off

inline constexpr const char* chorusRate = "chorus.rate";
inline constexpr const char* chorusDepth = "chorus.depth";
inline constexpr const char* chorusPhase = "chorus.phase";

inline constexpr const char* phaseRate = "phase.rate";
inline constexpr const char* phaseDepth = "phase.depth";

// BitBit Wah's engine as a swept filter.
inline constexpr const char* filterFreq = "filter.freq";
inline constexpr const char* filterQ = "filter.q";
inline constexpr const char* filterRange = "filter.range";
inline constexpr const char* filterTime = "filter.time"; // meaning set by filterSync
inline constexpr const char* filterSync = "filter.sync"; // false = ms, true = tempo
inline constexpr const char* filterWave = "filter.wave"; // Triangle / Ramp / Square
inline constexpr const char* filterStereo = "filter.stereo";

} // namespace ee::modulation::id
