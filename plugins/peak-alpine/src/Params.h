#pragma once

namespace ee::alpine::id
{

/** Peak Alpine's parameter ids, namespaced by module.
 *
 * Dotted rather than run together, and prefixed rather than flat, for two
 * reasons. Three modules carry a Mix, a Level and a Low Cut between them, so a
 * flat set would need names like `revspringlocut` that nobody can read. And the
 * face resolves ids through a prefix (`ParamScope` in
 * `@synthpeak/pedal-ui/juce`), which is what lets one `DelayFace` component
 * drive Peak Delay's bare `mix` and this plugin's `dly.mix` without knowing
 * which host it is in.
 *
 * Dots are safe: an APVTS id is a *value* in the state tree, not a property
 * name, and VST3 hashes it while AU addresses by index.
 *
 * **The Delay and Artifact leaf names are their pedal's own, exactly.** That is
 * not tidiness, it is the mechanism - drop the `dly.` (or `art.`) and you have
 * that pedal's parameter list, which is what makes one prefix enough to bind one
 * face to two plugins. The two exceptions are `dly.level` and `art.level`: those
 * are this plugin's module chrome, the same Level trim `mod.level` and
 * `rev.level` are, and they are driven from Peak Alpine's own header rather than
 * from the shared face - so the face still speaks only its pedal's own names.
 */

// ------------------------------------------------------------------- global
inline constexpr const char* inGain = "ingain";
inline constexpr const char* outGain = "outgain";
inline constexpr const char* on = "on";

// ----------------------------------------------------------------- artifact
// The first module: ee::fx::ArtifactModule, which is what Peak Artifact runs.
// The leaf names are that pedal's own, exactly - drop the `art.` and you have
// its parameter list, which is what lets one `ArtifactFace` bind to both.
inline constexpr const char* artOn = "art.on";
inline constexpr const char* artEngine = "art.engine"; // Ring Mod / Bit Crush / Filter / Rust / Amp
inline constexpr const char* artLevel = "art.level";   // module chrome, not Peak Artifact's
inline constexpr const char* artMix = "art.mix";
inline constexpr const char* artFltFreq = "art.flt.freq";
inline constexpr const char* artFltQ = "art.flt.q";
inline constexpr const char* artFltRange = "art.flt.range";
inline constexpr const char* artFltTime = "art.flt.time"; // meaning set by artFltSync
inline constexpr const char* artFltSync = "art.flt.sync"; // false = ms, true = tempo
inline constexpr const char* artFltWave = "art.flt.wave"; // Triangle / Ramp / Square
inline constexpr const char* artFltStereo = "art.flt.stereo";
inline constexpr const char* artCrushBits = "art.crush.bits";
inline constexpr const char* artCrushRate = "art.crush.rate";
inline constexpr const char* artCrushLp = "art.crush.lp";
inline constexpr const char* artCrushJitter = "art.crush.jitter";
inline constexpr const char* artRingFreq = "art.ring.freq";
inline constexpr const char* artRingTweak = "art.ring.tweak"; // meaning set by artRingMode
inline constexpr const char* artRingLp = "art.ring.lp";
inline constexpr const char* artRingMode = "art.ring.mode"; // Wobble / Octave (Earworm / Green Lantern in the DSP)
inline constexpr const char* artRustGrind = "art.rust.grind";
inline constexpr const char* artRustTone = "art.rust.tone";
inline constexpr const char* artRustMode = "art.rust.mode"; // Oxide / Contact
inline constexpr const char* artAmpDrive = "art.amp.drive";
inline constexpr const char* artAmpMids = "art.amp.mids";
inline constexpr const char* artAmpBit = "art.amp.bit";
inline constexpr const char* artAmpTone = "art.amp.tone";
inline constexpr const char* artAmpStereo = "art.amp.stereo";

// --------------------------------------------------------------- modulation
inline constexpr const char* modOn = "mod.on";
inline constexpr const char* modEngine = "mod.engine";
inline constexpr const char* modLevel = "mod.level";
inline constexpr const char* modMix = "mod.mix";

inline constexpr const char* modTapeSat = "mod.tape.sat";
inline constexpr const char* modTapeFlutter = "mod.tape.flutter";
inline constexpr const char* modTapeWear = "mod.tape.wear";
inline constexpr const char* modTapeNoise = "mod.tape.noise";
inline constexpr const char* modTapeTone = "mod.tape.tone";
inline constexpr const char* modTapeStereo = "mod.tape.stereo";

inline constexpr const char* modTremAmount = "mod.trem.amount";
inline constexpr const char* modTremRate = "mod.trem.rate"; // meaning set by modTremSync
inline constexpr const char* modTremSync = "mod.trem.sync"; // false = ms, true = tempo
inline constexpr const char* modTremShape = "mod.trem.shape";
inline constexpr const char* modTremTube = "mod.trem.tube";

inline constexpr const char* modChorusRate = "mod.chorus.rate";
inline constexpr const char* modChorusDepth = "mod.chorus.depth";
inline constexpr const char* modChorusPhase = "mod.chorus.phase";

inline constexpr const char* modPhaseRate = "mod.phase.rate";
inline constexpr const char* modPhaseDepth = "mod.phase.depth";

// -------------------------------------------------------------------- delay
inline constexpr const char* dlyOn = "dly.on";
inline constexpr const char* dlyLevel = "dly.level"; // module chrome, not Peak Delay's
inline constexpr const char* dlyLeftTime = "dly.ltime";
inline constexpr const char* dlyRightTime = "dly.rtime";
inline constexpr const char* dlySync = "dly.sync";
inline constexpr const char* dlyTimeUnit = "dly.timeunit";
inline constexpr const char* dlyType = "dly.dtype";
inline constexpr const char* dlyFeedback = "dly.fb";
inline constexpr const char* dlyMix = "dly.mix";
inline constexpr const char* dlyWear = "dly.tape";
inline constexpr const char* dlyFlutter = "dly.flutter";
inline constexpr const char* dlyDrift = "dly.mod";
inline constexpr const char* dlyPhaser = "dly.phaser";
inline constexpr const char* dlyLoCut = "dly.locut";
inline constexpr const char* dlyHiCut = "dly.hicut";
inline constexpr const char* dlyTapePre = "dly.tapepre";

// ------------------------------------------------------------------- reverb
inline constexpr const char* revOn = "rev.on";
inline constexpr const char* revEngine = "rev.engine";
inline constexpr const char* revLevel = "rev.level";
inline constexpr const char* revMix = "rev.mix";

inline constexpr const char* revSpaceDecay = "rev.space.decay";
inline constexpr const char* revSpaceShimmer = "rev.space.shimmer";
inline constexpr const char* revSpaceLoCut = "rev.space.locut";
inline constexpr const char* revSpaceReso = "rev.space.reso";

inline constexpr const char* revSpringDecay = "rev.spring.decay";
inline constexpr const char* revSpringTension = "rev.spring.tension";
inline constexpr const char* revSpringLoCut = "rev.spring.locut";

// -------------------------------------------------------------------- chain
// A Lehmer-code index into the 24 permutations of the four modules - see
// ChainOrder.h. Index 0 is Artifact, Modulation, Delay, Reverb: today's order.
inline constexpr const char* chainOrder = "chain.order";

} // namespace ee::alpine::id
