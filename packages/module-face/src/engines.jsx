import {
  FilterIcon,
  ModIcon,
  PhaserIcon,
  ShimmerIcon,
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
 * `prefix` is the engine's own parameter namespace, as a leaf - `trem.`, not
 * `mod.trem.`. The face resolves it through the ParamScope its host declares,
 * so the same table binds `trem.rate` in BitBit Modulation and `mod.trem.rate`
 * in BitBit Alpine. Values are per-engine on purpose - switching Chorus to Phaser
 * and back restores the Chorus settings - and that falls out of every engine
 * owning its own parameters rather than sharing a pool. There is no state to
 * keep here; the APVTS is the store.
 *
 * `knobs` is a flat list of `[id, caption]` pairs, or `[id, caption,
 * scaleFrom]` when a knob doesn't read from its own minimum - a Hi Cut rests
 * open at the top of its travel and counts down, the same "max" distinction
 * BitBit Delay's own Hi Cut draws (see Knob's `scaleFrom` doc); every knob a
 * reverb engine's `hicut` id names wants it. Laid out two per row. Two rows
 * is the norm; Tape is the one engine that runs to three, because it is the
 * whole of BitBit Tape and
 * that pedal has five knobs plus a switch. `centre` is a single knob on a row
 * of its own under the pairs (Tape's bipolar Tone), `lead` a list of one or
 * more `[id, caption]` pairs on a row of its own above them, centred as a
 * group (Studio reverb's Decay and Size, Shimmer's Decay and Damping);
 * `toggle` is a Mono/Stereo
 * switch pinned to the bottom of the body, just above the footer. `centre`
 * and `toggle` are each used by one engine so far - an engine that wants one
 * needs a look at the layout, not just a line here.
 *
 * `body: "filter"` swaps the knob grid for SideModule's FilterBody - Filter's
 * controls carry a wave picker and a Sync pill under two of its knobs, which a
 * flat list cannot describe.
 *
 * `easy` is the module's "Easy" tab: `{ name, targets }`, where `targets` is
 * the set of this engine's own knobs the one macro knob rides, each with the
 * normalised `min`/`max` it spans as the macro goes 0..1. The macro has no
 * parameter of its own yet (see `JuceMacroKnob`) - it just drives these. Only
 * read when the host turns `easyTab` on (BitBit Alpine does).
 *
 * `picker` is a discrete choice control above the knob grid, for a setting
 * that steps rather than turns (the Shimmer engine's octave): `{ paramId,
 * options, label }`, where `options` is the ordered list of display strings -
 * the choice parameter's own value is the index into it, so this must list
 * them in the same order the processor's `AudioParameterChoice` does. Drawn
 * with the same `EngineStepper` the module's own engine picker is, one below
 * the other, since a run of "what and how" wells reads as one family.
 */

// Chorus is ModIcon, not a glyph of its own - the mark the design draws for it
// is that one path for path. See pedal-ui's index.js.
export const MOD_ENGINES = [
  {
    name: "Tape",
    icon: <TapeIcon size={22} />,
    prefix: "tape.",
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
    // fully wet, like BitBit Tape - the module's power toggle is its dry/wet.
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
    prefix: "trem.",
    // A tremolo's shape is the whole of what it does, and four knob captions
    // do not show it. Chorus and Phaser draw their LFO the same way.
    display: "tremolo",
    knobs: [
      ["amount", "Amount"],
      ["rate", "Rate"],
      ["shape", "Shape"],
      ["tube", "Tube"],
    ],
    // Flips the Rate knob between a free period in ms and a tempo-locked note
    // division - the same control the Delay module and BitBit Trem & Pan carry.
    sync: "trem.sync",
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
    prefix: "chorus.",
    display: "chorus",
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
    prefix: "phase.",
    display: "phaser",
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
  {
    // BitBit Wah's engine as an LFO-swept low-pass, Decay pinned fully up. It was
    // BitBit Artifact's third engine until it moved here - appended, so a saved
    // engine index keeps its meaning.
    name: "Filter",
    icon: <FilterIcon size={22} />,
    prefix: "filter.",
    display: "filter",
    body: "filter",
    // Time is left out, like Tremolo's Rate - the tempo feel is the player's.
    easy: {
      name: "Sweep",
      targets: [
        { id: "freq", min: 0.2, max: 0.75 },
        { id: "q", min: 0.2, max: 0.7 },
        { id: "range", min: 0.25, max: 0.85 },
      ],
    },
  },
];

/**
 * The Filter engine's <> wave picker. `shape01` is fed straight to the LFO
 * (ee::dsp::lfoValue's morph) and MUST match kFilterWaveShape01 in
 * shared/include/ee/fx/ModulationControls.h - the face draws its glyph from
 * this value and the audio path reads the same number the other side.
 */
export const FILTER_WAVES = [
  { name: "Triangle", shape01: 0.5 },
  { name: "Ramp", shape01: 0.25 },
  { name: "Square", shape01: 1.0 },
];

// Every reverb shows the same display - its tail as a time x frequency map,
// drawn from that engine's own decay model (pedal-ui's ReverbScope), so every
// knob on the engine shows up in it. `display` names the model. In the order
// of the owner's `engine` choice - Spring, Shimmer, Studio.
export const REVERB_ENGINES = [
  {
    name: "Spring",
    icon: <SpringIcon size={22} />,
    prefix: "spring.",
    display: "reverb",
    reverb: "spring",
    // No Reso: a spring tank has no resonance control to expose. What Shimmer
    // calls Reso is how hard its FDN is allowed to ring, and a tank's
    // equivalent is its decay - so a knob for it here would have been a
    // second name for the first one.
    knobs: [
      ["decay", "Decay"],
      ["tension", "Tension"],
      ["locut", "Low Cut"],
      ["hicut", "Hi Cut", "max"],
    ],
    easy: {
      name: "Boing",
      targets: [
        { id: "decay", min: 0.2, max: 0.85 },
        { id: "tension", min: 0.35, max: 0.7 },
      ],
    },
  },
  {
    // The FDN this module was called Space for, and still bound to its
    // `space.` ids so a saved session keeps its settings. Decay is a knob
    // again (4-10 s - ee::fx::ReverbModule::kMinShimmerDecay..kMaxShimmerDecay,
    // narrower than a plain FdnReverb's own range); its own Shimmer feedback
    // and Reso stay fixed inside ee::fx::ReverbModule::setShimmer.
    name: "Shimmer",
    icon: <ShimmerIcon size={22} />,
    prefix: "space.",
    display: "reverb",
    reverb: "shimmer",
    picker: { paramId: "octave", options: ["-1 Oct", "0", "+1 Oct"], label: "Shimmer octave" },
    lead: [
      ["decay", "Decay"],
      ["damping", "Damping"],
    ],
    knobs: [
      ["locut", "Low Cut"],
      ["hicut", "Hi Cut", "max"],
    ],
  },
  {
    // ee::dsp::SpaceReverb, voiced against NI Raum. Decay and Size on their
    // own at the top - Size is a real parameter (ee::dsp::SpaceReverb::setSize),
    // scaling the room's own travel times, not the Easy tab's macro (see
    // `easy` below, which used to share this name and was renamed to Room to
    // stop meaning two different things on the same engine) - then the tail's
    // shape in time and colour (Pre-delay, Damping), then the two cuts on the
    // finished wet.
    name: "Studio",
    icon: <SpaceIcon size={22} />,
    prefix: "studio.",
    display: "reverb",
    reverb: "studio",
    lead: [
      ["decay", "Decay"],
      ["size", "Size"],
    ],
    knobs: [
      ["predelay", "Pre-delay"],
      ["damping", "Damping"],
      ["locut", "Low Cut"],
      ["hicut", "Hi Cut", "max"],
    ],
    // A bigger space is a longer tail that arrives a little later. Its own
    // macro, riding the existing knobs - not the real Size parameter above,
    // which this pre-dates; kept as a coarse "feel" control alongside the
    // precise one now that both exist.
    easy: {
      name: "Room",
      targets: [
        { id: "decay", min: 0.25, max: 0.9 },
        { id: "predelay", min: 0.0, max: 0.3 },
        { id: "locut", min: 0.1, max: 0.35 },
      ],
    },
  },
];

/** Two knobs per row, so neither module ever runs past two. */
export function knobRows(knobs) {
  const rows = [];
  for (let i = 0; i < knobs.length; i += 2) rows.push(knobs.slice(i, i + 2));
  return rows;
}
