import { grainEnvelopePath, familyEnvelopePath, smoothForShape, FAMILY_TRIANGLE } from "./envelopeShape.js";

// Well past SMOOTH_ATTACK_MS + SMOOTH_RELEASE_MS (90ms), so `squeeze` in
// grainEnvelopePath never kicks in here - the icon always reads the plain
// attack/decay-vs-swell blend, the curve's own character, rather than one
// warped by whatever Size happens to be dialled to right now (the big
// display, GrainEnvelope, is the one place that also draws Size's effect).
const ICON_LENGTH_MS = 200;

/** A tiny reading of the same envelope the Grain section's own display
    (Displays.jsx's GrainEnvelope) draws at full size, for the Shape knob's
    own cap - the glyph *is* the value, the way BitBit Wah's WaveIcon is for
    its Shape knob. `shape01` must be the real (uninverted) parameter value:
    GrainFace.jsx reads it straight off the parameter rather than off the
    knob's own (flipped) drag position, so the glyph stays correct regardless
    of the knob's `invert`. `family` (FAMILY_TRIANGLE by default) picks which
    of the four windows the Shape Family dropdown below the knob has selected -
    grainEnvelopePath for Triangle, familyEnvelopePath for the other three. */
export default function GrainShapeIcon({ shape01, family = FAMILY_TRIANGLE, size = 20, color = "currentColor" }) {
  const margin = size * 0.12;
  const top = size * 0.14;
  const base = size * 0.62;
  const d =
    family === FAMILY_TRIANGLE
      ? grainEnvelopePath(margin, size - margin * 2, shape01, smoothForShape(shape01), ICON_LENGTH_MS, top, base, size * 0.08)
      : familyEnvelopePath(margin, size - margin * 2, family, shape01, top, base, size * 0.08);

  return (
    // Knob.css's shared .pui-knob__icon wrapper rotates whatever it's given
    // 180deg (WaveIcon's own note: it compensates by mirroring its path
    // instead) - this counter-rotates back to upright rather than redrawing
    // the curve's own math mirrored, so grainEnvelopePath's output here
    // reads exactly as it does in the big display, peak up.
    <svg
      width={size}
      height={size}
      viewBox={`0 0 ${size} ${size}`}
      fill="none"
      className="pui-grain-shape-icon"
      style={{ transform: "rotate(180deg)" }}
    >
      <path
        d={d}
        stroke={color}
        strokeWidth={Math.max(1.2, size * 0.07)}
        strokeLinecap="round"
        strokeLinejoin="round"
        fill={color}
        fillOpacity={0.16}
      />
    </svg>
  );
}
