// The one grain-envelope curve the DSP actually shapes (Grainer::envelopeOf),
// factored out so every place that draws it - the plate's own Grain display
// (Displays.jsx's GrainEnvelope) and the Shape knob's own icon
// (GrainShapeIcon.jsx) - reads the same math. Two copies of a curve like this
// drifting apart silently is exactly the kind of thing CLAUDE.md's "Known
// failures" warns distinct copies eventually do.

// Smooth (1 - Shape) blends the plain attack/decay curve toward Grainer's own
// swell - a linear fade-in of a fixed number of milliseconds, a hold, then a
// short cut - the same two times whatever the grain length
// (GrainerTuning::smoothAttackMs / smoothReleaseMs, mirrored here), squeezed
// only when the grain is too short to hold both.
export const SMOOTH_ATTACK_MS = 73;
export const SMOOTH_RELEASE_MS = 17;

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
