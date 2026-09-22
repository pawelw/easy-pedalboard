// The one grain-envelope curve the DSP actually shapes (Grainer::envelopeOf),
// factored out so every place that draws it - the plate's own Grain display
// (Displays.jsx's GrainEnvelope) and the Shape knob's own icon
// (GrainShapeIcon.jsx) - reads the same math. Two copies of a curve like this
// drifting apart silently is exactly the kind of thing CLAUDE.md's "Known
// failures" warns distinct copies eventually do.

// Smooth blends the plain attack/decay curve toward Grainer's own
// swell - a linear fade-in of a fixed number of milliseconds, a hold, then a
// short cut - the same two times whatever the grain length
// (GrainerTuning::smoothAttackMs / smoothReleaseMs, mirrored here), squeezed
// only when the grain is too short to hold both.
export const SMOOTH_ATTACK_MS = 73;
export const SMOOTH_RELEASE_MS = 17;

// Where on the Shape travel the swell starts, mirroring
// GrainerConfig.h's kSmoothShapeKnee / smoothForShape. Above it Shape is the
// plain attack/decay window and nothing else.
export const SMOOTH_SHAPE_KNEE = 0.25;

/** Smooth for a 0..1 Shape knob position. */
export function smoothForShape(shape01) {
  const s = Math.min(1, Math.max(0, shape01));
  return s >= SMOOTH_SHAPE_KNEE ? 0 : (SMOOTH_SHAPE_KNEE - s) / SMOOTH_SHAPE_KNEE;
}

/** One grain window's outline, closed down to a baseline `baselineGap` below
    `base` for the fill: a short attack up to the peak, then a decay leaning
    from gentle (Shape 0, Grainer's "soft" end) to a fast pluck that flattens
    early (Shape 1, its "hard" end) - sampled into a polyline since an SVG
    path takes no exponent. `top`/`base` are the canvas's own y-extent (the
    plate's 48-tall display strip, by default); a caller drawing into a
    differently-sized box, like the Shape knob's own icon, passes its own. */
export function grainEnvelopePath(x0, width, shape01, smooth01 = 0, lengthMs = 90, top = 6, base = 42, baselineGap = 4) {
  const s = Math.min(1, Math.max(0, smooth01));
  const attackFrac = 0.5 - shape01 * 0.42; // soft: peak near mid-window, hard: near the start
  const decayShape = 0.6 + shape01 * 3.4; // soft: gentle slope, hard: steep then flat

  const squeeze = Math.min(1, lengthMs / (SMOOTH_ATTACK_MS + SMOOTH_RELEASE_MS));
  const swellAttackFrac = (SMOOTH_ATTACK_MS * squeeze) / lengthMs;
  const swellHold = Math.min(0.98, Math.max(0, 1 - (SMOOTH_RELEASE_MS * squeeze) / (lengthMs * (1 - swellAttackFrac))));

  const attackWithSmooth = attackFrac + s * (swellAttackFrac - attackFrac);
  const peakX = x0 + width * Math.max(0.04, attackWithSmooth);

  const points = [`${x0.toFixed(1)} ${base}`, `${peakX.toFixed(1)} ${top}`];
  const steps = 16;
  for (let i = 1; i <= steps; i++) {
    const t = i / steps;
    const x = peakX + (x0 + width - peakX) * t;
    const fall = 1 - Math.exp(-decayShape * t); // 0 at the peak, sinking toward the baseline
    const swell = t < swellHold ? 0 : (t - swellHold) / (1 - swellHold); // held, then cut
    const y = top + (base - top) * (fall + s * (swell - fall));
    points.push(`${x.toFixed(1)} ${y.toFixed(1)}`);
  }
  points.push(`${(x0 + width).toFixed(1)} ${(base + baselineGap).toFixed(1)}`, `${x0.toFixed(1)} ${(base + baselineGap).toFixed(1)}`);
  return `M ${points.join(" L ")} Z`;
}

// The three windows Shape Family can pick instead of the curve above - see
// Grainer::familyEnvelope (shared/include/ee/dsp/Grainer.h), which this is a
// hand-kept port of the same way grainEnvelopePath is of Grainer::envelopeOf
// (this file's own opening note). The constants here are re-tuned for a good
// read at icon and display size rather than copied from GrainerTuning.h's -
// grainEnvelopePath already sets that precedent (its own attack/decay
// constants are not GrainerTuning::shapeAttackMs*/shapeDecayShape* either).
export const FAMILY_TRIANGLE = 0;
export const FAMILY_GAUSSIAN = 1;
export const FAMILY_SINC = 2;
export const FAMILY_SPIKE = 3;

/** familyEnvelope's own value at position `u` (0..1 across the grain), for
    one of the three symmetric families - Triangle is grainEnvelopePath's own
    curve and is not one of these cases. Gaussian and Spike are 0..1, same
    as Triangle; Sinc is bipolar, same as the engine's - see familyEnvelopePath
    for how that is drawn. */
function familyEnvelopeAt(family, shape01, u) {
  switch (family) {
    case FAMILY_GAUSSIAN: {
      const k = 6 + shape01 * 54; // GrainerTuning::shapeGaussKSoft/Hard's own span
      const d = u - 0.5;
      const raw = Math.exp(-k * d * d);
      const floor = Math.exp(-k * 0.25);
      return (raw - floor) / Math.max(1e-6, 1 - floor);
    }
    case FAMILY_SPIKE: {
      const k = 14 - shape01 * 12.2; // shapeSpikeKSoft/Hard's own span - a spike at 0, a triangle at 1
      const raw = Math.exp(-k * u); // one-sided: full level at the start
      const floor = Math.exp(-k);
      return (raw - floor) / Math.max(1e-6, 1 - floor);
    }
    case FAMILY_SINC: {
      const width = 2.4 + shape01 * 8.6; // shapeSincWidthSoft/Hard's own span
      const x = (u - 0.5) * width * Math.PI;
      const raw = Math.abs(x) < 1e-5 ? 1 : Math.sin(x) / x;
      const taper = 0.5 - 0.5 * Math.cos(2 * Math.PI * u);
      return raw * taper;
    }
    default:
      return 0;
  }
}

/** One grain window's outline for a non-Triangle Shape Family - grainEnvelopePath's
    own sibling, same signature shape (x0/width/top/base/baselineGap), for the
    same two callers (GrainShapeIcon's glyph, Displays.jsx's GrainEnvelope).
    Sinc's own value can go negative (a real sidelobe, not a drawing artefact -
    see Grainer::familyEnvelope's own note); `base` is that curve's centre
    line rather than its floor, so a negative lobe dips below it exactly as
    far as a positive one of the same size rises above `base` toward `top`. */
export function familyEnvelopePath(x0, width, family, shape01, top = 6, base = 42, baselineGap = 4) {
  const steps = 48; // more than grainEnvelopePath's 16 - Sinc needs the resolution to show its ripples
  const points = [];

  for (let i = 0; i <= steps; i++) {
    const u = i / steps;
    const env = familyEnvelopeAt(family, shape01, u);
    const x = x0 + width * u;
    const y = base - env * (base - top);
    points.push(`${x.toFixed(1)} ${y.toFixed(1)}`);
  }

  const closeY = base + baselineGap;
  points.push(`${(x0 + width).toFixed(1)} ${closeY.toFixed(1)}`, `${x0.toFixed(1)} ${closeY.toFixed(1)}`);
  return `M ${points.join(" L ")} Z`;
}
