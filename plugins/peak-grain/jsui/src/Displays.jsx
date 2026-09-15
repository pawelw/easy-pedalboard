import { useJuceSliderValue } from "@synthpeak/pedal-ui/juce";

/**
 * The five recessed displays across the plate (COMPONENTS.md's "Displays"
 * table) plus the full-width scope strip. Grain/Random/Reverb are decorative
 * and static, matching the design_handoff mock exactly - only Pitch Weights
 * is documented as reading live values ("heights from Low / Unison / High").
 */

/** Grain's envelope curve: one fixed path, fast attack into a long decay -
    the shape a grain's window looks like, not tied to the Shape knob (that
    knob's own live-morphing glyph was the native face's *cap* icon, not this
    panel; nothing in the revised mock asks the panel itself to react). */
export function GrainEnvelope({ accent }) {
  return (
    <svg width="100%" height="48" viewBox="0 0 240 48" preserveAspectRatio="none" fill="none">
      <path
        d="M6 42 L26 6 C 62 10 120 37 234 41"
        stroke={accent}
        strokeWidth="1.6"
        strokeLinecap="round"
        vectorEffect="non-scaling-stroke"
      />
      <path d="M6 42 L26 6 C 62 10 120 37 234 41 L234 46 L6 46 Z" fill={accent} fillOpacity="0.1" />
    </svg>
  );
}

/** Three bars, heights from the Low/Unison/High pitch-weight parameters -
    the one display in this row that COMPONENTS.md documents as reactive. */
export function PitchWeights({ accent }) {
  const [low] = useJuceSliderValue("plow");
  const [unison] = useJuceSliderValue("puni");
  const [high] = useJuceSliderValue("phigh");

  return (
    <div className="pg-weights">
      <div className="pg-weights__baseline" />
      {[low, unison, high].map((v, i) => (
        <div key={i} className="pg-weights__col">
          <div
            className="pg-weights__bar"
            style={{ height: `${Math.max(2, v * 40)}px`, background: accent }}
          />
        </div>
      ))}
    </div>
  );
}

// Fixed scatter - 11 grains, left/top percent and opacity straight off the
// mock (Peak Grain - 1c.dc.html), which does not tie this to Stereo/Scatter.
const RANDOM_DOTS = [
  { left: "8%", top: "62%", size: 5, opacity: 0.35 },
  { left: "16%", top: "34%", size: 3, opacity: 0.5 },
  { left: "24%", top: "70%", size: 3, opacity: 0.65 },
  { left: "33%", top: "46%", size: 5, opacity: 0.8 },
  { left: "41%", top: "26%", size: 3, opacity: 0.35 },
  { left: "49%", top: "58%", size: 3, opacity: 0.5 },
  { left: "57%", top: "38%", size: 5, opacity: 0.65 },
  { left: "64%", top: "74%", size: 3, opacity: 0.8 },
  { left: "72%", top: "50%", size: 3, opacity: 0.35 },
  { left: "80%", top: "30%", size: 5, opacity: 0.5 },
  { left: "88%", top: "64%", size: 3, opacity: 0.65 },
];

/** L/R stereo field: a centre line and a scatter of grains either side. */
export function RandomField({ accent }) {
  return (
    <div className="pg-field">
      <div className="pg-field__hline" />
      <span className="pg-field__side pg-field__side--l">L</span>
      <span className="pg-field__side pg-field__side--r">R</span>
      {RANDOM_DOTS.map((d, i) => (
        <span
          key={i}
          className="pg-field__dot"
          style={{ left: d.left, top: d.top, width: d.size, height: d.size, background: accent, opacity: d.opacity }}
        />
      ))}
    </div>
  );
}

// Falling bar heights, 28 -> 3px - Reverb's tail (COMPONENTS.md: "eight 5px
// bars falling 28 -> 3px").
const TAIL_HEIGHTS = [28, 22, 17, 13, 9, 6, 4, 3];

export function ReverbTail({ accent }) {
  return (
    <div className="pg-tail">
      {TAIL_HEIGHTS.map((h, i) => (
        <div key={i} className="pg-tail__bar" style={{ height: `${h}px`, background: accent }} />
      ))}
    </div>
  );
}

// The scope strip's decorative wash - grain cloud (purple, left) fading into
// delay repeats (green, right) over a faint cyan reverb tint. Lifted straight
// off Grain Scope.dc.html: a fixed background-image, not driven by any
// parameter (see App.jsx's own note on why - nothing in the handoff asks for
// this to animate).
const SCOPE_BACKGROUND = [
  "radial-gradient(3px 3px at 6% 62%, rgba(179,155,216,.9), transparent 65%)",
  "radial-gradient(2px 2px at 9% 38%, rgba(179,155,216,.7), transparent 65%)",
  "radial-gradient(4px 4px at 12% 72%, rgba(179,155,216,.85), transparent 65%)",
  "radial-gradient(2px 2px at 14% 46%, rgba(179,155,216,.6), transparent 65%)",
  "radial-gradient(3px 3px at 17% 30%, rgba(179,155,216,.75), transparent 65%)",
  "radial-gradient(5px 5px at 19% 58%, rgba(179,155,216,.8), transparent 65%)",
  "radial-gradient(2px 2px at 22% 80%, rgba(179,155,216,.6), transparent 65%)",
  "radial-gradient(3px 3px at 25% 44%, rgba(179,155,216,.8), transparent 65%)",
  "radial-gradient(4px 4px at 28% 66%, rgba(179,155,216,.7), transparent 65%)",
  "radial-gradient(2px 2px at 31% 26%, rgba(179,155,216,.55), transparent 65%)",
  "radial-gradient(3px 3px at 33% 54%, rgba(179,155,216,.7), transparent 65%)",
  "radial-gradient(5px 5px at 36% 74%, rgba(179,155,216,.65), transparent 65%)",
  "radial-gradient(2px 2px at 39% 40%, rgba(179,155,216,.5), transparent 65%)",
  "radial-gradient(3px 3px at 42% 60%, rgba(179,155,216,.55), transparent 65%)",
  "radial-gradient(3px 3px at 52% 56%, rgba(163,206,122,.6), transparent 65%)",
  "radial-gradient(2px 2px at 57% 40%, rgba(163,206,122,.45), transparent 65%)",
  "radial-gradient(4px 4px at 61% 68%, rgba(163,206,122,.5), transparent 65%)",
  "radial-gradient(2px 2px at 66% 48%, rgba(163,206,122,.38), transparent 65%)",
  "radial-gradient(3px 3px at 71% 62%, rgba(163,206,122,.34), transparent 65%)",
  "radial-gradient(2px 2px at 76% 36%, rgba(163,206,122,.28), transparent 65%)",
  "radial-gradient(3px 3px at 81% 58%, rgba(163,206,122,.24), transparent 65%)",
  "radial-gradient(2px 2px at 87% 50%, rgba(163,206,122,.18), transparent 65%)",
  "radial-gradient(2px 2px at 93% 62%, rgba(163,206,122,.12), transparent 65%)",
  "linear-gradient(90deg, rgba(127,210,216,.05), rgba(127,210,216,.02) 55%, transparent)",
].join(", ");

export function GrainScope() {
  return (
    <div className="pg-scope" style={{ backgroundImage: SCOPE_BACKGROUND }}>
      <div className="pg-scope__hline" />
      <div className="pg-scope__vdivider" />
      <span className="pg-scope__label pg-scope__label--grains">GRAINS</span>
      <span className="pg-scope__label pg-scope__label--delay">DELAY</span>
    </div>
  );
}
