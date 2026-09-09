import {
  ModIcon,
  PhaserIcon,
  SpaceIcon,
  SpringIcon,
  TapeIcon,
  TremoloIcon,
} from "@synthpeak/pedal-ui";

/**
 * What each engine *is*, as data: its name, its glyph, and the knobs it puts
 * on the face. `SideModule` renders whichever entry is selected and knows
 * nothing else about them, so adding an engine is adding a row here.
 *
 * `prefix` is the engine's own parameter namespace. Values are per-engine on
 * purpose - switching Chorus to Phaser and back restores the Chorus settings -
 * and that falls out of every engine owning its own parameters rather than
 * sharing a pool. There is no state to keep here; the APVTS is the store.
 *
 * `knobs` is a flat list, laid out two per row. Two rows is the norm; Tape is
 * the one engine that runs to three, because it is the whole of Peak Tape and
 * that pedal has five knobs plus a switch. `centre` is a single knob on a row
 * of its own under the pairs (Tape's bipolar Tone); `toggle` is a Mono/Stereo
 * switch pinned to the bottom of the body, just above the footer. Both are
 * Tape-only for now - an engine that wants either needs a look at the layout,
 * not just a line here.
 */

// Chorus is ModIcon, not a glyph of its own - the mark the design draws for it
// is that one path for path. See pedal-ui's index.js.
export const MOD_ENGINES = [
  {
    name: "Tape",
    icon: <TapeIcon size={22} />,
    prefix: "mod.tape.",
    knobs: [
      ["sat", "Saturation"],
      ["flutter", "Flutter"],
      ["wear", "Wear"],
      ["noise", "Noise"],
    ],
    // A bipolar tilt that rests dead centre - its own row under the four.
    centre: ["tone", "Tone"],
    // The machine's mono/stereo switch, pinned to the bottom of the body.
    toggle: ["stereo", "Mono", "Stereo"],
    // No Mix: the tape transport's wow makes the wet path wander, so any
    // partial blend against the dry combs and is heard as tremolo. It runs
    // fully wet, like Peak Tape - the module's power toggle is its dry/wet.
    // The footer strip stays (its height is fixed in CSS); only the knob goes.
    hideMix: true,
  },
  {
    name: "Trem",
    icon: <TremoloIcon size={22} />,
    prefix: "mod.trem.",
    // The one engine with a display: a tremolo's shape is the whole of what it
    // does, and four knob captions do not show it.
    display: "tremolo",
    knobs: [
      ["amount", "Amount"],
      ["rate", "Rate"],
      ["shape", "Shape"],
      ["tube", "Tube"],
    ],
  },
  {
    name: "Chorus",
    icon: <ModIcon size={20} />,
    prefix: "mod.chorus.",
    knobs: [
      ["rate", "Rate"],
      ["depth", "Depth"],
      ["phase", "Phase"],
    ],
  },
  {
    name: "Phaser",
    icon: <PhaserIcon size={22} />,
    prefix: "mod.phase.",
    knobs: [
      ["rate", "Rate"],
      ["depth", "Depth"],
    ],
  },
];

// Both reverbs show the decay display: what a reverb does is a tail, and the
// Decay knob is the one control whose effect is worth drawing.
export const REVERB_ENGINES = [
  {
    name: "Space",
    icon: <SpaceIcon size={22} />,
    prefix: "rev.space.",
    display: "decay",
    decayId: "rev.space.decay",
    knobs: [
      ["decay", "Decay"],
      ["shimmer", "Shimmer"],
      ["locut", "Low Cut"],
      ["reso", "Reso"],
    ],
  },
  {
    name: "Spring",
    icon: <SpringIcon size={22} />,
    prefix: "rev.spring.",
    display: "decay",
    decayId: "rev.spring.decay",
    // Three, not four: a spring tank has no resonance control to expose. What
    // Space calls Reso is how hard its FDN is allowed to ring, and a tank's
    // equivalent is its decay - so a fourth knob here would have been a second
    // name for the first one.
    knobs: [
      ["decay", "Decay"],
      ["tension", "Tension"],
      ["locut", "Low Cut"],
    ],
  },
];

/** Two knobs per row, so neither side module ever runs past two. */
export function knobRows(knobs) {
  const rows = [];
  for (let i = 0; i < knobs.length; i += 2) rows.push(knobs.slice(i, i + 2));
  return rows;
}
