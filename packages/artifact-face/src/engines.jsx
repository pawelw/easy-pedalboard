import { CrushIcon, FilterIcon, ModIcon, TapeIcon } from "@synthpeak/pedal-ui";

/**
 * The four engines the module steps through, each with its own body. The order
 * is the order of the processor's `engine` choice parameter.
 */
export const ENGINES = [
  { name: "Ring", icon: <ModIcon size={22} />, body: "ring" },
  { name: "Crasher", icon: <CrushIcon size={22} />, body: "crush" },
  { name: "Filter", icon: <FilterIcon size={22} />, body: "filter" },
  { name: "Rust", icon: <TapeIcon size={22} />, body: "rust" },
];

/**
 * The <> wave picker's three positions. `shape01` is fed straight to the
 * filter LFO (ee::dsp::lfoValue's morph) and MUST match the processor's
 * kWaveShape01 table (Peak Artifact's PluginProcessor.cpp, Peak Alpine's
 * kArtWaveShape01) - the face draws the display from this value and the audio
 * path reads the same number the other side.
 */
export const WAVES = [
  { name: "Triangle", shape01: 0.5 },
  { name: "Ramp", shape01: 0.25 },
  { name: "Square", shape01: 1.0 },
];
