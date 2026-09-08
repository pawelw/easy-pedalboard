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
 * `knobs` is a flat list, laid out two per row. Never more than two rows: a
 * 186px module that grows a third row stops being the narrow thing the layout
 * is built around, so an engine that needs five controls needs a rethink
 * rather than another line here.
 */

// Chorus is ModIcon, not a glyph of its own - the mark the design draws for it
// is that one path for path. See pedal-ui's index.js.
export const MOD_ENGINES = [
  {
    name: "Tape",
    icon: <TapeIcon size={26} />,
    prefix: "mod.tape.",
    knobs: [
      ["sat", "Saturation"],
      ["flutter", "Flutter"],
      ["wear", "Wear"],
      ["noise", "Noise"],
    ],
  },
  {
    name: "Tremolo",
    icon: <TremoloIcon size={26} />,
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
    icon: <ModIcon size={26} />,
    prefix: "mod.chorus.",
    knobs: [
      ["rate", "Rate"],
      ["depth", "Depth"],
      ["phase", "Phase"],
    ],
  },
  {
    name: "Phaser",
    icon: <PhaserIcon size={26} />,
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
    icon: <SpaceIcon size={26} />,
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
    icon: <SpringIcon size={26} />,
    prefix: "rev.spring.",
    display: "decay",
    decayId: "rev.spring.decay",
    knobs: [
      ["decay", "Decay"],
      ["tension", "Tension"],
      ["locut", "Low Cut"],
      ["reso", "Reso"],
    ],
  },
];

/** Two knobs per row, so neither side module ever runs past two. */
export function knobRows(knobs) {
  const rows = [];
  for (let i = 0; i < knobs.length; i += 2) rows.push(knobs.slice(i, i + 2));
  return rows;
}
