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
 *
 * `easy` is the module's "Easy" tab: `{ name, targets }`, where `targets` is
 * the set of this engine's own knobs the one macro knob rides, each with the
 * normalised `min`/`max` it spans as the macro goes 0..1. The macro has no
 * parameter of its own yet (see `JuceMacroKnob`) - it just drives these.
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
    // The Easy tab's macro: one knob that opens saturation, flutter and wear
    // together - the "more tape" move. Starting ranges; the maxed-out target
    // for each is the design's to lock (a screenshot of the Adv panel at full
    // Easy). `min`/`max` are normalised 0..1 positions of the named knob.
    easy: {
      name: "Character",
      targets: [
        { id: "sat", min: 0.15, max: 0.75 },
        { id: "flutter", min: 0.2, max: 0.6 },
        { id: "wear", min: 0.05, max: 0.5 },
      ],
    },
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
    // Flips the Rate knob between a free period in ms and a tempo-locked note
    // division - the same control the Delay module and Peak Trem & Pan carry.
    sync: "mod.trem.sync",
    // Rate is left out on purpose - the tempo feel is the player's to set.
    easy: {
      name: "Depth",
      targets: [
        { id: "amount", min: 0.1, max: 0.9 },
        { id: "shape", min: 0.35, max: 0.65 },
        { id: "tube", min: 0.0, max: 0.5 },
      ],
    },
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
    easy: {
      name: "Lush",
      targets: [
        { id: "depth", min: 0.2, max: 0.85 },
        { id: "rate", min: 0.2, max: 0.5 },
        { id: "phase", min: 0.4, max: 0.95 },
      ],
    },
  },
  {
    name: "Phaser",
    icon: <PhaserIcon size={22} />,
    prefix: "mod.phase.",
    knobs: [
      ["rate", "Rate"],
      ["depth", "Depth"],
    ],
    easy: {
      name: "Sweep",
      targets: [
        { id: "depth", min: 0.2, max: 0.9 },
        { id: "rate", min: 0.15, max: 0.5 },
      ],
    },
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
    // Shimmer is left out - it is a taste control, and not bit-reproducible
    // (see CLAUDE.md), so a macro should not be nudging it under the player.
    easy: {
      name: "Size",
      targets: [
        { id: "decay", min: 0.25, max: 0.9 },
        { id: "reso", min: 0.3, max: 0.65 },
        { id: "locut", min: 0.1, max: 0.35 },
      ],
    },
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
    easy: {
      name: "Boing",
      targets: [
        { id: "decay", min: 0.2, max: 0.85 },
        { id: "tension", min: 0.35, max: 0.7 },
      ],
    },
  },
];

/**
 * The Artifact module's Easy macros, keyed by engine name. Artifact is
 * `ArtifactFace` from `@synthpeak/artifact-face`, a face shared with the
 * standalone Peak Artifact pedal - so its Easy config is handed in from here
 * as a prop rather than living on that package's own engine table, which the
 * standalone pedal (no Easy tab) has no use for.
 *
 * Leaf ids carry the engine's own sub-prefix (`flt.`, `crush.`, `ring.`); the
 * face resolves them through its `art.` ParamScope.
 */
export const ARTIFACT_EASY = {
  Ring: {
    name: "Metal",
    targets: [
      { id: "ring.freq", min: 0.2, max: 0.7 },
      { id: "ring.tweak", min: 0.1, max: 0.6 },
      { id: "ring.lp", min: 0.5, max: 0.95 },
    ],
  },
  Crasher: {
    name: "Crush",
    targets: [
      { id: "crush.bits", min: 0.2, max: 0.8 },
      { id: "crush.rate", min: 0.25, max: 0.8 },
      { id: "crush.jitter", min: 0.0, max: 0.4 },
    ],
  },
  Filter: {
    name: "Sweep",
    targets: [
      { id: "flt.freq", min: 0.2, max: 0.75 },
      { id: "flt.q", min: 0.2, max: 0.7 },
      { id: "flt.range", min: 0.25, max: 0.85 },
    ],
  },
};

/** Two knobs per row, so neither side module ever runs past two. */
export function knobRows(knobs) {
  const rows = [];
  for (let i = 0; i < knobs.length; i += 2) rows.push(knobs.slice(i, i + 2));
  return rows;
}
