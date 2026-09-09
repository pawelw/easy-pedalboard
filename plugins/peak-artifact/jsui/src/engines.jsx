import { FilterIcon, ModIcon, PhaserIcon } from "@synthpeak/pedal-ui";

/**
 * The three engines the module steps through. Only Filter has a body - Ring Mod
 * and Bit Crush are placeholders: selectable, and audibly nothing (the
 * processor passes the signal straight through for them). `body: "blank"` is
 * what ArtifactModule renders a dash for.
 *
 * The order is the order of the processor's `engine` choice parameter.
 */
export const ENGINES = [
  { name: "Ring Mod", icon: <ModIcon size={26} />, body: "blank" },
  { name: "Bit Crush", icon: <PhaserIcon size={26} />, body: "blank" },
  { name: "Filter", icon: <FilterIcon size={26} />, body: "filter" },
];

/**
 * The <> wave picker's three positions. `shape01` is fed straight to the
 * filter LFO (ee::dsp::lfoValue's morph) and MUST match the processor's
 * kWaveShape01 table in PluginProcessor.cpp - the face draws the display from
 * this value and the audio path reads the same number the other side.
 */
export const WAVES = [
  { name: "Triangle", shape01: 0.5 },
  { name: "Ramp", shape01: 0.25 },
  { name: "Square", shape01: 1.0 },
];
